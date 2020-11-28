// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKET 8
#define MOD 7
char bcache_name[13][9] = {"bcache0","bcache1","bcache2","bcache3","bcache4","bcache5","bcache6","bcache7","bcache8","bcache9","bcache10","bcache11","bcache12"};
struct {
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head[NBUCKET];
} bcache;

int find_the_slot(uint dev,uint blockno){
  return ((dev<<1)^(blockno>>4)^blockno)&MOD;
}

void binit(void){
// 初始化一个双向的链表结构大概是:
// [head0] -(next)> [28/21/14/7] -(next)> ... -(next)> [0] -(next)> [head]
// [headi] <(prev)- [n*NBUCKET+i] <(prev)- ... <(prev)- [i] <(prev)- [head]
// 无竞争
  struct buf *b;
  for(int i=0;i<NBUCKET;i++){
    initlock(&bcache.lock[i], bcache_name[i]);
    // initlock(&bcache.lock[i], "bcache");
    // Create linked list of buffers
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }
  uint l;
  struct buf* head_pointer = 0;
  for(b = bcache.buf,l = 0; b < bcache.buf+NBUF; b++,l++){
    head_pointer = &bcache.head[l&MOD];
    b->next = head_pointer->next;
    b->prev = head_pointer;
    initsleeplock(&b->lock, "buffer");
    head_pointer->next->prev = b;
    head_pointer->next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// 为指定的设备磁盘块(dev,blockno)寻找一个它对应的缓冲区，如果没有找到，则分配一个；如果找到了就返回一个上了睡眠锁的
static struct buf* bget(uint dev, uint blockno){
  struct buf* b;
  int slot_0 = find_the_slot(dev,blockno);
  // Is the block already cached?
  // 检查是否已经给设备磁盘块分配过缓冲区（注意这个缓冲区可能不在hash到的那个桶里）
  for(int count = 0,slot = slot_0; count < NBUCKET; count++,slot++){
    if(slot == NBUCKET){
      slot = 0;
    }
    acquire(&bcache.lock[slot]);
    for(b = bcache.head[slot].next; b != &bcache.head[slot]; b = b->next){
      if(b->dev == dev && b->blockno == blockno){
        b->refcnt++;
        release(&bcache.lock[slot]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[slot]);
  }
  // Not cached; recycle an unused buffer.
  // 从邻居处找一个空闲的块
  for(int count = 0;count <NBUCKET; count++,slot_0++){
    if(slot_0 == NBUCKET){
      slot_0 = 0;
    }
    acquire(&bcache.lock[slot_0]);
    for(b = bcache.head[slot_0].prev; b != &bcache.head[slot_0]; b = b->prev){
      if(b->refcnt == 0) {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          release(&bcache.lock[slot_0]);
          acquiresleep(&b->lock);
          return b;
        }
    }
    release(&bcache.lock[slot_0]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf* bread(uint dev, uint blockno){
  struct buf *b;

  b = bget(dev, blockno);  // b->lock开了睡眠锁
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);  // 读取磁盘数据放到缓存块
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b){
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);  // 将缓存块数据写回磁盘
}

// Release a locked buffer.
// Move to the head of the MRU list.
void brelse(struct buf *b){
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int slot = (b-bcache.buf)&MOD;
  acquire(&bcache.lock[slot]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 将这个复原的缓冲块放到链表头
    struct buf* head = &bcache.head[(b-bcache.buf)&MOD];
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = head->next;
    b->prev = head; 
    head->next->prev = b;
    head->next = b;
  }
  
  release(&bcache.lock[slot]);
}

void bpin(struct buf *b){
  int slot = (b - bcache.buf)&MOD;
  acquire(&bcache.lock[slot]);
  b->refcnt++;
  release(&bcache.lock[slot]);
}

void bunpin(struct buf *b){
  int slot = (b - bcache.buf)&MOD;
  acquire(&bcache.lock[slot]);
  b->refcnt--;
  release(&bcache.lock[slot]);
}


