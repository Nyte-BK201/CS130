#include "vm/swap.h"
#define NUM_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)

void
swap_init(){
    lock_init (&swap_lock);
    swap_block = block_get_role(BLOCK_SWAP);
    swap_bitmap = bitmap_create(block_size(swap_block)/NUM_PER_PAGE);

    if(!swap_block || !swap_bitmap){
        PANIC("Virtual memory: swap init failed!\n");
    }
}

/* swap out a given frame from physical memory to device;
    panic if something goes wrong */
void
swap_out(struct frame_table_entry *frame_entry){
    ASSERT(frame_entry->frame!= NULL);
    ASSERT(frame_entry->swap_bitmap_index == -1);
    lock_acquire(&swap_lock);

    // find a free place in bitmap to put frame
    size_t index = bitmap_scan_and_flip(swap_bitmap,0,1,false);
    if(index == BITMAP_ERROR) PANIC("Virtual memory: not enough disk to swap!");

    // write to disk sector by sector until full frame finished
    for(int i=0;i<NUM_PER_PAGE;i++){
        block_write(swap_block,index*NUM_PER_PAGE+i,frame_entry->frame+i*NUM_PER_PAGE);
    }
    frame_entry->swap_bitmap_index = index;

    lock_release(&swap_lock);
}

/* swap in a given frame from device to physical memory;
    panic if something goes wrong */
void
swap_in(struct frame_table_entry *frame_entry){
    ASSERT(frame_entry->frame != NULL);
    ASSERT(frame_entry->swap_bitmap_index != -1);
    lock_acquire(&swap_lock);

    // find the given frame
    size_t index = bitmap_scan_and_flip(swap_bitmap,frame_entry->swap_bitmap_index,1,true);
    if(index == BITMAP_ERROR) PANIC("Virtual memory: swap out frame not found!");

    for(int i=0;i<NUM_PER_PAGE;i++){
        block_read(swap_block,index*NUM_PER_PAGE+i,frame_entry->frame+i*NUM_PER_PAGE);
    }
    frame_entry->swap_bitmap_index = -1;

    lock_release(&swap_lock);
}