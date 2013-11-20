#ifndef _PE_H_
#define _PE_H_

#include <time.h>

/* Event execute status */
#define PE_OK     0
#define PE_ERR   -1

/* FileEvent status */
#define PE_NONE      0
#define PE_READABLE  1
#define PE_WRITABLE  2

/* Time Process' Execute  */
#define PE_FILE_EVENTS  1
#define PE_TIME_EVENTS  2
#define PE_ALL_EVENTS   (PE_FILE_EVENTS|PE_TIME_EVENTS)
#define PE_DONT_WAIT    4

/* Decide to Continue to perform Time Event */
#define PE_NOMORE   -1

/* Macros */
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
typedef struct aeEventLoop {
    int maxfd;   /* highest file descriptor currently registered */

    int setsize; /* max number of file descriptors tracked */

    long long timeEventNextId;

    time_t lastTime;     /* Used to detect system clock skew */

    peFileEvent *events; /* Registered events */

    peFiredEvent *fired; /* Fired events */

    peTimeEvent *timeEventHead;

    int stop;

    void *apidata; /* This is used for polling API specific data */
    // 在处理事件前要执行的函数
    peBeforeSleepProc *beforesleep;
} peEventLoop;

/* Prototypes */
peEventLoop *aeCreateEventLoop(int setsize);
void aeDeleteEventLoop(peEventLoop *eventLoop);
void aeStop(peEventLoop *eventLoop);
int aeCreateFileEvent(peEventLoop *eventLoop, int fd, int mask,
                      peFileProc *proc, void *clientData);
void aeDeleteFileEvent(peEventLoop *eventLoop, int fd, int mask);
int aeGetFileEvents(peEventLoop *eventLoop, int fd);
long long aeCreateTimeEvent(peEventLoop *eventLoop, long long milliseconds,
                            peTimeProc *proc, void *clientData,
                            peEventFinalizerProc *finalizerProc);
int aeDeleteTimeEvent(peEventLoop *eventLoop, long long id);
int aeProcessEvents(peEventLoop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(peEventLoop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(peEventLoop *eventLoop, peBeforeSleepProc *beforesleep);

#endif










