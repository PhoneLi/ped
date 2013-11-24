#include <stdio.h>
#include <stdlib.h>
#include "pe.h"

#define NOT_USED(p) ((void)p)

void
putestInitWithFuncs(void (**_fun)(void) , int index){
    _fun[index - 48]();
    return ;
}

void
pmalloc_test(){
    pmalloc_enable_thread_safeness();
    printf("char : %zu \nsize : %zu \n" , sizeof(char) , sizeof(size_t));

    char * dm = (char *)pmalloc(sizeof(char) * 512);
    strcpy(dm , "hello world. - pmalloc");
    printf("dm : %s \n" , dm);
    pfree(dm);
    
    dm  = (char *)pcalloc(sizeof(char) * 512);
    strcpy(dm , "hello world. - pcalloc.");
    printf("dm : %s \n" , dm);
    pfree(dm);
    
    dm  = (char *)pmalloc(sizeof(char) * 10);
    printf("dm size : %zu \n" , pmalloc_size(dm));
    dm =  prealloc(dm , sizeof(char) * 20);
    printf("dm size : %zu \n" , pmalloc_size(dm));
    pfree(dm);

    char * pl = pstrdup("hello w");
    printf("pl : %s \n" , pl);
    printf(" userd memory : %zu\n framentation_ratio : %lf\n rss : %zu\n private_dirty : %zu\n" , 
               pmalloc_used_memory() , 
               pmalloc_get_fragmentation_ratio() ,
               pmalloc_get_rss() ,
               pmalloc_get_private_dirty()
           );
    pfree(pl);
    
}

/* ped test ========================================*/
#define DATA_STR 50

void
fun_peBeforesleepProc(struct peEventLoop *el){
    NOT_USED(el);
    printf("peBeforeSleepProc runned!!!\n");
}

void
fun_peFinalizerProc(struct peEventLoop *el , void * clientData){
    NOT_USED(el);
    NOT_USED(clientData);
    printf("peFinalizerProc runned!!!\n");
}

void
file_cb(struct peEventLoop *loop , int fd , void *clientData , int mask){
    char buf[DATA_STR + 1] = {0};
    read(fd , buf , DATA_STR + 1);
    printf("file_cb : [eventloop : %p] , [fd : %d] , [data : %s] , [mask : %d]\n" , loop , fd , (char *)clientData  , mask);
    printf("buf : %s \n" , buf);

    if(strncmp(buf , "quit" , 4) == 0)
        loop->stop = 1;
}

int
time_cb(struct peEventLoop *loop , long long id , void *clientData){
    printf("time_cb : [eventloop : %p] , [id : %lld] , [data : %p]\n" , loop , id , clientData);
    return PE_NOMORE;
}

void
TimeEvent_test(void){
    const char * msg = "ped say :\" hello.\"";
    char *user_data = calloc(DATA_STR , sizeof(char));
    if(!user_data)
        return;
    memcpy(user_data , msg , strlen(msg));
   
    peEventLoop *loop = peCreateEventLoop(1000);
    peSetBeforeSleepProc(loop  ,fun_peBeforesleepProc);
    int res;
    NOT_USED(res);
    res = peCreateFileEvent(loop , STDIN_FILENO , PE_READABLE , file_cb , user_data);
    res = peCreateTimeEvent(loop , 5 * 1000 , time_cb , NULL, fun_peFinalizerProc);

    peMain(loop);
    /*
    peProcessEvents(loop , PE_ALL_EVENTS);
    */
    peDeleteEventLoop(loop);
    free(user_data);
    return;
}
/* ped test ========================================End*/

int
main(int argv , char * args[])
{
    if(argv > 1){
        void (*fun[])(void) = {
            pmalloc_test,
            TimeEvent_test
        };
        putestInitWithFuncs(fun ,(int) *args[1]);
    }
    return 0;
}

















