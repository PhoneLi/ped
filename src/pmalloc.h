#ifndef __PMALLOC_H__
#define __PMALLOC_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "config.h"

#define __xstr(s) __str(s)
#define __str(s)  #s

#ifndef ZMALLOC_LIB
#define ZMALLOC_LIB "libc"
#endif

void   *pmalloc(size_t size);
void   *pcalloc(size_t size);
void   *prealloc(void *ptr, size_t size);
void    pfree(void *ptr);
char   *pstrdup(const char *s);
size_t  pmalloc_size(void *ptr);
size_t  pmalloc_used_memory(void);
void    pmalloc_enable_thread_safeness(void);
void    pmalloc_set_oom_handler(void (*oom_handler)(size_t));
float   pmalloc_get_fragmentation_ratio(void);
size_t  pmalloc_get_rss(void);
size_t  pmalloc_get_private_dirty(void);

#endif


















