#include <common.h>
#include <klib.h>
//#include <src/x86/x86-qemu.h>
//#include <stdio.h>


//***************** Variables ******************
#define block_num 4096
static uintptr_t pm_start, pm_end;
spinlock_t alloc_lk;
spinlock_t print_lk;
typedef struct run{
  int state;
  uintptr_t begin_addr,end_addr;
  uintptr_t size;
  struct run *next;
  struct run *prev;
}kblock;
typedef struct kmem{
  kblock *head;
  int size;
}kmem;
kmem freelist;
kmem runlist;
kblock freehead;
kblock runhead;
kblock block[block_num];
//****************** code ************************

void show_alloc(){
  kmt->spin_lock(&print_lk);
  printf("|----------------------------|\n");
  printf("now i will show runlist with size %d\n",runlist.size);
  if(runlist.size){
    kblock *pr = runlist.head -> next;
    while(1){
      printf("begin at %d and end at %d and size is %d\n",pr->begin_addr, pr->end_addr, pr->size);
      if(pr->next==NULL)
        break;
      pr = pr->next;
    }
  }
  printf("now i will show you the freelist  with size %d\n",freelist.size);
  if(freelist.size){
    kblock *pr2 = freelist.head -> next ;
    while(1){
      printf("begin at %d and end at %d and size is %d\n",pr2->begin_addr,pr2->end_addr,pr2->size);
      //printf("state is %d\n",pr2->state);
      if(pr2->next==NULL)
        break;
      pr2 = pr2->next;
    }
  }
  printf("|----------------------------|\n");
  //print_unlock();
  kmt->spin_unlock(&print_lk);
}

static void pmm_init() {
  kmt ->spin_init((spinlock_t*)&alloc_lk,"alloc_lock");
  kmt ->spin_init((spinlock_t*)&print_lk,"print_lock");
  pm_start = (uintptr_t)_heap.start;
  pm_end   = (uintptr_t)_heap.end;
  //printf("heap start at %d\n",pm_start);
  //printf("heap end at %d\n",pm_end);
  //printf("you could use %d space\n",pm_end-pm_start);
  all_size = pm_end-pm_start;
  //alloc_lk.status = 0;
  //print_lk.status =0;
  runhead.begin_addr = 0;
  runhead.prev = NULL;
  runhead.end_addr = 0;
  freehead.end_addr = freehead.begin_addr = 0;
  freehead.prev = NULL;
  freelist.head = &freehead;
  runlist.head = &runhead;
  for(int i=0;i<block_num;i++){
    block[i].state = 0;  // 0: unused 1:in freelist 2:in runlist
  } 
  block[0].begin_addr = pm_start;
  block[0].end_addr = pm_end;
  block[0].size = (pm_end-pm_start);
  block[0].state = 1;
  block[0].prev = freelist.head;
  block[0].next = NULL;
  freelist.head->prev=NULL;
  freelist.head->next = &block[0];
  //printf("begin at %d and end at %d and size is %d\n",block.begin_addr,block.end_addr,block.size);
  runlist.head->next=NULL;
  runlist.size=0;
  freelist.size = 1;
}

static int find_free_block(){
  for(int i=0;i<block_num;i++){
    if(block[i].state == 0){
      return i;
    }
  }
  //print_lock();
  kmt->spin_lock(&print_lk);
  printf("NO FREE BLOCK TO USE");
  //print_unlock();
  kmt->spin_unlock(&print_lk);
  assert(0);
}

static void block_cut(kblock *blockc,uintptr_t need_size){
    kmt->spin_lock(&print_lk);
    //print_lock();
    //printf("*** you used memory from %d to %d from cpu %d\n",blockc->begin_addr,blockc->begin_addr+need_size,(_cpu()+1));
    kmt->spin_unlock(&print_lk);
    //print_unlock();
    if(blockc->size == need_size){
        blockc->prev->next = blockc->next;
        blockc->next->prev = blockc ->prev;
        blockc->next=NULL;
        blockc->prev=NULL;
        freelist.size--;
        return;
    }
    uintptr_t rest_block_size=blockc->size-need_size;
    blockc->size = need_size;
    kblock *p_block = blockc->prev;
    int a = find_free_block();
    kblock *new_block = &block[a];
    if(blockc->next==NULL){
        //printf("!\n");
        new_block->state= 1;
        new_block->size=rest_block_size;
        new_block->end_addr=blockc->end_addr;
        blockc->end_addr=blockc->begin_addr+need_size;
        new_block->begin_addr=blockc->end_addr;
        new_block->next=NULL;
        new_block->prev=p_block;
        p_block->next= new_block;
        //printf("pre_block's end_addr is %d\n",p_block->end_addr);
        //printf(" pre_block's next block's end_addr is %d\n",p_block->next->end_addr);
        //printf("new_block's begin_addr is %d\n",new_block->begin_addr);
        blockc->next=NULL;
        blockc->prev=NULL;
        /*show_alloc(); 
        printf("pre's end %d\n",p_block->end_addr);
        printf("pre's next end %d\n",p_block->next->end_addr);      
        printf("block1's end %d\n",block[1].end_addr);
        printf("block0's end %d\n",block[0].next->end_addr);
        printf("p_block is %d\n",&p_block);
        printf("block[0] is %d\n",&block[0]);
        for(int i=0;i<=5;i++){
          printf("i is %d and end is %d\n",i,block[i].end_addr);
        }*/
        return;
    }
    kblock *n_block = blockc->next;
    if(n_block->begin_addr==blockc->end_addr){
        //printf("here1\n");
        freelist.size--;
        new_block ->state = 0;
        n_block->size+=rest_block_size;
        n_block->begin_addr-=rest_block_size;
        blockc->end_addr=blockc->begin_addr+need_size;
        p_block->next=n_block;
        n_block->prev = p_block;
        blockc->next=NULL;
        blockc->prev=NULL;
        return;
    }
    new_block->state=1;
    new_block->size=rest_block_size;
    new_block->end_addr=blockc->end_addr;
    blockc->end_addr=blockc->begin_addr+need_size;
    new_block->begin_addr=blockc->end_addr;
    new_block->next=n_block;
    new_block->prev=p_block;
    n_block->prev= new_block;
    p_block->next= new_block; 
    blockc->next=NULL;
    blockc->prev=NULL;
}

static void add_runlist(kblock *blockadd){
    if(!runlist.size){
        runlist.head->next=blockadd;
        blockadd ->prev = runlist.head;
        blockadd ->next = NULL;
    }
    else{
        kblock *tail_block=runlist.head->next;
        while (tail_block->next!=NULL){
            tail_block=tail_block->next;
        }
        tail_block->next= blockadd;
        blockadd ->prev = tail_block;
        blockadd -> next = NULL;
    }
    runlist.size++;
}

static void *alloc_unsafe(size_t size){
  if(size == 0)
    return NULL;
  uintptr_t block_size = (size/1024+(size%1024!=0))*1024;
  used_size += block_size;
  kblock *block1 = freelist.head->next;
  while(block1->size<block_size&&block1->next!=NULL){
      block1 = block1->next;
  }
  if(block1->size<block_size){
      kmt->spin_lock(&print_lk);
      //print_lock();
      printf("you need %d but you dont have it\n",block_size);
      kmt->spin_unlock(&print_lk);
      //print_unlock();
      return NULL;
      //assert(0);
  }
  block1->state=2;
  block_cut(block1,block_size);
  add_runlist(block1);
  //printf("here2\n");
  return (void*)block1->begin_addr;
}

static void check_block(){
    kblock *chek = freelist.head->next;
    while(chek -> next != NULL){
      if(chek->end_addr == chek->next->begin_addr){
        kblock *nblock = chek->next;
        chek->end_addr = nblock->end_addr;
        chek->size += nblock->size;
        nblock ->state = 0;
        chek ->next = nblock ->next;
        nblock -> next -> prev = chek;
        nblock ->next = nblock->prev = NULL;
        freelist.size--;
      }
      chek = chek ->next;
    }
}

void free_unsafe(uintptr_t b_addr){
    kmt->spin_lock(&print_lk);
    //print_lock();
    //printf("### you want to free block from %d\n",b_addr);
    //print_unlock();
    kmt->spin_unlock(&print_lk);
    if(!runlist.size){
        kmt->spin_lock(&print_lk);
        //print_lock();
        printf("WRONG : WE DONT USE THE ADDR!\n");
        //print_unlock();
        kmt->spin_unlock(&print_lk);
        return;
    }
    kblock *used_block = runlist.head->next;
    while(used_block->begin_addr!=b_addr && used_block->next !=NULL){
        used_block=used_block->next;
        }
    if(used_block->begin_addr!=b_addr){
        kmt->spin_lock(&print_lk);
        //print_lock();
        printf("WRONG : WE DONT USE THE ADDR!\n");
        //print_unlock();
        kmt->spin_unlock(&print_lk);
        return;
    }
    used_size =used_size - used_block->size;
    used_block->state=1;
    runlist.size--;
    freelist.size++;   //show_alloc();
    kblock *ppblock = used_block->prev;
    ppblock->next = used_block->next;
    used_block->next->prev = used_block->prev;
    //freelist is null 
    if(freelist.head->next==NULL){
        freelist.head->next=used_block;
        used_block->prev=freelist.head;
        used_block->next =NULL;
        freelist.size++;
        return;
    }
    kblock *used_prev=freelist.head->next;
    if(used_block->end_addr < used_prev->begin_addr){
        freelist.head->next=used_block;
        used_block->next = used_prev;
        used_block->prev=freelist.head;
        used_prev->prev=used_block;
        return;
    }
    if(used_block ->end_addr == used_prev -> begin_addr){
      used_prev->begin_addr = used_block ->begin_addr;
      used_prev->size += used_block->size;
      used_block->state = 0;
      used_block -> prev = used_block ->next =NULL;
      freelist.size--;
      return;
    }
    while(used_prev->next!=NULL && used_prev->next->end_addr < used_block->begin_addr){
        used_prev=used_prev->next;
    }
    if(used_prev->next==NULL){
        used_prev->next=used_block;
        used_block->prev=used_prev;
    }
    else if(used_prev->end_addr == used_block->begin_addr){
      used_prev->end_addr = used_block ->end_addr;
      used_prev->size += used_block->size;
      used_block->state = 0;
      used_block ->prev =NULL;
      //ppblock->next = used_block->next;
      used_block -> next =NULL;
      freelist.size--;
    }
    else{
        used_block->next=used_prev->next;
        used_prev->next->prev=used_block;
        used_block->prev=used_prev;
        used_prev->next=used_block;
    }
    check_block();
}




static void *kalloc(size_t size) {
  kmt->spin_lock(&alloc_lk);
  //alloc_lock();
  void *ret = alloc_unsafe(size);
  //printf("finish\n");
  //alloc_unlock();
  //show_alloc();
  kmt->spin_unlock(&alloc_lk);
  //printf("hi\n");
  return ret;
}

static void kfree(void *ptr) {
  kmt->spin_lock(&alloc_lk);
  //alloc_lock();
  if(ptr == NULL){
    kmt->spin_lock(&print_lk);
    printf("WRONG: YOU WANT TO FREE A NULL SPACE\n");
    kmt->spin_unlock(&print_lk);
    //print_unlock();
  }
  else{
    free_unsafe((uintptr_t)(ptr));
    //show_alloc();
  }
  kmt->spin_unlock(&alloc_lk);
  //alloc_unlock();
  //spin_unlock(&alloc_lk);
}

MODULE_DEF(pmm) {
  .init = pmm_init,
  .alloc = kalloc,
  .free = kfree,
};
