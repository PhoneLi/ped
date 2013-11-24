
#include "pe.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */
#ifdef HAVE_EPOLL
    #include "pe_epoll.c"
#else
    #include "pe_select.c"
#endif


peEventLoop *
peCreateEventLoop(int setsize) {
    peEventLoop *eventLoop;
    int i;

    if ((eventLoop = pmalloc(sizeof(*eventLoop))) == NULL) goto err;

    eventLoop->events = pmalloc(sizeof(peFileEvent)*setsize);
    eventLoop->fired = pmalloc(sizeof(peFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    eventLoop->setsize = setsize;
    eventLoop->lastTime = time(NULL);

    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;

    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
    if (peApiCreate(eventLoop) == -1) goto err;
    /* Events with mask == PE_NONE are not set. So let's initialize the
     * vector with it. */
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = PE_NONE;
    return eventLoop;

 err:
    if (eventLoop) {
        pfree(eventLoop->events);
        pfree(eventLoop->fired);
        pfree(eventLoop);
    }
    return NULL;
}

void 
peDeleteEventLoop(peEventLoop *eventLoop) {
    peApiFree(eventLoop);
    pfree(eventLoop->events);
    pfree(eventLoop->fired);
    pfree(eventLoop);
}

void 
peStop(peEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

int 
peCreateFileEvent(peEventLoop *eventLoop, int fd, int mask,
                      peFileProc *proc, void *clientData){
    if (fd >= eventLoop->setsize) return PE_ERR;
    peFileEvent *fe = &eventLoop->events[fd];

    if (peApiAddEvent(eventLoop, fd, mask) == -1)
        return PE_ERR;

    fe->mask |= mask;
    if (mask & PE_READABLE) fe->rfileProc = proc;
    if (mask & PE_WRITABLE) fe->wfileProc = proc;

    fe->clientData = clientData;

    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return PE_OK;
}

void 
peDeleteFileEvent(peEventLoop *eventLoop, int fd, int mask){
    if (fd >= eventLoop->setsize) return;
    peFileEvent *fe = &eventLoop->events[fd];

    if (fe->mask == PE_NONE) return;

    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == PE_NONE) {
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != PE_NONE) break;
        eventLoop->maxfd = j;
    }

    peApiDelEvent(eventLoop, fd, mask);
}

int 
peGetFileEvents(peEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    peFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

static void 
peGetTime(long *seconds, long *milliseconds){
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

static void 
peAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    peGetTime(&cur_sec, &cur_ms);

    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;

    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

long long 
peCreateTimeEvent(peEventLoop *eventLoop, long long milliseconds,
                            peTimeProc *proc, void *clientData,
                            peEventFinalizerProc *finalizerProc){
    long long id = eventLoop->timeEventNextId++;
    peTimeEvent *te;

    te = pmalloc(sizeof(*te));
    if (te == NULL) return PE_ERR;

    te->id = id;

    peAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;

    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;

    return id;
}

int 
peDeleteTimeEvent(peEventLoop *eventLoop, long long id){
    peTimeEvent *te, *prev = NULL;

    te = eventLoop->timeEventHead;
    while(te) {
        if (te->id == id) {

            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else
                prev->next = te->next;

            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);

            pfree(te);

            return PE_OK;
        }
        prev = te;
        te = te->next;
    }

    return PE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static 
peTimeEvent *peSearchNearestTimer(peEventLoop *eventLoop)
{
    peTimeEvent *te = eventLoop->timeEventHead;
    peTimeEvent *nearest = NULL;

    while(te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
            (te->when_sec == nearest->when_sec &&
             te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process time events */
static int processTimeEvents(peEventLoop *eventLoop) {
    int processed = 0;
    peTimeEvent *te;
    long long maxId;
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }

    eventLoop->lastTime = now;

    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        
        peGetTime(&now_sec, &now_ms);

        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
            {
                int retval;

                id = te->id;
                retval = te->timeProc(eventLoop, id, te->clientData);
                processed++;
                /* After an event is processed our time event list may
                 * no longer be the same, so we restart from head.
                 * Still we make sure to don't process events registered
                 * by event handlers itself in order to don't loop forever.
                 * To do so we saved the max ID we want to handle.
                 *
                 * FUTURE OPTIMIZATIONS:
                 * Note that this is NOT great algorithmically. Redis uses
                 * a single time event so it's not a problem but the right
                 * way to do this is to add the new elements on head, and
                 * to flag deleted elements in a special way for later
                 * deletion (putting references to the nodes to delete into
                 * another linked list). */

                if (retval != PE_NOMORE) {
                    peAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
                } else {
                    peDeleteTimeEvent(eventLoop, id);
                }

                te = eventLoop->timeEventHead;
            } else {
            te = te->next;
        }
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 *
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurrs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 *
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 *
 * if flags has AE_FILE_EVENTS set, file events are processed.
 *
 * if flags has AE_TIME_EVENTS set, time events are processed.
 *
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. 
 */
int 
peProcessEvents(peEventLoop *eventLoop, int flags){
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    if (!(flags & PE_TIME_EVENTS) && !(flags & PE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    if (eventLoop->maxfd != -1 ||
        ((flags & PE_TIME_EVENTS) && !(flags & PE_DONT_WAIT))) {
        int j;
        peTimeEvent *shortest = NULL;
        struct timeval tv, *tvp;

        if (flags & PE_TIME_EVENTS && !(flags & PE_DONT_WAIT))
            shortest = peSearchNearestTimer(eventLoop);
        if (shortest) {
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            peGetTime(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                tvp->tv_sec --;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
            }

            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        } else {
            
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to se the timeout
             * to zero */
            if (flags & PE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                tvp = NULL; /* wait forever */
            }
        }

        numevents = peApiPoll(eventLoop, tvp);
        for (j = 0; j < numevents; j++) {
            peFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

            /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
                * processed, so we check if the event is still valid. */
            if (fe->mask & mask & PE_READABLE) {
                rfired = 1; 
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);
            }
            if (fe->mask & mask & PE_WRITABLE) {
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }

            processed++;
        }
    }

    /* Check time events */
    if (flags & PE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for millseconds until the given file descriptor becomes
 * writable/readable/exception */
int 
peWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & PE_READABLE) pfd.events |= POLLIN;
    if (mask & PE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= PE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= PE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= PE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= PE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

void 
peMain(peEventLoop *eventLoop) {

    eventLoop->stop = 0;

    while (!eventLoop->stop) {

        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);

        peProcessEvents(eventLoop, PE_ALL_EVENTS);
    }
}

char *
peGetApiName(void) {
    return peApiName();
}

void 
peSetBeforeSleepProc(peEventLoop *eventLoop, peBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
