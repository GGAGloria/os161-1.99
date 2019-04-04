/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *  The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
 #include <sfs.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
 #include <coremap.h>
#include "opt-A3.h"
 #include <syscall.h>


/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
static volatile bool flag = false;
static volatile unsigned long size = 0;
static struct entry *c = NULL;
paddr_t lo = 0;
paddr_t hi = 0;

void coremap_init(void){
    
    ram_getsize(&lo,&hi);
    spinlock_acquire(&coremap_lock);
    unsigned long totalpage = (hi - lo) / (PAGE_SIZE)-1;
    unsigned long coremapsize = sizeof(struct entry)*totalpage;
    unsigned long pageforcoremap = (coremapsize+PAGE_SIZE-1) / PAGE_SIZE;
    size = totalpage - pageforcoremap;
    size_t startaddr = SFS_ROUNDUP((lo + coremapsize), PAGE_SIZE); //First page address  
    c = (struct entry*)PADDR_TO_KVADDR(lo);
    for (unsigned long i = 0; i<size; i++){
        c[i].use = 0;
        c[i].owner = 0;
        c[i].page_addr = startaddr+ i*PAGE_SIZE;
    }
    // DEBUG(DB_VM, "5");
    //count how many are used now
    // DEBUG(DB_VM, "lo before: %lu\n", (unsigned long)lo);
    // DEBUG(DB_VM, "hi before: %lu\n", (unsigned long)hi);
    ram_getsize(&lo, &hi);
    ram_set_to_zero();
    // DEBUG(DB_VM, "lo: %lu\n", (unsigned long)lo);
    // DEBUG(DB_VM, "hi: %lu\n", (unsigned long)hi);
    // DEBUG(DB_VM, "6");
    spinlock_release(&coremap_lock);
    // DEBUG(DB_VM, "7");
}

void
vm_bootstrap(void)
{    
    DEBUG(DB_VM, "Let's bootstrap!\n");

#if OPT_A3
    //In vm_bootstrap, call ram_getsize to get the remaining physical memory in the system. 
    // DEBUG(DB_VM, "Hey are u here????????");
    // DEBUG(DB_VM, "Or here????????");
    ram_getsize(&lo,&hi);
    spinlock_acquire(&coremap_lock);
    unsigned long totalpage = (hi - lo) / (PAGE_SIZE)-1;
    unsigned long coremapsize = sizeof(struct entry)*totalpage;
    unsigned long pageforcoremap = (coremapsize+PAGE_SIZE-1) / PAGE_SIZE;
    size = totalpage - pageforcoremap;
    size_t startaddr = SFS_ROUNDUP((lo + coremapsize), PAGE_SIZE); //First page address  
    c = (struct entry*)PADDR_TO_KVADDR(lo);
    for (unsigned long i = 0; i<size; i++){
        c[i].use = 0;
        c[i].owner = 0;
        c[i].page_addr = startaddr+ i*PAGE_SIZE;
    }
    // DEBUG(DB_VM, "5");
    //count how many are used now
    // DEBUG(DB_VM, "lo before: %lu\n", (unsigned long)lo);
    // DEBUG(DB_VM, "hi before: %lu\n", (unsigned long)hi);
    ram_getsize(&lo, &hi);
    ram_set_to_zero();
    // DEBUG(DB_VM, "lo: %lu\n", (unsigned long)lo);
    // DEBUG(DB_VM, "hi: %lu\n", (unsigned long)hi);
    // DEBUG(DB_VM, "6");
    spinlock_release(&coremap_lock);
    // DEBUG(DB_VM, "7");
    DEBUG(DB_VM, "Bootstrap finished!\n");
    flag = true;




 
#endif
    /* Do nothing. */
}

void set_owner(paddr_t addr,unsigned long start, unsigned long end){
    for (unsigned long i =start; i<=end; i++){
        c[i].owner = addr;
        c[i].use = 1;
    }
}
static
paddr_t
getppages(unsigned long npages)
{
    paddr_t addr = 0;
    if (!flag){
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);  
        spinlock_release(&stealmem_lock);
        return addr;
    } else {
        spinlock_acquire(&coremap_lock);
        unsigned int count = 0;
        for (unsigned long i=0; i<size; i++){
            if (c[i].use == 0){
                for (unsigned long j=i; j<size; j++){
                    if (c[j].use == 0) count++;
                    if (count == npages){
                        // DEBUG(DB_VM, "c[1] before getppages: %lu\n", (unsigned long)c[1].page_addr);
                        addr = c[i].page_addr;
                        // DEBUG(DB_VM, "I get the slot: %lu\n", i);
                        // DEBUG(DB_VM, "The finish slot: %lu\n", j);
                        set_owner(c[i].page_addr,i,j);
                        spinlock_release(&coremap_lock);
                        // DEBUG(DB_VM, "npages: %lu\n", npages);
                        // DEBUG(DB_VM, "addr: %lu\n", (unsigned long)addr);
                        // DEBUG(DB_VM, "c[1] after getppages: %lu\n", (unsigned long)c[1].page_addr);
                        return addr;
                    }
                    if (c[j].use == 1){
                        break;
                    }
                }
                count = 0;
            }
        }
        spinlock_release(&coremap_lock);
        return addr;
    } 
}


/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{

    paddr_t pa;
    pa = getppages(npages);
    DEBUG(DB_VM,"pa: %lu\n",(unsigned long)pa);
    if (pa==0) {
        return 0;
    }
    return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
    DEBUG(DB_VM, "I am trying to free\n");
    paddr_t paddr = KVADDR_TO_PADDR(addr);
    spinlock_acquire(&coremap_lock);
    for (unsigned long i=0; i<size; i++){
        if (c[i].page_addr == paddr){
            if (c[i].owner == paddr){
                for (unsigned long j=i; j<size; j++){
                    if (c[j].owner == paddr){
                        DEBUG(DB_VM, "I free the slot: %lu\n", j);
                        c[j].owner = 0;
                        c[j].use = 0;
                    } else{
                        break;
                    }
                }
                spinlock_release(&coremap_lock);
                return;
            } else {
                break;
            }
        }
    }
    spinlock_release(&coremap_lock);
    return;
}


void
vm_tlbshootdown_all(void)
{
    panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
    paddr_t paddr;
    int i;
    uint32_t ehi, elo;
    struct addrspace *as;
    int spl;
    // bool miss = false;
    bool readonly = false;

    faultaddress &= PAGE_FRAME;

    DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

    switch (faulttype) {
        case VM_FAULT_READONLY:
            sys__exit(1);
        /* We always create pages read-write, so we can't get this */
        //panic("dumbvm: got VM_FAULT_READONLY\n");
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
        break;
        default:
        return EINVAL;
    }

    if (curproc == NULL) {
        /*
         * No process. This is probably a kernel fault early
         * in boot. Return EFAULT so as to panic instead of
         * getting into an infinite faulting loop.
         */
        return EFAULT;
    }

    as = curproc_getas();
    if (as == NULL) {
        /*
         * No address space set up. This is probably also a
         * kernel fault early in boot.
         */
        return EFAULT;
    }

    /* Assert that the address space has been set up properly. */
    KASSERT(as->as_vbase1 != 0);
    KASSERT(as->as_pbase1 != 0);
    KASSERT(as->as_npages1 != 0);
    KASSERT(as->as_vbase2 != 0);
    KASSERT(as->as_pbase2 != 0);
    KASSERT(as->as_npages2 != 0);
    KASSERT(as->as_stackpbase != 0);
    KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
    KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
    KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
    KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
    KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

    vbase1 = as->as_vbase1;
    vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
    vbase2 = as->as_vbase2;
    vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
    stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
    stacktop = USERSTACK;
    // vaddr_t page;
    if (faultaddress >= vbase1 && faultaddress < vtop1) {
        paddr = (faultaddress - vbase1) + as->as_pbase1;
        readonly = true;
    }
    else if (faultaddress >= vbase2 && faultaddress < vtop2) {
        paddr = (faultaddress - vbase2) + as->as_pbase2;
    }
    else if (faultaddress >= stackbase && faultaddress < stacktop) {
        paddr = (faultaddress - stackbase) + as->as_stackpbase;
    }
    else {
        // DEBUG(DB_VM, "why here\n");
        return EFAULT;
    }

    /* make sure it's page-aligned */
    KASSERT((paddr & PAGE_FRAME) == paddr);

    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();

    // miss = true;

    for (i=0; i<NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if (elo & TLBLO_VALID) {
            continue;
        }
        ehi = faultaddress;

        if (readonly == false || as->loaded == false) elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
        else elo = paddr | TLBLO_VALID;
        DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
        tlb_write(ehi, elo, i);
        // miss = false;
        splx(spl);
        return 0;
    }
    // DEBUG(DB_VM, "-------------after for---------------\n");
#if OPT_A3
    // if(miss == true){
    ehi = faultaddress;
    if (readonly == false || as->loaded == false) elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
    else elo = paddr | TLBLO_VALID;
    tlb_random(ehi, elo);
    splx(spl);
    return 0;
    // }
        
#endif
    //if not go into opt_a3, panic
    kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
    splx(spl);
    return EFAULT;
}



struct addrspace *
as_create(void)
{
    struct addrspace *as = kmalloc(sizeof(struct addrspace));
    if (as==NULL) {
        return NULL;
    }

    as->as_vbase1 = 0;
    as->as_pbase1 = 0;
    as->as_npages1 = 0;
    as->as_vbase2 = 0;
    as->as_pbase2 = 0;
    as->as_npages2 = 0;
    as->as_stackpbase = 0;
    // as->as_ptable1 = NULL;
    // as->as_ptable2 = NULL;
    // as->as_stackptable = NULL;
    as->loaded = false;
    return as;
}



void
as_destroy(struct addrspace *as)
{
    // free_pagetable(as->as_stackptable, DUMBVM_STACKPAGES);
    // free_pagetable(as->as_ptable1, as->as_npages1);
    // free_pagetable(as->as_ptable2, as->as_npages2);
    free_kpages(PADDR_TO_KVADDR(as->as_pbase1));
    free_kpages(PADDR_TO_KVADDR(as->as_pbase2));
    free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
    kfree(as);
    DEBUG(DB_VM, "addrspace freed\n");

}

void
as_activate(void)
{
    int i, spl;
    struct addrspace *as;

    as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
    if (as == NULL) {
        return;
    }

    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();

    for (i=0; i<NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    splx(spl);
}

void
as_deactivate(void)
{
    /* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
         int readable, int writeable, int executable)
{
    size_t npages; 

    /* Align the region. First, the base... */
    sz += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;

    /* ...and now the length. */
    sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

    npages = sz / PAGE_SIZE;

    /* We don't use these - all pages are read-write */
    (void)readable;
    (void)writeable;
    (void)executable;

    if (as->as_vbase1 == 0) {
        as->as_vbase1 = vaddr;
        as->as_npages1 = npages;
        return 0;
    }

    if (as->as_vbase2 == 0) {
        as->as_vbase2 = vaddr;
        as->as_npages2 = npages;
        return 0;
    }

    /*
     * Support for more than two regions is not available.
     */
    kprintf("dumbvm: Warning: too many regions\n");
    return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
    bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}


int
as_prepare_load(struct addrspace *as)
{
    KASSERT(as->as_pbase1 == 0);
    KASSERT(as->as_pbase2 == 0);
    KASSERT(as->as_stackpbase == 0);
    // as->as_ptable1 = kmalloc(as->as_npages1* sizeof(paddr_t));
    // DEBUG(DB_VM, "Finished ptable1\n");
    // as->as_ptable2 = kmalloc(as->as_npages2* sizeof(paddr_t));
    // DEBUG(DB_VM, "Finished ptable2\n");
    // as->as_stackptable = kmalloc(DUMBVM_STACKPAGES* sizeof(paddr_t));
    // prepare(as->as_ptable1, as->as_npages1);
    // prepare(as->as_ptable2, as->as_npages2);
    // prepare(as->as_stackptable, DUMBVM_STACKPAGES);
    // DEBUG(DB_VM, "c[1] before getppages: %lu\n", (unsigned long)c[1].page_addr);
    as->as_pbase1 = getppages(as->as_npages1);
    //DEBUG(DB_VM, "c[1] after getppages: %lu\n", (unsigned long)c[1].page_addr);
    if (as->as_pbase1 == 0) {
        return ENOMEM;
    }

    as->as_pbase2 = getppages(as->as_npages2);
    if (as->as_pbase2 == 0) {
        return ENOMEM;
    }

    as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
    // DEBUG(DB_VM, "after last getppages\n");
    if (as->as_stackpbase == 0) {
        return ENOMEM;
    }
    // DEBUG(DB_VM, "stackpbase not equal to 0\n");
    //DEBUG(DB_VM, "c[1]: %lu\n", (unsigned long)c[1].page_addr);
    // DEBUG(DB_VM, "as_pbase1: %lu\n", (unsigned long)as->as_pbase1);
    as_zero_region(as->as_pbase1, as->as_npages1);
    // DEBUG(DB_VM, "ptable1 equal 0\n");
    as_zero_region(as->as_pbase2, as->as_npages2);
    // DEBUG(DB_VM, "ptable2 equal 0\n");
    as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);
    // DEBUG(DB_VM, "stacktable equal 0\n");

    return 0;
}

int
as_complete_load(struct addrspace *as)
{
    as->loaded = true;
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    // KASSERT(as->as_stackptable != NULL);
    KASSERT(as->as_stackpbase != 0);
    *stackptr = USERSTACK;
    return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *new;

    new = as_create();
    if (new==NULL) {
        return ENOMEM;
    }

    new->as_vbase1 = old->as_vbase1;
    new->as_npages1 = old->as_npages1;
    new->as_vbase2 = old->as_vbase2;
    new->as_npages2 = old->as_npages2;

    /* (Mis)use as_prepare_load to allocate some physical memory. */
    if (as_prepare_load(new)) {
        as_destroy(new);
        return ENOMEM;
    }

    KASSERT(new->as_pbase1 != 0);
    KASSERT(new->as_pbase2 != 0);
    KASSERT(new->as_stackpbase != 0);
    // KASSERT(new->as_ptable1 != NULL);
    // KASSERT(new->as_ptable2 != NULL);
    // KASSERT(new->as_stackptable != NULL);

    memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
        (const void *)PADDR_TO_KVADDR(old->as_pbase1),
        old->as_npages1*PAGE_SIZE);

    memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
        (const void *)PADDR_TO_KVADDR(old->as_pbase2),
        old->as_npages2*PAGE_SIZE);

    memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
        (const void *)PADDR_TO_KVADDR(old->as_stackpbase),
        DUMBVM_STACKPAGES*PAGE_SIZE);
    // for(size_t i = 0; i < old->as_npages1; i++){
    //     memmove((void *)PADDR_TO_KVADDR(new->as_ptable1[i]),(const void *)PADDR_TO_KVADDR(old->as_ptable1[i]),PAGE_SIZE);
    // }

    // for(size_t i = 0; i < old->as_npages2; ++i){
    //     memmove((void *)PADDR_TO_KVADDR(new->as_ptable2[i]),(const void *)PADDR_TO_KVADDR(old->as_ptable2[i]),PAGE_SIZE);
    // }

    // for(size_t i = 0; i < DUMBVM_STACKPAGES; ++i){
    //     memmove((void *)PADDR_TO_KVADDR(new->as_stackptable[i]),(const void *)PADDR_TO_KVADDR(old->as_stackptable[i]),PAGE_SIZE);
    // }

    *ret = new;
    return 0;
}
