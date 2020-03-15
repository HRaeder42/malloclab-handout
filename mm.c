/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team 32",
    /* First member's full name */
    "Dylan Clausen",
    /* First member's email address */
    "dylanclausen2022@u.northwestern.edu",
    /* Second member's full name (leave blank if none) */
    "Henry Raeder",
    /* Second member's email address (leave blank if none) */
    "henryraeder2020@u.northwestern.edu"
};

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Double word size (bytes) */
#define ALIGNMENT   8       /* Double word alignment (4 for single) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */

/* Rounds up to nearest multiple of current alignment (in bytes) */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) //line:vm:mm:pack

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/*given a free block ptr bp, compute address of its next and previous pointer */
#define PTRN(bp)       (*(char **)(bp))
#define PTRP(bp)       (*(char **)(bp + WSIZE))

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Size of a size_t accounting for alignment */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Number of segregated lists */
#define NUM_SEG_LISTS  20

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */
static char *seg_listp = 0;        /* Explicit List Root*/

/* Segregated list helpers */

#define PUT_PTR(p, ptr)  (*(unsigned int *)(p) = (unsigned int)(ptr))

/* Get address of the previous or next field of a free seg_list block */
#define GET_PREV(bp)    ((char *)bp)
#define GET_NEXT(bp)    ((char *)(bp) + WSIZE)

/* Get address of previous or next block in list */
#define GET_PREV_BLK(bp)    (*(char **)(bp))
#define GET_NEXT_BLK(bp)    (*(char **)(GET_NEXT(bp)))

/* Gets particular list from set of all seg_lists */
#define SEG_LIST(ptr, index) (*((char **)ptr+index))

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void m_check(int verbose);
static void checkblock(void *bp);
static void insert_free_block(void *bp, size_t b_size);
static void remove_free_block(void *bp);

/*
 * mm_init - Initialize the memory manager
 */
/* $begin mminit */
int mm_init(void)
{
    int list_index;
    seg_listp = mem_sbrk(NUM_SEG_LISTS*WSIZE);

    /* Initialize all lists */
    for (list_index = 0; list_index < NUM_SEG_LISTS; list_index++) {
        SEG_LIST(seg_listp, list_index) = NULL;
    }

    /* Create empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + (1*WSIZE), PACK(ALIGNMENT, 1));
    PUT(heap_listp + (2*WSIZE), PACK(ALIGNMENT, 1));
    PUT(heap_listp + (3*WSIZE), PACK(0,1));
    heap_listp += (2*WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
 */
/* $begin mmmalloc */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;

    /* Nothing to allocate */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        bp = place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    bp = place(bp, asize);
    return bp;
}
/* $end mmmalloc */

/*
 * mm_free - Free a block
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    /* Updates headers to show as unallocated */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* Inserts newly-freed block into proper list */
    insert_free_block(bp, size);
    coalesce(bp);
}

/* $end mmfree */
/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
/* $begin mmfree */
static void *coalesce(void *bp)
{
    char *prv = PREV_BLKP(bp);
    char *nxt = NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(FTRP(prv));
    size_t next_alloc = GET_ALLOC(HDRP(nxt));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* Case 1 */
        return bp;                 /* Nothing to coalesce */
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
                                    /* Coalesce forwards */
        remove_free_block(bp);
        remove_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
                                 /* Coalesce backwards */
        remove_free_block(bp);
        remove_free_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {                                     /* Case 4 */
                                /* Bidirectional coalesce */
        remove_free_block(PREV_BLKP(bp));
        remove_free_block(bp);
        remove_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    insert_free_block(bp, size);
    return bp;
}


/*
 * mm_realloc - Naive implementation of realloc
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize, asize, next_size;
    void *newptr;
    void *nextblk;

    /* Just free */
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    /* Just malloc */
    if (ptr == NULL) {
        return mm_malloc(size);
    }

    asize = ALIGN(size);

    oldsize = GET_SIZE(HDRP(oldptr)) - DSIZE; //Subtracts header and footer size

    if (asize == oldsize) {
        return ptr;
    }

    /* Previously-allocated block can fit new block */
    if (asize < oldsize){
        if (oldsize - asize - DSIZE <= DSIZE)
            return ptr;
        
        PUT(HDRP(ptr), PACK(asize + DSIZE, 1));
        PUT(FTRP(ptr), PACK(asize + DSIZE, 1));

        newptr = ptr;

        ptr = NEXT_BLKP(newptr);

        PUT(HDRP(ptr), PACK(oldsize - asize, 0));
        PUT(FTRP(ptr), PACK(oldsize - asize, 0));

        insert_free_block(ptr, GET_SIZE(HDRP(ptr)));
        coalesce(ptr);
        return newptr;
    }
    
    nextblk = NEXT_BLKP(ptr);
    /* Next block in memory is free and may be of use */
    if (!GET_ALLOC(HDRP(nextblk)) && nextblk != NULL){
        next_size = GET_SIZE(HDRP(nextblk));

        if (next_size + oldsize >= asize){
            remove_free_block(nextblk);

            if (next_size + oldsize - asize <= DSIZE){
                // Extra space cannot be used due to alignment--allocate it all
                PUT(HDRP(ptr), PACK(oldsize + DSIZE + next_size, 1));
                return ptr;
            }

            else {
                // Extra space can be used--coalesce the extra
                PUT(HDRP(ptr), PACK(asize + DSIZE, 1));
                PUT(FTRP(ptr), PACK(aszie + DSIZE, 1));
                newptr = ptr;
                ptr = NEXT_BLKP(newptr);
                PUT(HDRP(ptr), PACK(oldsize + next_size - asize, 0));
                PUT(FTRP(ptr), PACK(oldsize + next_size - asize, 0));
                insert_free_block(ptr, GET_SIZE(HDRP(ptr)));
                return newptr;
            }
        }
    }

    /* We must allocate a new block */
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    memcpy(newptr, ptr, oldsize);
    mm_free(ptr);
    return newptr;


}

/*
 * mm_checkheap - Check the heap for correctness
 */
void mm_checkheap(int verbose)
{
    checkheap();
}

/*
 * The remaining routines are internal helper routines
 */

/*
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words)
{
  char *bp;
  size_t size;

  /* Allocate an even number of words to maintain alignment */
  size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
  if ((long)(bp = mem_sbrk(size)) == -1)
      return NULL;

  /* Initialize free block header/footer and the epilogue header */
  PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
  PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */
  insert_free_block(bp, size);

  /* Coalesce if the previous block was free */
  return coalesce(bp);
}

/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    void *nxt = NULL;
    remove_free_block(bp);

    if ((csize - asize) >= (2*DSIZE)) {

        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        nxt = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        insert_free_block(nxt);
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
    return bp;
}

/*
 * find_fit - Find a fit for a block with asize bytes
 */
static void *find_fit(size_t asize){
    size_t size_check = asize;
    void *curr = seg_listp;

    for (int i = 0; i < NUM_SEG_LISTS; i++) {
      if ((i == NUM_SEG_LISTS - 1) || ((size <= 1) && (SEG_LIST(seg_listp, i) != NULL))) {
        curr = SEG_LIST(seg_listp, i);
        while ((curr != NULL) && asize > GET_SIZE(HDRP(curr))) {
            curr = GET_PREV_BLK(curr)
        }
        if (curr != NULL) {
          break
        }
      }
      size = size >> 1;
    }
    return curr;
}

static void insert_free_block(void *bp, size_t block_size){
    void *list_ptr = NULL;
    void *ins_loc = NULL;
    int list_ind = 0;

    // Find list for this block
    while ((list_ind < (NUM_SEG_LISTS - 1)) && (block_size > 1)) {
        block_size = block_size >> 1;
        list_number++;
    }

    list_ptr = SEG_LIST(seg_listp, list_ind);

    /* Find place to insert while maintaining sorting */
    while ((list_ptr != NULL) && (block_size > GET_SIZE(HDRP(list_ptr)))) {
        ins_loc = list_ptr;
        list_ptr = GET_PREV_BLK(list_ptr);
    }

    if (list_ptr) {
        if (ins_loc) {
            PUT_PTR(GET_PREV(insert_loc), bp);
            PUT_PTR(GET_NEXT(bp), ins_loc);
            PUT_PTR(GET_PREV(bp), list_ptr);
            PUT_PTR(GET_NEXT(list_ptr), bp); 
        }
        else {
            PUT_PTR(GET_NEXT(list_ptr), bp);
            PUT_PTR(GET_PREV(bp), list_ptr);
            PUT_PTR(GET_NEXT(bp), NULL);
            SEG_LIST(seg_listp, list_ind) = bp;
        }
    }

    else{
        if (ins_loc){
            PUT_PTR(GET_NEXT(bp), ins_loc);
            PUT_PTR(GET_PREV(ins_loc), bp);
            PUT_PTR(GET_PREV(bp), NULL); 
        }
        else {
            SEG_LIST(seg_listp, list_ind) = bp;
            PUT_PTR(GET_PREV(bp), NULL);
            PUT_PTR(GET_NEXT(bp), NULL);
        }
    }
    return;
}

static void remove_free_block(void *bp){
    int list_num = 0;
    size_t block_size = GET_SIZE(HDRP(bp));

    if (GET_NEXT_BLK(bp) == NULL) {
        while (list_number < (SEG_LIST_COUNT - 1) && block_size > 1) {
            block_size = block_size >> 1;
            list_number++;
        }
        SEG_LIST(seg_listp, list_number) = GET_PREV_BLK(bp);
        if (SEG_LIST(seg_listp, list_number) != NULL) {
            PUT_PTR(GET_NEXT(SEG_LIST(seg_listp, list_number)), NULL);
        }
        return;
    }
    

    PUT_PTR(GET_PREV(GET_NEXT_BLK(bp)), GET_PREV_BLK(bp)); 
    if (GET_PREV_BLK(bp) != NULL) {
        PUT_PTR(GET_NEXT(GET_PREV_BLK(bp)), GET_NEXT_BLK(bp));
    } 
}

static void checkblockfree(void *bp)
{
    if ((size_t)bp % 8)  //checks alignment
        printf("Error: %p is not doubleword aligned\n", bp);
        return 0;
    if (GET(HDRP(bp)) != GET(FTRP(bp)))  //checks that sizes match both directions
        printf("Error: header does not match footer\n");
        return 0;
    if (GET_ALLOC(bp) != 0) {
      printf("Error: block in free list at %p is not marked free")
    }
    if (GET_ALLOC(HDRP(bp)) == 0) {  //specific to free blocks
      if (GET_ALLOC(HDRP(GET(bp + WSIZE))) != 0) {  // check previous pointer points to free
        printf("Error: %p did not assign previous properly\n", );
        return 0;
      }
      if (GET_ALLOC(HDRP(GET(bp))) != 0) {  // check next pointer points to free
        printf("Error: %p next does not point to a free block\n");
        return 0;
      }
    }
}

static void checkblockall(void *curr, void *next)
{
    if ((size_t)curr % 8)  //checks alignment
        printf("Error: %p is not doubleword aligned\n", bp);
        return 0;
    if (GET(HDRP(curr)) != GET(FTRP(curr)))  //checks that sizes match both directions
        printf("Error: header does not match footer\n");
        return 0;
    if (GET_ALLOC(HDRP(curr)) == 0) {  //specific to free blocks
      if (GET_ALLOC(HDRP(GET(bp + WSIZE))) != 0) {  // check previous pointer points to free
        printf("Error: %p did not assign previous properly\n", );
        return 0;
      }
      if (GET_ALLOC(HDRP(GET(bp))) != 0) {  // check next pointer points to free
        printf("Error: %p next does not point to a free block\n");
        return 0;
      }
      if(GET_ALLOC(HDRP(next)) == 0) {  //checks that no two free blocks are next to each other
        printf("Error: two blocks did not coalesce correctly at %p");
        return 0;
      }
    }
}

/*
 * checkheap - Minimal check of the heap for consistency
 */
int checkheap(void)
{
    void *curr = heap_listp;  //points to current block
    void *next = NULL; //points to next block

    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header\n");  //make sure prologue is aligned and formatted
        return 0;
    checkblock(heap_listp);

    for (int i = 0; i < NUM_SEG_LISTS; i++) {    //check every block in our free lists
        curr = SEG_LIST(seg_listp, i);
        while (curr != NULL) {
          checkblockfree(curr);
          curr = GET_PREV_BLK(curr);
        }
    }

    for (curr = heap_listp; GET_SIZE(HDRP(curr)) > 0; curr = NEXT_BLKP(curr)) {
        next = NEXT_BLKP(curr);
        checkblockall(curr, next);
    }

    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("Bad epilogue header\n");  //make sure epilogue is aligned and formatted
        return 0;
    return 1
}
