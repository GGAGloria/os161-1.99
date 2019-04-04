#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <spinlock.h>

struct entry
{
    paddr_t page_addr;
    paddr_t owner;
    volatile bool use;
};
void coremap_init(void);
void set_owner(paddr_t addr,unsigned long start, unsigned long end);

#endif 
