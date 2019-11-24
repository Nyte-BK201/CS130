#include "vm/swap.h"
#define NUM_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)

void
swap_init(){
    lock_init (&swap_lock);
    swap_block = NULL; 
    swap_bitmap = NULL;

    swap_block = block_get_role(BLOCK_SWAP);
    if(!swap_block){
        return;
    }

    swap_bitmap = bitmap_create(block_size(swap_block)/NUM_PER_PAGE);
    if(!swap_bitmap){
        return;
    }
    bitmap_set_all(swap_bitmap,false);
}

/* swap out a given frame from physical memory to device;
    panic if something goes wrong */
size_t
swap_out(void *frame){
    if(!swap_block || !swap_bitmap) PANIC("Virtual memory: swap init failed");
    // ASSERT(fte->frame!= NULL);
    lock_acquire(&swap_lock);

    // find a free place in bitmap to put frame
    size_t index = bitmap_scan_and_flip(swap_bitmap,0,1,false);
    if(index == BITMAP_ERROR) PANIC("Virtual memory: not enough disk to swap!\n");

    // write to disk sector by sector until full frame finished
    for(int i=0;i<NUM_PER_PAGE;i++){
        block_write(swap_block,index*NUM_PER_PAGE+i,frame+i*BLOCK_SECTOR_SIZE);
    }

    lock_release(&swap_lock);
    return index;
}

/* swap in a given frame from device to physical memory;
    panic if something goes wrong */
void
swap_in(void *frame, size_t index){
    if(!swap_block || !swap_bitmap) PANIC("Virtual memory: swap init failed");
    ASSERT(fte->frame != NULL);
    lock_acquire(&swap_lock);

    // find the given frame
    size_t ind = bitmap_scan_and_flip(swap_bitmap,index,1,true);
    if(ind == BITMAP_ERROR) PANIC("Virtual memory: swap out frame not found!\n");

    for(int i=0;i<NUM_PER_PAGE;i++){
        block_read(swap_block,index*NUM_PER_PAGE+i,frame+i*BLOCK_SECTOR_SIZE);
    }

    lock_release(&swap_lock);
}