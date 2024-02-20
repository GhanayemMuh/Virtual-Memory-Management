#include <unistd.h>
#include <stdio.h>
#include <math.h>

#define maxAlloc 100000000 // 10^8

void* smalloc(size_t size)
{
    if(size > maxAlloc || size ==0)
        return NULL;
    
    void* allocate = sbrk(size);
    if(allocate == (void*)-1)
        return NULL;
    
    return allocate;
}

