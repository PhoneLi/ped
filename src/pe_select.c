#include <sys/types.h>
#include <sys/times.h>
#include <sys/select.h>
#include "pe.h"
#include "pmalloc.h"

typedef struct peApiState {
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to reuse
     * FD sets after select(). */
    fd_set _rfds, _wfds;
} peApiState;

static int peApiCreate(peEventLoop *eventLoop) {
    peApiState *state = pmalloc(sizeof(peApiState));

    if (!state) return -1;
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    eventLoop->apidata = state;
    return 0;
}

static void peApiFree(peEventLoop *eventLoop) {
    pfree(eventLoop->apidata);
}

static int peApiAddEvent(peEventLoop *eventLoop, int fd, int mask) {
    peApiState *state = eventLoop->apidata;

    if (mask & PE_READABLE) FD_SET(fd,&state->rfds);
    if (mask & PE_WRITABLE) FD_SET(fd,&state->wfds);
    return 0;
}

static void peApiDelEvent(peEventLoop *eventLoop, int fd, int mask) {
    peApiState *state = eventLoop->apidata;

    if (mask & PE_READABLE) FD_CLR(fd,&state->rfds);
    if (mask & PE_WRITABLE) FD_CLR(fd,&state->wfds);
}

static int peApiPoll(peEventLoop *eventLoop, struct timeval *tvp) {
    peApiState *state = eventLoop->apidata;
    int retval, j, numevents = 0;

    memcpy(&state->_rfds,&state->rfds,sizeof(fd_set));
    memcpy(&state->_wfds,&state->wfds,sizeof(fd_set));

    retval = select(eventLoop->maxfd+1,
                    &state->_rfds,&state->_wfds,NULL,tvp);
    if (retval > 0) {
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            peFileEvent *fe = &eventLoop->events[j];

            if (fe->mask == PE_NONE) continue;
            if (fe->mask & PE_READABLE && FD_ISSET(j,&state->_rfds))
                mask |= PE_READABLE;
            if (fe->mask & PE_WRITABLE && FD_ISSET(j,&state->_wfds))
                mask |= PE_WRITABLE;
            eventLoop->fired[numevents].fd = j;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

static char *peApiName(void) {
    return "select";
}
