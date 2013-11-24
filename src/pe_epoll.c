#include <sys/epoll.h>
#include <unistd.h>
#include "pe.h"

typedef struct peApiState {
    int epfd;
    struct epoll_event *events;
} peApiState;

static int 
peApiCreate(peEventLoop *eventLoop) {
    peApiState *state = pmalloc(sizeof(peApiState));

    if (!state) return -1;
    state->events = pmalloc(sizeof(struct epoll_event)*eventLoop->setsize);
    if (!state->events) {
        pfree(state);
        return -1;
    }
    state->epfd = epoll_create(1024); /* 1024 is just an hint for the kernel */
    if (state->epfd == -1) {
        pfree(state->events);
        pfree(state);
        return -1;
    }
    eventLoop->apidata = state;
    return 0;
}

static void 
peApiFree(peEventLoop *eventLoop) {
    peApiState *state = eventLoop->apidata;

    close(state->epfd);
    pfree(state->events);
    pfree(state);
}

static int 
peApiAddEvent(peEventLoop *eventLoop, int fd, int mask) {
    peApiState *state = eventLoop->apidata;
    struct epoll_event ee;
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    int op = eventLoop->events[fd].mask == PE_NONE ?
        EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= eventLoop->events[fd].mask; /* Merge old events */
    if (mask & PE_READABLE) ee.events |= EPOLLIN;
    if (mask & PE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    return 0;
}

static void 
peApiDelEvent(peEventLoop *eventLoop, int fd, int delmask) {
    peApiState *state = eventLoop->apidata;
    struct epoll_event ee;
    int mask = eventLoop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & PE_READABLE) ee.events |= EPOLLIN;
    if (mask & PE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (mask != PE_NONE) {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}

static int 
peApiPoll(peEventLoop *eventLoop, struct timeval *tvp) {
    peApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd,state->events,eventLoop->setsize,
                        tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN)  mask |= PE_READABLE;
            if (e->events & EPOLLOUT) mask |= PE_WRITABLE;
            if (e->events & EPOLLERR) mask |= PE_WRITABLE;
            if (e->events & EPOLLHUP) mask |= PE_WRITABLE;
            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    }
    return numevents;
}

static char *
peApiName(void) {
    return "epoll";
}
