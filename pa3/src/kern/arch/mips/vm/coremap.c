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

//recode the first paddr after coremap
static paddr_t next_free_frame = 0;
int coremap_ready_flag ;

//static unsigned long num_of_pages = 0;
unsigned long num_of_free_pages = 0;
struct coremap_entry *  coremap = NULL;

static paddr_t start_paddr = 0;
static paddr_t end_paddr = 0;
static paddr_t free_frame_after_coremap = 0;
static int free_frame_after_coremap_id = 0;
static size_t coremap_size = 0;
static int coremap_page_num = 0;
//static int num_of_entities = 0;
static unsigned long total_ppages = 0;
static __u64 timestamp_counter = 0;
//static in t evit_page_index = 0;


int coremap_initialize() {

  start_paddr = ram_get_first_paddr();
  end_paddr = ram_get_last_paddr();

  KASSERT((start_paddr&PAGE_FRAME)==start_paddr);
  KASSERT((end_paddr&PAGE_FRAME)==end_paddr);

  total_ppages = (end_paddr - start_paddr)/PAGE_SIZE;
  //num_of_pages = total_ppages;
  num_of_free_pages = total_ppages;

  if((int)total_ppages == -1) {
    panic("npages is 0\n");
  }

  //create coremap table
  create_coremap(sizeof(struct coremap_entry), total_ppages);


  int coremap_index = 0;
  coremap[coremap_index].paddr = start_paddr;
  coremap[coremap_index].block_len = coremap_page_num;
  coremap[coremap_index].is_allocated = 1;
  coremap[coremap_index].is_pinned = 0;

  for(coremap_index = 1; coremap_index <(int)total_ppages; coremap_index++) {
    if(coremap_index < coremap_page_num) {
      coremap[coremap_index].paddr = start_paddr + coremap_index * PAGE_SIZE;
      coremap[coremap_index].block_len = 0;
      coremap[coremap_index].is_allocated = 1;
      coremap[coremap_index].is_pinned = 0;
      coremap[coremap_index].timestamp = 0;
    }
    coremap[coremap_index].paddr = start_paddr + coremap_index * PAGE_SIZE;
    coremap[coremap_index].block_len = 0;
    coremap[coremap_index].is_pinned = 0;
    coremap[coremap_index].is_allocated = 0;
    coremap[coremap_index].timestamp = 0;
  }

    //TODO: make up other information of kernel page
  coremap_ready_flag = 1;
  return 0;
}

void create_coremap(unsigned long entry_size, unsigned long num_of_entires) {
  size_t size;
  //unsigned long tmp_npages;

  size = entry_size * num_of_entires;
  size_t tmp_size = ROUNDUP(size, PAGE_SIZE);

  coremap_size = tmp_size ;
 // kprintf("pages needed for coremap: %d", (int)tmp_npages);

  coremap_page_num = coremap_size/PAGE_SIZE;
  //KASSERT((coremap_size & PAGE_FRAME)==coremap_size);
  free_frame_after_coremap = start_paddr + coremap_size;
  free_frame_after_coremap_id = convert_paddr_to_coremap_index(free_frame_after_coremap);

  if(free_frame_after_coremap >= end_paddr) {
    panic("Out of Memory`````\n");
  }
  coremap = (struct coremap_entry *) PADDR_TO_KVADDR(start_paddr);
  num_of_free_pages = num_of_free_pages - coremap_page_num;
  next_free_frame = (paddr_t)(free_frame_after_coremap);
}

unsigned long convert_coremap_index_to_paddr(unsigned long index) {
  //the firstfree_frame is started after coremap
  return index*PAGE_SIZE + start_paddr;
}
int convert_paddr_to_coremap_index(paddr_t addr) {
  int index = (addr-start_paddr)/PAGE_SIZE;
  return index;
}

paddr_t allocate_pages_by_coremap(struct page_struct * lg_page, bool pin_state, unsigned long npages) {
  paddr_t p_addr = 0;
  int is_kern;

  is_kern = (lg_page == NULL);
  if(is_kern) {
    pin_state = 0;
  }

  if(is_kern) {
    p_addr = get_kern_pages(lg_page, pin_state, npages);
  }

  return p_addr;
}

paddr_t get_kern_pages(struct page_struct *lg_page, bool pin_state, unsigned long npages) {

  (void)lg_page;
  (void)pin_state;
  paddr_t addr = 0;
  int page_track_counter = 0;
  int pos = -1;
 // int next_free_frame_id = convert_paddr_to_coremap_index(next_free_frame);

  if(num_of_free_pages >= npages) {
    unsigned long coremap_index = free_frame_after_coremap_id;
    for(coremap_index = free_frame_after_coremap_id;
        coremap_index < total_ppages;
        coremap_index++) {

      if(coremap[coremap_index].is_allocated){
        page_track_counter = 0;
        continue;
      }
      page_track_counter++;

      if((int)npages == page_track_counter) {
        pos =(int)coremap_index + 1 - (int)npages;
        break;
      }
    }
  } else {
    pos = replace_pages();
    //vaddr_t tmp_vaddr = PADDR_TO_KVADDR(coremap[pos].paddr);
    int len = coremap[pos].block_len;
    sw_one_pageout(coremap[pos].paddr);
    for(int i = pos + 1; i < pos + len ; i++ ){
      //tmp_vaddr = PADDR_TO_KVADDR(coremap[i].paddr);
      sw_one_pageout(coremap[i].paddr);
    }
    free_ppges(coremap[pos].paddr);
  }

  if(pos == -1){
    pos = replace_pages();
    vaddr_t tmp_vaddr = PADDR_TO_KVADDR(coremap[pos].paddr);
    int len = coremap[pos].block_len;
    sw_one_pageout(tmp_vaddr);
    for(int i = pos + 1; i < pos + len ; i++ ){
      tmp_vaddr = PADDR_TO_KVADDR(coremap[i].paddr);
      sw_one_pageout(tmp_vaddr);
    }
    free_ppges(coremap[pos].paddr);
  }


  if(pos != -1) {
    timestamp_counter++;
    int page_used_counter = 0;
    KASSERT(coremap[pos].is_allocated == 0);
    coremap[pos].is_allocated = 1;
    coremap[pos].block_len = npages;
    coremap[pos].timestamp = timestamp_counter;
    page_used_counter++;
    int i = pos + 1;
    for(i = pos +1; i < pos + (int)npages; i++) {
      coremap[i].timestamp = timestamp_counter;
      coremap[i].is_allocated = 1;
      page_used_counter++;
    }

    num_of_free_pages = (int)num_of_free_pages - page_used_counter;
  }else{
    panic("coremap:195~~~~~~~~~~~~~");
  }

  addr =  coremap[pos].paddr;

  return addr;
}


void free_ppges(paddr_t addr) {
  int index = convert_paddr_to_coremap_index(addr);
  KASSERT(coremap[index].is_allocated == 1);
  int len  = coremap[index].block_len;
  for(int i = index; i < index + len; i++ ) {
    coremap[i].is_allocated = 0;
    coremap[i].is_pinned = 0;
    coremap[i].block_len = 0;
    coremap[i].timestamp = 0;
   // coremap[index].is_kern_page = 0;
   // coremap[index].page = NULL;
   // coremap[index].owner = 0;
    //TODO: add more info;
    num_of_free_pages++;
  }

}

unsigned long replace_pages() {
  __u64 oldest_time_stamp = coremap[free_frame_after_coremap_id].timestamp;
  unsigned long evict_page_index = free_frame_after_coremap_id;

  unsigned long index  = free_frame_after_coremap_id;
  for(index = free_frame_after_coremap_id; index < total_ppages; index++){
    if(coremap[index].timestamp > 0){
      evict_page_index = index;
      break;
    }
  }

  oldest_time_stamp = coremap[index].timestamp;

  for(unsigned long i = free_frame_after_coremap_id; i < total_ppages; i++){
    __u64 tmp = coremap[i].timestamp;
    if(tmp > 0 && oldest_time_stamp > tmp){
      KASSERT(coremap[i].is_allocated == 1);
      oldest_time_stamp = tmp;
      evict_page_index = i;
      i += coremap[i].block_len;
    }
  }
  return evict_page_index;
}
