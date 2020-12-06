#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy allocator

static int nsizes;     // the number of entries in bd_sizes array

#define LEAF_SIZE     16                         // The smallest block size
#define MAXSIZE       (nsizes-1)                 // Largest index in bd_sizes array
#define BLK_SIZE(k)   ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define HEAP_SIZE     BLK_SIZE(MAXSIZE) 
#define NBLK(k)       (1 << (MAXSIZE-k))         // Number of block at size k
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
struct sz_info {
  Bd_list free;
  char *alloc;
  char *split;
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes; 
static void *bd_base;   // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
int bit_isset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void bit_set(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b | m);
}

// Clear bit at position index in array
void bit_clear(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}

void bit_xor(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b ^ m);
}
// Print a bit vector as a list of ranges of 1 bits
void bd_print_vector(char *vector, int len) {
  int last, lb;
  
  last = 1;
  lb = 0;
  for (int b = 0; b < len; b++) {
    if (last == bit_isset(vector, b))
      continue;
    if(last == 1)
      printf(" [%d, %d)", lb, b);
    lb = b;
    last = bit_isset(vector, b);
  }
  if(lb == 0 || last == 1) {
    printf(" [%d, %d)", lb, len);
  }
  printf("\n");
}

// Print buddy's data structures
void bd_print() {
  for (int k = 0; k < nsizes; k++) {
    printf("size %d (blksz %d nblk %d): free list: ", k, BLK_SIZE(k), NBLK(k));
    lst_print(&bd_sizes[k].free);
    printf("  alloc:");
    bd_print_vector(bd_sizes[k].alloc, NBLK(k));
    if(k > 0) {
      printf("  split:");
      bd_print_vector(bd_sizes[k].split, NBLK(k));
    }
  }
}

// What is the first k such that 2^k >= n?
int firstk(uint64 n) {
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n) {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address p at size k
int blk_index(int k, char *p) {
  int n = p - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
void *addr(int k, int bi) {
  int n = bi * BLK_SIZE(k);
  return (char *) bd_base + n;
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void * bd_malloc(uint64 nbytes)
{
  int fk, k;

  acquire(&lock);

  // Find a free block >= nbytes, starting with smallest k possible
  fk = firstk(nbytes);
  for (k = fk; k < nsizes; k++) {
    if(!lst_empty(&bd_sizes[k].free))
      break;
  }
  if(k >= nsizes) { // No free blocks?
    release(&lock);
    return 0;
  }

  // Found a block; pop it and potentially split it.
  char *p = lst_pop(&bd_sizes[k].free);
  bit_xor(bd_sizes[k].alloc, blk_index(k, p)>>1);  // 只需要处理alloc，当它出现在了链表上时，已经说明它被分裂了（因为meta和无效区的存在）
  for(; k > fk; k--) {  // 处理分裂标签，在高于fk的层都将把右儿子放到链表上，并维护alloc和split
    // split a block at size k and mark one half allocated at size k-1
    // and put the buddy on the free list at size k-1
    char *q = p + BLK_SIZE(k-1);   // p's buddy 的地址
    bit_set(bd_sizes[k].split, blk_index(k, p));
    bit_set(bd_sizes[k-1].alloc, blk_index(k-1, p)>>1);  // 通过地址找到下一层的alloc 的 index
    lst_push(&bd_sizes[k-1].free, q);  // 将右儿子地址放到空闲链表中
  }
  // fk层 不需要 split
  release(&lock);

  return p;
}

// Find the size of the block that p points to.
int size(char *p) {
  for (int k = 0; k < nsizes; k++) {
    if(bit_isset(bd_sizes[k+1].split, blk_index(k+1, p))) {
      return k;
    }
  }
  return 0;
}

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
void bd_free(void *p) {
  void *q;
  int k;
  acquire(&lock);
  // 这个p一定是被使用的，它的上层一定被分裂，而它这一层没被分裂，利用这个特性找到它的层->size()函数
  for (k = size(p); k < MAXSIZE; k++) {
    int bi = blk_index(k, p);
    int buddy = ((bi & 1) == 0) ? bi+1 : bi-1;
    bit_xor(bd_sizes[k].alloc, bi>>1);  // 出现bd_malloc/_free只需要翻转
    if (bit_isset(bd_sizes[k].alloc, buddy>>1)) {  // is buddy allocated? 如果为1说明现在buddy正被使用，不可继续合并
      break;   // break out of loop
    }
    // budy is free; merge with buddy
    q = addr(k, buddy);  // 通过buddy编号以及第k层块的大小来计算出buddy的地址
    lst_remove(q);    // remove buddy from free list 节点->X->节点
    if(buddy % 2 == 0) {  // [0 1] [2 3] [4 5] 维护p为偶数块地址 左儿子
      p = q;
    }
    // at size k+1, mark that the merged buddy pair isn't split
    // anymore
    bit_clear(bd_sizes[k+1].split, blk_index(k+1, p));
  }
  lst_push(&bd_sizes[k].free, p);
  release(&lock);
}

// Compute the first block at size k that doesn't contain p
int blk_index_next(int k, char *p) {
  int n = (p - (char *) bd_base) / BLK_SIZE(k);
  if((p - (char*) bd_base) % BLK_SIZE(k) != 0)
      n++;
  return n ;
}

int log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated. 
void bd_mark(void *start, void *stop)
{
  int bi, bj;

  if (((uint64) start % LEAF_SIZE != 0) || ((uint64) stop % LEAF_SIZE != 0))
    panic("bd_mark");

  for (int k = 0; k < nsizes; k++) {
    bi = blk_index(k, start);  
    bj = blk_index_next(k, stop);  // 小数向上取整，整数不动
    // 不能这里进行插入,奇怪的错误
    if((bi & 1) == 1){  // [0 1] [2 3] [偶 奇] 如果左闭边界是奇数，则说明[No Used,bi] [bi+1,...]
      bit_xor(bd_sizes[k].alloc,bi>>1);
      // lst_push(&bd_sizes[k].free, addr(k, bi-1));   // put buddy on free list
      // printf("k:%d, bi-1:%d, addr:%p\n",k,bi-1,addr(k, bi-1));  // put buddy on free list)
    }
    if((bj & 1) == 1){  // [0 1] [2 3] [偶 奇] 如果右开边界是奇数，则说明[...,][Used,bj) bj就是No Used
      bit_xor(bd_sizes[k].alloc,bj>>1);
      // if(bi+1!=bj){  // 跳过最高层
      //   lst_push(&bd_sizes[k].free, addr(k, bj));   // put buddy on free list
      // }
      // printf("k:%d, bj:%d, addr:%p\n",k,bj,addr(k, bj));  // put buddy on free list)
    }
    for(; bi < bj; bi++) {
      if(k > 0) {
        // if a block is allocated at size k, mark it as split too.
        bit_set(bd_sizes[k].split, bi);
      }
    }
  }
}

// If a block is marked as allocated and the buddy is free, put the
// buddy on the free list at size k.
int bd_initfree_pair(int k, int bi, void *bd_left, void *bd_right) {  // [bd_left,bd_right]为可用区间
  int buddy = (bi % 2 == 0) ? bi+1 : bi-1;
  int free = 0;
  if(bit_isset(bd_sizes[k].alloc, bi>>1)) {
    // one of the pair is free
    free = BLK_SIZE(k);
    void* buddy_address = addr(k,buddy);
    if(buddy_address>=bd_left && buddy_address<bd_right)
      lst_push(&bd_sizes[k].free, addr(k, buddy));   // put buddy on free list
    else
      lst_push(&bd_sizes[k].free, addr(k, bi));      // put bi on free list
  }
  return free;
}
  
// Initialize the free lists for each size k.  For each size k, there
// are only two pairs that may have a buddy that should be on free list:
// bd_left and bd_right.
int bd_initfree(void *bd_left, void *bd_right) {
  int free = 0;

  for (int k = 0; k < MAXSIZE; k++) {   // skip max size
    int left = blk_index_next(k, bd_left);
    int right = blk_index(k, bd_right);
    free += bd_initfree_pair(k, left, bd_left, bd_right);
    if(right <= left)
      continue;
    free += bd_initfree_pair(k, right, bd_left, bd_right);
  }
  return free;
}

// Mark the range [bd_base,p) as allocated
int bd_mark_data_structures(char *p) {
  int meta = p - (char*)bd_base;
  printf("bd: %d meta bytes for managing %d bytes of memory\n", meta, BLK_SIZE(MAXSIZE));
  bd_mark(bd_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int bd_mark_unavailable(void *end, void *left) {
  int unavailable = BLK_SIZE
  (MAXSIZE)-(end-bd_base);
  if(unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  bd_mark(bd_end, bd_base+BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
void bd_init(void *base, void *end) {
  char *p = (char *) ROUNDUP((uint64)base, LEAF_SIZE);  // 头部对齐16字节
  int sz;

  initlock(&lock, "buddy");
  bd_base = (void *) p;  // 使用全局变量记录真实分配区域头部

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char *)end-p)/LEAF_SIZE) + 1;  
  // (end-p)/LEAF_SIZE [整数除法向下取整，(47-0)/16->2] -> (end-p)/16计算最小块数,log2取层数
  // (47-0)/16=2 log2(2)=1 [1,1]-> [1 1] 实际上2层
  // (31-0)/16=1 log2(1)=0 [1] 实际上也要2层作为管理
  // MAXSIZE 实际可用最大层 nsizes-1
  // ((1Long << ((nsizes-1))) * 16)
  if((char*)end-p > BLK_SIZE(MAXSIZE)) {
    nsizes++;  // round up to the next power of 2
  }

  printf("bd: memory sz is %d bytes; allocate an size array of length %d\n",
         (char*) end - p, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *) p;
  p += sizeof(Sz_info) * nsizes;
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes);  // 该数据区作为存放Sz_info区域

  // initialize free list and allocate the alloc array for each size k
  for (int k = 0; k < nsizes; k++) {
    lst_init(&bd_sizes[k].free);
    sz = sizeof(char)* ROUNDUP(NBLK(k), 8)>>4;  // (当前层块数/8/2)个字节，层数越高，块数越少
    // xxxx/8 -> xxxx/16
    // 这里卡了很久...... sz 现在是 8的倍数，在之前/8是没问题的，而如果在这里/16将会把sz置成0，而sz实际上不足1要补1
    if(sz == 0) {
      sz = 1;
    }
    bd_sizes[k].alloc = p; 
    // printf("k:%d, sz:%d, alloc array address:%p\n",k,sz,(char*)p);
    memset(bd_sizes[k].alloc, 0, sz);
    p += sz;
  }

  // allocate the split array for each size k, except for k = 0, since
  // we will not split blocks of size k = 0, the smallest size.
  for (int k = 1; k < nsizes; k++) {
    sz = sizeof(char)* (ROUNDUP(NBLK(k), 8))/8;
    // printf("k:%d, sz:%d, split array address:%p\n",k,sz,(char*)p);
    bd_sizes[k].split = p;
    memset(bd_sizes[k].split, 0, sz);
    p += sz;
  }
  p = (char *) ROUNDUP((uint64) p, LEAF_SIZE);

  // done allocating; mark the memory range [base, p) as allocated, so
  // that buddy will not hand out that memory.
  int meta = bd_mark_data_structures(p);
  
  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  int unavailable = bd_mark_unavailable(end, p);
  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  
  // initialize free lists for each size k
  int free = bd_initfree(p, bd_end);
  // check if the amount that is free is what we expect
  // bd_print();
  if(free != BLK_SIZE(MAXSIZE)-meta-unavailable) {
    printf("free %d %d\n", free, BLK_SIZE(MAXSIZE)-meta-unavailable);
    panic("bd_init: free mem");
  }
}