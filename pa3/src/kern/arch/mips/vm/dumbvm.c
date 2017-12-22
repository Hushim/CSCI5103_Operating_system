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
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>
#include <array.h>
/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 *
 * NOTE: it's been found over the years that students often begin on
 * the VM assignment by copying dumbvm.c and trying to improve it.
 * This is not recommended. dumbvm is (more or less intentionally) not
 * a good design reference. The first recommendation would be: do not
 * look at dumbvm at all. The second recommendation would be: if you
 * do, be sure to review it from the perspective of comparing it to
 * what a VM system is supposed to do, and understanding what corners
 * it's cutting (there are many) and why, and more importantly, how.
 */

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */

/*
 * Wrap ram_stealmem in a spinlock.
 */

static struct  spinlock stealmem_lock = SPINLOCK_INITIALIZER;

//struct spinlock sw_lock = SPINLOCK_INITIALIZER;


void
vm_bootstrap(void)
{
 // spinlock_init(&stealmem_lock);
  coremap_initialize();
  sw_init();
}

/*
 * Check if we're in a context that can sleep. While most of the
 * operations in dumbvm don't in fact sleep, in a real VM system many
 * of them would. In those, assert that sleeping is ok. This helps
 * avoid the situation where syscall-layer code that works ok with
 * dumbvm starts blowing up during the VM assignment.
 */

static
paddr_t
getppages(unsigned long npages)
{
  paddr_t addr;
    spinlock_acquire(&stealmem_lock);
    if (coremap_ready_flag == 0) {

    addr = ram_stealmem(npages);

    spinlock_release(&stealmem_lock);
      return addr;
    } else {
      addr = allocate_pages_by_coremap(NULL, 1, npages);
      spinlock_release(&stealmem_lock);
      return addr;
    }
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
  paddr_t pa;

  pa = getppages(npages);
  if (pa==0) {
    return 0;
  }
  return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
  spinlock_acquire(&stealmem_lock);
  paddr_t paddr = KVADDR_TO_PADDR(addr);

 // kprintf("the vaddr is to free is : %p", (void *)addr);
  free_ppges(paddr);
  spinlock_release(&stealmem_lock);
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
  vaddr_t *l1_vaddr, *l2_vaddr, vaddr;
  vaddr_t vbase, vtop;
  vaddr_t tmp = 0;
  paddr_t paddr;
  struct addrspace *as;

  uint32_t ehi, elo;
  int i, l1_index, l2_index, spl;

  switch (faulttype) {
    case VM_FAULT_READONLY:
      return EFAULT;
    case VM_FAULT_READ:
    case VM_FAULT_WRITE:
      break;
    default:
      return EINVAL;
  }

  as = curproc -> p_addrspace;
  if (as == NULL) {
    return EFAULT;
  }

  //if(sw_exist(curproc->t_pid, faultaddress) ==-1) {

//    faultaddress = sw_one_pagein(faultaddress, curproc->t_pid);
//  }
  faultaddress &= PAGE_FRAME;

  l1_index = (faultaddress & LEVEL_ONE_MASK) >> 22;
  l2_index = (faultaddress & LEVEL_TWO_MASK) >> 12;

  int pgs_size = (int)array_num(&as->pages);
  for(int i = 0; i < pgs_size; i++) {
    struct page * pg = array_get(&as->pages, i);

    KASSERT(pg->as_vbase != 0);
    KASSERT(pg->as_npages != 0);
    KASSERT((pg->as_vbase & PAGE_FRAME) == pg->as_vbase);

    vbase = pg->as_vbase;
    vtop = vbase + pg->as_npages * PAGE_SIZE;

    if(faultaddress >= vbase && faultaddress < vtop) {
      tmp = faultaddress;
      break;
    }
  }

  if (tmp == 0) {
    vtop = USERSTACK;
    vbase = vtop - STACK_PG_SIZE * PAGE_SIZE;
    if (faultaddress >= vbase && faultaddress < vtop) {
      tmp = faultaddress;
    }

    if (tmp == 0) {
      return EFAULT;
    }
  }


  l1_index *= 4;
  l1_vaddr = (vaddr_t *)(as->as_first_level_base + l1_index);
  if (!(*l1_vaddr)) {
    *l1_vaddr = alloc_kpages(1);
    KASSERT(*l1_vaddr != 0);
    as_zero_region(*l1_vaddr, 1);

    l2_index *= 4;
    l2_vaddr = (vaddr_t *)(*l1_vaddr + l2_index);
    vaddr = alloc_kpages(1);
    KASSERT(vaddr != 0);
    as_zero_region(vaddr, 1);
    *l2_vaddr = vaddr ;

    paddr = KVADDR_TO_PADDR(vaddr);

  }

  else {

      l2_vaddr = (vaddr_t *)(*l1_vaddr + l2_index * 4);
      if (*l2_vaddr ) {
        vaddr = *l2_vaddr;
        paddr = KVADDR_TO_PADDR(vaddr);
      }

      else {
        vaddr = alloc_kpages(1);

        as_zero_region(vaddr, 1);
        *l2_vaddr = vaddr;

        paddr = KVADDR_TO_PADDR(vaddr);
      }
  }

  spl = splhigh();


  for (i=0; i<1024; i++) {
    tlb_read(&ehi, &elo, i);
    if (elo & TLBLO_VALID) {
      continue;
    }
    elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
    ehi = faultaddress;
    tlb_write(ehi, elo, i);
    splx(spl);
    return 0;
  }
  ehi = faultaddress;
  elo = paddr | TLBLO_DIRTY |TLBLO_VALID;

  tlb_random(ehi, elo);
  splx(spl);
  return 0;
}

struct addrspace *
as_create(void)
{
  struct addrspace *as;

  as = kmalloc(sizeof(struct addrspace));
  if (as == NULL) {
    return NULL;
  }

  array_init(&as->pages);

  as->as_first_level_base = alloc_kpages(1);

  as_zero_region(as->as_first_level_base, 1);

  return as;
}


int
as_copy(struct addrspace *old, struct addrspace **ret)
{
  struct addrspace *new;
  struct page  *news, *olds;
  vaddr_t *old_l1_vaddr, *old_l2_vaddr, *new_l1_vaddr, *new_l2_vaddr, vaddr;

  new = as_create();
  if (new==NULL) {
    return ENOMEM;
  }

  int num = (int)array_num(&old->pages);

  if(num == 0){
    return 0;
  }


  int size =(int) array_num(&old->pages);
  for(int i = 0; i < size; i++){
    olds = array_get(&old->pages, i);
    news = (struct page *)kmalloc(sizeof(struct page));
    news->as_vbase = olds->as_vbase;
    news->as_npages = olds->as_npages;
    array_add(&new->pages, news, NULL);

  }

  old_l1_vaddr = (vaddr_t *) old->as_first_level_base;
  new_l1_vaddr = (vaddr_t *) new->as_first_level_base;

  int counter  = 0;
  while (counter < 1024) {

    if (*old_l1_vaddr) {

      *new_l1_vaddr = alloc_kpages(1);

      as_zero_region(*new_l1_vaddr, 1);

      old_l2_vaddr = (vaddr_t *)(*old_l1_vaddr);
      new_l2_vaddr = (vaddr_t *)(*new_l1_vaddr);
      for (int j = 0; j < 1024; j++) {

        if (*old_l2_vaddr ) {
          vaddr = alloc_kpages(1);
          KASSERT(vaddr != 0);
          memmove((void *)vaddr, (void *)(*old_l2_vaddr & PAGE_FRAME), PAGE_SIZE);
          *new_l2_vaddr = vaddr;
         }
        old_l2_vaddr += 1;
        new_l2_vaddr += 1;
      }
    }
    old_l1_vaddr += 1;
    new_l1_vaddr += 1;
    counter ++;
  }

  *ret = new;
  return 0;
}


void
as_destroy(struct addrspace *as)
{
  vaddr_t *l1_vaddr, *l2_vaddr, vaddr;

  KASSERT(as != NULL);

  l1_vaddr = (vaddr_t *) as->as_first_level_base;

  for (int i = 0; i < 1024; ++i) {
    if (*l1_vaddr) {
      l2_vaddr = (vaddr_t *)(*l1_vaddr);
      for (int j = 0; j < 1024; ++j) {
        if (*l2_vaddr ) {
          vaddr = *l2_vaddr ;
          free_kpages(vaddr);
        }
        l2_vaddr += 1;
      }
      free_kpages(*l1_vaddr);
    }
    l1_vaddr += 1;
  }
  free_kpages(as->as_first_level_base);

  //KASSERT(as->pages != 0);
  as_destroy_regions(&as->pages);
  kfree(as);
}

void
as_destroy_regions(struct array * pages)
{
 while(array_num(pages)){
    struct page * pg = array_get(pages,0);
    kfree(pg);
    array_remove(pages,0);
  }

  array_cleanup(pages);
}

/*
 * Frobe TLB table
 */
void
as_activate()
{
  //(void)as;
  int spl = splhigh();
  for (int i = 0; i < 1024; i++) {
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
  size_t l1_index, l2_index, npages;
  vaddr_t *l1_vaddr, *l2_vaddr;
  (void)readable;
  (void)writeable;
  (void)executable;
  //struct array * pgs;

  KASSERT(as != NULL);

  // Align the region. First, the base...
  sz += vaddr & ~(vaddr_t)PAGE_FRAME;
  vaddr &= PAGE_FRAME;

  // ...and now the length.
  sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
  npages = sz / PAGE_SIZE;


  struct page * tmp_pg = (struct page *)kmalloc(sizeof(struct page));
  tmp_pg->as_vbase = vaddr;
  tmp_pg->as_npages = npages;
  //tmp_pg->as_permission = readable | writeable | executable;
  array_add(&as->pages, tmp_pg, NULL);

 /* the data struct design are take from the vedio
   * https://www.youtube.com/watch?v=zaGD95nsZDA
   *
   */

  /*
   *         |-----| total 1024
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|
   *         |-----|             1024
   *         |-----|                |-----|
   *         |-----|                |-----|
   *         |-----|                |-----|---------->"0"
   *         |-----| -------------->|-----|vaddr_level2
   *         |-----| vaddr_level1
   *         |-----|
   *         |-----|
   *         ^
   *         | as_first_level_base
   */

  for (size_t i = 0; i < npages; ++i) {
    l1_index = (vaddr & LEVEL_ONE_MASK) >> 22;
    l1_vaddr = (vaddr_t *)(as->as_first_level_base + l1_index * 4);
    if (*l1_vaddr == 0) {
      *l1_vaddr = alloc_kpages(1);
      if (*l1_vaddr == 0) {
        return EFAULT;
      }
      as_zero_region(*l1_vaddr, 1);
    }
    l2_index = (vaddr & LEVEL_TWO_MASK) >> 12;
    l2_vaddr = (vaddr_t *)(*l1_vaddr + l2_index *4);
    *l2_vaddr = 0;
    vaddr += PAGE_SIZE;
  }

  as->as_heap_start += PAGE_SIZE * npages;
  as->as_heap_end = as->as_heap_start;
  return 0;
}

/*
 * Assign the writable permission to all region in order for os
 * to load segment into physical frame
 */
int
as_prepare_load(struct addrspace *as)
{
  KASSERT(as != NULL);

  return 0;
}

/*
 * Restore the original region permission flag back
 */
int
as_complete_load(struct addrspace *as)
{
  KASSERT(as != NULL);

  return 0;
}

/*
 * Set up the mapping from stack virtual address to the page table
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
  int l1_index, l2_index;
  vaddr_t vaddr, *l1_vaddr, *l2_vaddr;
  for (int i = 0; i <STACK_PG_SIZE; ++i) {
    vaddr = USERSTACK - i * PAGE_SIZE;

    l1_index = (vaddr & LEVEL_ONE_MASK) >> 22;
    l1_index *= 4;
    l1_vaddr = (vaddr_t *)(as->as_first_level_base + l1_index);
    if (*l1_vaddr == 0) {
      *l1_vaddr = alloc_kpages(1);
      if (*l1_vaddr == 0) {
        return EFAULT;
      }
      as_zero_region(*l1_vaddr, 1);
    }
    l2_index = (vaddr & LEVEL_TWO_MASK) >> 12;
    l2_vaddr = (vaddr_t *)(*l1_vaddr + l2_index * 4);
    *l2_vaddr = 0;
  }

  // Initial user-level stack pointer
  *stackptr = USERSTACK;

  return 0;
}

/*
 * Zero out a page within the provide address
 */
void
as_zero_region(vaddr_t vaddr, unsigned npages)
{
  bzero((void *)vaddr, npages * PAGE_SIZE);
}

int sys_sbrk(intptr_t num, int * retval){

  size_t l1_index, l2_index;
  vaddr_t * l1_vaddr, * l2_vaddr;
  struct addrspace * as = curproc->p_addrspace;
  vaddr_t start = as->as_heap_start;
  vaddr_t end = as->as_heap_end;

  if(num == 0) {
    *retval = end;
    return EINVAL;
  }

  if((int)num < (int)PAGE_SIZE && (int)num <(int) (end - start)%PAGE_SIZE){
    as->as_heap_end += num;
    *retval = end;
  }else{

    intptr_t tmp_num = num;
    intptr_t rest = PAGE_SIZE - (end - start)%PAGE_SIZE;
    tmp_num = tmp_num - rest;
    int page_taken_num = tmp_num/PAGE_SIZE;
    if(tmp_num%PAGE_SIZE) page_taken_num++;

    struct page * tmp_heap = (struct page *)kmalloc(sizeof(struct page));
    tmp_heap->as_vbase = end;
    tmp_heap->as_npages = (unsigned int)page_taken_num;
    array_add(&as->pages, tmp_heap, NULL);

    vaddr_t vaddr = as->as_heap_start;
    for (int i = 0; i < page_taken_num ; i++) {
      l1_index = (vaddr & LEVEL_ONE_MASK) >> 22;
      l1_vaddr = (vaddr_t *)(as->as_first_level_base + l1_index * 4);
      if (*l1_vaddr == 0) {
        *l1_vaddr = alloc_kpages(1);
        if (*l1_vaddr == 0) {
          return EFAULT;
        }

        as_zero_region(*l1_vaddr, 1);
      }

      l2_index = (vaddr & LEVEL_TWO_MASK) >> 12;
      l2_vaddr = (vaddr_t *)(*l1_vaddr + l2_index *4);
      *l2_vaddr = 0;
      vaddr += PAGE_SIZE;
    }
  }
  as->as_heap_end += num;
  *retval = end;
  return 0;
}
