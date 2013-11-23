#ifndef __CONFIG_H__
#define __CONFIG_H__


/* For proc filesystem */
#ifdef __linux__
#define HAVE_PROC_STAT 1
#define HAVE_PROC_MAPS 1
#define HAVE_PROC_SMAPS 1
#endif

/* For polling API */
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#endif










