// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
char kmem_name[8][6] = {"kmem0","kmem1","kmem2","kmem3","kmem4","kmem5","kmem6","kmem7"};

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
  int length;
};
struct kmem kmems[NCPU]; /*等同CPU数目*/

void kinit(){
  for(int i=0;i<NCPU;i++){
    initlock(&kmems[i].lock, kmem_name[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end){
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){  // PAGESIZE 4096
    kfree(p);
  }
}

int get_cpu_id(){
  int cpu_id;
  push_off();
  cpu_id = cpuid();
  pop_off();
  return cpu_id;
}

void* steal(void){
  int max_index = 0;
  struct run* r;
  acquire(&kmems[0].lock);
  int max_length = kmems[0].length;
  for(int i=1;i<NCPU;i++){
    acquire(&kmems[i].lock);
      if(kmems[i].length>max_length){
        release(&kmems[max_index].lock);
        max_index = i;
        max_length = kmems[i].length;
      }
      else{
        release(&kmems[i].lock);
      }
  }
  r = kmems[max_index].freelist;
  if(r){
    kmems[max_index].freelist = r->next;
    kmems[max_index].length--;
  }
  release(&kmems[max_index].lock);
  return (void*)r;
  
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa){
  struct run *r;
  int cpu_id;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  cpu_id = get_cpu_id();
  acquire(&kmems[cpu_id].lock);
  kmems[cpu_id].length++;
  r->next = kmems[cpu_id].freelist;  //  末尾的指针会为0
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void * kalloc(void){
  struct run *r;
  int cpu_id;

  cpu_id = get_cpu_id();
  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;
  if(r){  // 有
    kmems[cpu_id].length--;
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
  } 
  else{
    // steal() 从别人那偷
    release(&kmems[cpu_id].lock);
    r = steal();  // 若偷失败则返回0，偷成功则修改那个内存的空闲列表
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
// 如果错了想一下：允许中断时不允许有任何锁
