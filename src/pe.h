#ifndef __PE_H__
#define __PE_H__

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#include "pmalloc.h"

/* Event execute status */
#define PE_OK     0
#define PE_ERR   -1

/* FileEvent status */
#define PE_NONE      0
#define PE_READABLE  1
#define PE_WRITABLE  2

/* TimeEvent Process' Execute  */
#define PE_FILE_EVENTS  1
#define PE_TIME_EVENTS  2
#define PE_ALL_EVENTS   (PE_FILE_EVENTS|PE_TIME_EVENTS)
#define PE_DONT_WAIT    4

/* Decide to Continue to perform Time Event */
#define PE_NOMORE   -1

#define PE_NOTUSED(V) ((void) V)

/* Event Process Status */
struct peEventLoop;

/* Types and data structures */
typedef void peFileProc(struct peEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int  peTimeProc(struct peEventLoop *eventLoop, long long id, void *clientData);
typedef void peEventFinalizerProc(struct peEventLoop *eventLoop, void *clientData);
typedef void peBeforeSleepProc(struct peEventLoop *eventLoop);

/* File event structure */
typedef struct peFileEvent {
    int mask; /* one of PE_(READABLE|WRITABLE) */

    peFileProc *rfileProc;
    peFileProc *wfileProc;
   
    void *clientData;
} peFileEvent;

/* Time event structure */
typedef struct peTimeEvent {

    long long id; /* time event identifier. */

    long when_sec; /* seconds */
    long when_ms; /* milliseconds */

    peTimeProc *timeProc;

    peEventFinalizerProc *finalizerProc;

    void *clientData;

    struct peTimeEvent *next;

} peTimeEvent;

/* A fired event */
typedef struct peFiredEvent {
    int fd;
    int mask;
} peFiredEvent;

/* State of an event based program */
typedef struct peEventLoop {
    int maxfd;   /* highest file descriptor currently registered */

    int setsize; /* max number of file descriptors tracked */

    long long timeEventNextId;

    time_t lastTime;     /* Used to detect system clock skew */

    peFileEvent *events; /* Registered events */

    peFiredEvent *fired; /* Fired events */

    peTimeEvent *timeEventHead;

    int stop;

    void *apidata; /* This is used for polling API specific data */

    peBeforeSleepProc *beforesleep;
} peEventLoop;


peEventLoop *peCreateEventLoop(int setsize);
void   peDeleteEventLoop(peEventLoop *eventLoop);
void   peStop(peEventLoop *eventLoop);
int    peCreateFileEvent(peEventLoop *eventLoop, int fd, int mask,
                         peFileProc *proc, void *clientData);
void   peDeleteFileEvent(peEventLoop *eventLoop, int fd, int mask);
int    peGetFileEvents(peEventLoop *eventLoop, int fd);
long long peCreateTimeEvent(peEventLoop *eventLoop, long long milliseconds,
                            peTimeProc *proc, void *clientData, peEventFinalizerProc *finalizerProc);
int    peDeleteTimeEvent(peEventLoop *eventLoop, long long id);
int    peProcessEvents(peEventLoop *eventLoop, int flags);
int    peWait(int fd, int mask, long long milliseconds);
void   peMain(peEventLoop *eventLoop);
char  *peGetApiName(void);
void   peSetBeforeSleepProc(peEventLoop *eventLoop, peBeforeSleepProc *beforesleep);

#endif
















