#include <stdio.h>
#include <stdlib.h>
#include "pe.h"
#include "pmalloc.h"

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

int
main(int argv , char * args[])
{
    if(argv > 1){
        void (*fun[])(void) = {
            pmalloc_test
        };
        putestInitWithFuncs(fun ,(int) *args[1]);
    }
    return 0;
}

















