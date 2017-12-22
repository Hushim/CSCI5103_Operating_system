#include <types.h>
#include <kern/errno.h>
#include <proc.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <bitmap.h>
#include <synch.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <vnode.h>
#include <array.h>

static size_t total_solt;
static size_t free_slot;
static size_t offset = 1;
struct vnode *sw_node;
struct array records;
static struct spinlock sw_lock = SPINLOCK_INITIALIZER;
char DISK_NAME[] = "lhd0raw:";


void
sw_init(void) {
  int check;

  struct stat sw_dsk_stat;

  check = vfs_open(DISK_NAME, O_RDWR, 0, &sw_node);

  if(check) {
    panic("fail to open the disk %s\n", DISK_NAME);
  }

  VOP_STAT(sw_node, &sw_dsk_stat);
  free_slot = sw_dsk_stat.st_size /PAGE_SIZE;
  total_solt = free_slot;

  if(free_slot == 0) {
    panic("%s do not have enough space \n", DISK_NAME);
  }

  array_init(&records);


 kprintf("done with swap Initial \n");
 kprintf("the total_solt is %d \n", total_solt );
 kprintf("the free_slot is %d \n", free_slot);
}


int
sw_exist(pid_t pid, vaddr_t addr) {

  for(unsigned int i = 0; i < array_num(&records); i++) {
    struct record *tmp = array_get(&records, i);
    if((unsigned)tmp->ower == (unsigned)addr && (unsigned)tmp->which_pid == (unsigned)pid)
      return i;
  }

  return -1;
}

void
sw_delet_recoder(pid_t pid, vaddr_t addr) {

  for(unsigned int i = 0; i < array_num(&records); i++) {
    struct record *tmp = array_get(&records, i);
    if((unsigned)tmp->ower == (unsigned)addr && (unsigned)tmp->which_pid == (unsigned)pid)
     array_remove(&records, i);
     return;
  }
}



int
sw_one_pageout(paddr_t paddr){

  struct uio sw_u;
  struct iovec sw_iov;
  struct record *tmp_recode;




  vaddr_t vaddr = PADDR_TO_KVADDR(paddr);
  int offset_exiest = sw_exist(curproc->t_pid, vaddr);

  //spinlock_acquire(&sw_lock);
  if(offset_exiest == -1) {
    //spinlock_release(&stealmem_lock);

    uio_kinit(&sw_iov, &sw_u,(void *) vaddr, PAGE_SIZE, offset * vaddr ,UIO_WRITE);
    //spinlock_release(&sw_lock);
    tmp_recode = kmalloc(sizeof(struct record));
    tmp_recode->ower = vaddr;
    tmp_recode->which_pid = curproc->t_pid;
    tmp_recode->offset = vaddr + curproc->t_pid;
    array_add(&records, tmp_recode, NULL);
    offset += 1;

  }else {
    //spinlock_release(&sw_lock);
    struct record *tmp_recode = array_get(&records, offset_exiest);
    off_t get_offset = tmp_recode->offset;
    //spinlock_release(&sw_lock);
    uio_kinit(&sw_iov, &sw_u,(void *) vaddr, PAGE_SIZE, get_offset ,UIO_WRITE);

  }


  spinlock_acquire (&sw_lock);
  VOP_WRITE(sw_node, &sw_u);
  spinlock_release(&sw_lock);


  return 0;
}

vaddr_t
sw_one_pagein(vaddr_t vaddr, pid_t pid){

  struct uio sw_u;
  struct iovec sw_iov;
  vaddr_t  tmp_vaddr = alloc_kpages(1);

  //lock_acquire(sw_lock);
  int offset_exiest = sw_exist(pid, vaddr);
  struct record *tmp_recode = array_get(&records, offset_exiest);
  off_t get_offset = tmp_recode->offset;
  uio_kinit(&sw_iov, &sw_u,(char *) tmp_vaddr, PAGE_SIZE, get_offset ,UIO_READ);

  VOP_READ(sw_node, &sw_u);
  //****
  // call the sw_delet_recoder function
  // to remove the recoder and to update the recodrs status;
  //lock_release(sw_lock);
  return tmp_vaddr;
}

void
sw_finishe(void) {
  vfs_close(sw_node);
  //lock_destroy(&sw_lock);
  while(array_num(&records) > 0) {
    struct record * tmp = array_get(&records, 0);
    kfree(tmp);
    array_remove(&records, 0);
  }
  array_cleanup(&records);
}
