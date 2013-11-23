
#include "pe.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */
#ifdef HAVE_EPOLL
    #include "pe_epoll.c"
#else
    #include "pe_select.c"
#endif


/*
 * 初始化事件处理器状态
 */
peEventLoop *peCreateEventLoop(int setsize) {
    peEventLoop *eventLoop;
    int i;

    // 创建事件状态结构
    if ((eventLoop = pmalloc(sizeof(*eventLoop))) == NULL) goto err;

    // 初始化文件事件结构和已就绪文件事件结构
    eventLoop->events = pmalloc(sizeof(peFileEvent)*setsize);
    eventLoop->fired = pmalloc(sizeof(peFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    eventLoop->setsize = setsize;
    eventLoop->lastTime = time(NULL);

    // 初始化时间事件结构
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;

    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
    if (peApiCreate(eventLoop) == -1) goto err;
    /* Events with mask == AE_NONE are not set. So let's initialize the
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

/*
 * 删除事件处理器
 */
void peDeleteEventLoop(peEventLoop *eventLoop) {
    peApiFree(eventLoop);
    pfree(eventLoop->events);
    pfree(eventLoop->fired);
    pfree(eventLoop);
}

/*
 * 停止事件处理器
 */
void peStop(peEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

/*
 * 根据 mask 参数的值，监听 fd 文件的状态，
 * 当 fd 可用时，执行 proc 函数
 */
int peCreateFileEvent(peEventLoop *eventLoop, int fd, int mask,
                      peFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) return PE_ERR;
    peFileEvent *fe = &eventLoop->events[fd];

    // 监听指定 fd
    if (peApiAddEvent(eventLoop, fd, mask) == -1)
        return PE_ERR;

    // 设置文件事件类型
    fe->mask |= mask;
    if (mask & PE_READABLE) fe->rfileProc = proc;
    if (mask & PE_WRITABLE) fe->wfileProc = proc;

    fe->clientData = clientData;

    // 如果有需要，更新事件处理器的最大 fd
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return PE_OK;
}

/*
 * 将 fd 从 mask 指定的监听队列中删除
 */
void peDeleteFileEvent(peEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;
    peFileEvent *fe = &eventLoop->events[fd];

    // 未设置监听的事件类型，直接返回
    if (fe->mask == PE_NONE) return;

    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == PE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != PE_NONE) break;
        eventLoop->maxfd = j;
    }

    // 取消监听给定 fd
    peApiDelEvent(eventLoop, fd, mask);
}

/*
 * 获取给定 fd 正在监听的事件类型
 */
int peGetFileEvents(peEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    peFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/*
 * 取出当前时间的秒和毫秒，
 * 并分别将它们保存到 seconds 和 milliseconds 参数中
 */
static void peGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

/*
 * 为当前时间加上 milliseconds 秒。
 */
static void peAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    // 获取当前时间
    peGetTime(&cur_sec, &cur_ms);

    // 计算增加 milliseconds 之后的秒数和毫秒数
    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;

    // 进位：
    // 如果 when_ms 大于等于 1000
    // 那么将 when_sec 增大一秒
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

/*
 * 创建时间事件
 */
long long peCreateTimeEvent(peEventLoop *eventLoop, long long milliseconds,
                            peTimeProc *proc, void *clientData,
                            peEventFinalizerProc *finalizerProc)
{
    // 更新时间计数器
    long long id = eventLoop->timeEventNextId++;
    peTimeEvent *te;

    te = pmalloc(sizeof(*te));
    if (te == NULL) return PE_ERR;

    te->id = id;

    // 设定处理事件的时间
    peAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;

    // 将新事件放入表头
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;

    return id;
}

/*
 * 删除给定 id 的时间事件
 */
int peDeleteTimeEvent(peEventLoop *eventLoop, long long id)
{
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
// 寻找里目前时间最近的时间事件
// 因为链表是乱序的，所以查找复杂度为 O（N）
static peTimeEvent *peSearchNearestTimer(peEventLoop *eventLoop)
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

/* Process time events
 *
 * 处理所有已到达的时间事件
 */
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
    // 通过重置事件的运行时间，
    // 防止因时间穿插（skew）而造成的事件处理混乱
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    // 更新最后一次处理时间事件的时间
    eventLoop->lastTime = now;

    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

        // 跳过无效事件
        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        
        // 获取当前时间
        peGetTime(&now_sec, &now_ms);

        // 如果当前时间大于或等于事件的执行时间，那么执行这个事件
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

                // 记录是否有需要循环执行这个事件时间
                if (retval != PE_NOMORE) {
                    // 是的， retval 毫秒之后继续执行这个时间事件
                    peAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
                } else {
                    // 不，将这个事件删除
                    peDeleteTimeEvent(eventLoop, id);
                }

                // 因为执行事件之后，事件列表可能已经被改变了
                // 因此需要将 te 放回表头，继续开始执行事件
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
 * 处理所有已到达的时间事件，以及所有已就绪的文件事件。
 *
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurrs (if any).
 *
 * 如果不传入特殊 flags 的话，那么函数睡眠直到文件事件就绪，
 * 或者下个时间事件到达（如果有的话）。
 *
 * If flags is 0, the function does nothing and returns.
 * 如果 flags 为 0 ，那么函数不作动作，直接返回。
 *
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * 如果 flags 包含 AE_ALL_EVENTS ，所有类型的事件都会被处理。
 *
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * 如果 flags 包含 AE_FILE_EVENTS ，那么处理文件事件。
 *
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * 如果 flags 包含 AE_TIME_EVENTS ，那么处理时间事件。
 *
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 * 如果 flags 包含 AE_DONT_WAIT ，
 * 那么函数在处理完所有不许阻塞的事件之后，即刻返回。
 *
 * The function returns the number of events processed. 
 * 函数的返回值为已处理事件的数量
 */
int peProcessEvents(peEventLoop *eventLoop, int flags)
{
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

        // 获取最近的时间事件
        if (flags & PE_TIME_EVENTS && !(flags & PE_DONT_WAIT))
            shortest = peSearchNearestTimer(eventLoop);
        if (shortest) {
            // 如果时间事件存在的话
            // 那么根据最近可执行时间事件和现在时间的时间差来决定文件事件的阻塞时间
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            // 计算距今最近的时间事件还要多久才能达到
            // 并将该时间距保存在 tv 结构中
            peGetTime(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                tvp->tv_sec --;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
            }

            // 时间差小于 0 ，说明事件已经可以执行了，将秒和毫秒设为 0 （不阻塞）
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        } else {
            
            // 执行到这一步，说明没有时间事件
            // 那么根据 AE_DONT_WAIT 是否设置来决定是否阻塞，以及阻塞的时间长度

            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to se the timeout
             * to zero */
            if (flags & PE_DONT_WAIT) {
                // 设置文件事件不阻塞
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                // 文件事件可以阻塞直到有事件到达为止
                tvp = NULL; /* wait forever */
            }
        }

        // 处理文件事件，阻塞时间由 tvp 决定
        numevents = peApiPoll(eventLoop, tvp);
        for (j = 0; j < numevents; j++) {
            // 从已就绪数组中获取事件
            peFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

            /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
                * processed, so we check if the event is still valid. */
            if (fe->mask & mask & PE_READABLE) {
                // 读事件
                rfired = 1; // 确保读/写事件只能执行其中一个
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);
            }
            if (fe->mask & mask & PE_WRITABLE) {
                // 写事件
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }

            processed++;
        }
    }

    /* Check time events */
    // 执行时间事件
    if (flags & PE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for millseconds until the given file descriptor becomes
 * writable/readable/exception */
int peWait(int fd, int mask, long long milliseconds) {
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

// 事件处理器的主循环
void peMain(peEventLoop *eventLoop) {

    eventLoop->stop = 0;

    while (!eventLoop->stop) {

        // 如果有需要在事件处理前执行的函数，那么运行它
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);

        // 开始处理事件
        peProcessEvents(eventLoop, PE_ALL_EVENTS);
    }
}

// 返回所使用的多路复用库的名称
char *peGetApiName(void) {
    return peApiName();
}

// 设置处理事件前需要被执行的函数
void peSetBeforeSleepProc(peEventLoop *eventLoop, peBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
