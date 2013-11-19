#include "pmalloc.h"


#define PREFIX_SIZE (sizeof(size_t))

#define update_pmalloc_stat_add(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
    } while(0)

#define update_pmalloc_stat_sub(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
    } while(0)

#define update_pmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (pmalloc_thread_safe) { \
    update_pmalloc_stat_add(_n); \
    } else { \
    used_memory += _n; \
    } \
    } while(0)

#define update_pmalloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (pmalloc_thread_safe) { \
    update_pmalloc_stat_sub(_n); \
    } else { \
    used_memory -= _n; \
    } \
    } while(0)

void
plibc_free(void *ptr){
    free(ptr);
}

static size_t used_memory = 0;
static int pmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
pmalloc_default_oom(size_t size){
    fprintf(stderr , "pmalloc : Out of memory trying to allocate %zu bytes\n", size);
    fflush(stderr);
    abort();
}

static void (*pmalloc_oom_handler)(size_t) = pmalloc_default_oom;

void *
pmalloc(size_t size){
    void *ptr = malloc(size + PREFIX_SIZE);
    
    if(!ptr) pmalloc_oom_handler(size);
    *((size_t*)ptr) = size;
    update_pmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char *)ptr + PREFIX_SIZE;
}

void *
pcalloc(size_t size){
    void *ptr = calloc(1 , size + PREFIX_SIZE);

    if(!ptr) pmalloc_oom_handler(size);
    *((size_t*)ptr) = size;
    update_pmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char *)ptr + PREFIX_SIZE;
}

void *
prealloc(void *ptr , size_t size){
    size_t oldsize;
    void
        *realptr,
        *newptr;
    
    if(ptr == NULL) return pmalloc(size);
    realptr = (char *)ptr - PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    newptr  = realloc(realptr , size+PREFIX_SIZE);
    if(!newptr) pmalloc_oom_handler(size);
    
    *((size_t*) newptr) = size;
    update_pmalloc_stat_free(oldsize);
    update_pmalloc_stat_alloc(size);
    return (char *)newptr + PREFIX_SIZE;
}

size_t
pmalloc_size(void *ptr){
    void *realptr = (char *)ptr-PREFIX_SIZE;
    size_t size = *((size_t*)realptr);

    if(size&(sizeof(long)-1)) size+=sizeof(long)-(size&(sizeof(long)-1));
    return size+PREFIX_SIZE;
}

void
pfree(void *ptr){
    void * realptr;
    size_t oldsize;
    
    if(ptr == NULL) return;
    realptr = (char *)ptr - PREFIX_SIZE;
    oldsize = *((size_t*) realptr);
    update_pmalloc_stat_free(oldsize+PREFIX_SIZE);
    free(realptr);
}

char *
pstrdup(const char *s){
    size_t l = strlen(s) + 1;
    char *p = pmalloc(l);
    memcpy(p,s,l);
    return p;
}

size_t
pmalloc_used_memory(void){
    size_t um;
    if(pmalloc_thread_safe){
        pthread_mutex_lock(&used_memory_mutex);
        um = used_memory;
        pthread_mutex_unlock(&used_memory_mutex);
    }else {
        um = used_memory;
    }
    return um;
}

void
pmalloc_enable_thread_safeness(void){
    pmalloc_thread_safe = 1;
}

void
pmalloc_set_oom_handler(void (*oom_handler)(size_t)){
    pmalloc_oom_handler = oom_handler;
}

#if defined(HAVE_PROC_STAT)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
size_t 
pmalloc_get_rss(void){  /*得到占用系统内存总量*/
    int             /*获取runtime的信息，获取byte为单位的page大小*/
        page = sysconf(_SC_PAGESIZE),
        fd,
        count;
    size_t rss;
    char 
        buf[4096],
        filename[256],
        *p,
        *x;
    snprintf(filename , 256 , "/proc/%d/stat" , getpid());
    if((fd = open(filename , O_RDONLY)) == -1) return 0;
    if(read(fd , buf , 4096) <= 0){
        close(fd);
        return 0;
    }
    close(fd);

    p = buf;
    count = 23;
    while(p && count--){
        p = strchr(p , ' ');
        if(p) p++;
    }
    if(!p) return 0;
    x = strchr(p , ' ');
    if(!x) return 0;
    *x = '\0';
    
    rss = strtoll(p , NULL , 10);
    rss *= page;
    return rss;
}
#else
size_t
pmalloc_get_rss(void){
    return pmalloc_used_memory();
}
#endif

float    /* 碎片率Fragmentation = RSS / allocated-bytes */
pmalloc_get_fragmentation_ratio(void){
    return (float)pmalloc_get_rss()/pmalloc_used_memory();
}

#if defined(HAVE_PROC_SMAPS)
size_t
pmalloc_get_private_dirty(void){
    char line[1024];
    size_t pd = 0;
    FILE *fp = fopen("/proc/self/smaps" , "r");

    if(!fp) return 0;
    while(fgets(line , sizeof(line) , fp) != NULL){
        if(strncmp(line , "Private_Dirty:" , 14) == 0){
            char *p = strchr(line , 'k');
            if(p){
                *p = '\0';
                pd += strtol(line + 14 , NULL , 10) * 1024;
            }
        }
    }
    fclose(fp);
    return pd;
}
#else
size_t
pmalloc_get_private_dirty(void){
    return 0;
}
#endif



















