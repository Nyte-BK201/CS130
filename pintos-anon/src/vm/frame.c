#include "vm/frame.h"

// List of all frames.
static struct list frame_table;

// Lock to synchronize on frame table.
static struct lock frame_lock;

void
frame_init(void)
{
  lock_init(&frame_lock);
  list_init(&frame_table);
}

/* Allocate a page from USER_POOL, and add to frame_table. 
   Return the newly frame_table_entry. 
   Return NULL if allocate failed. */
struct frame_table_entry*
frame_allocate(enum palloc_flags flags)
{
  void *frame = NULL;

  if (!(flags & PAL_USER)){
    return NULL;
  }
  frame = palloc_get_page(flags);

  if(frame != NULL){
    return frame_add(frame);
  }else{
    // Fail to allocate a page, evict a frame from frame_table.
    frame_evict();

    frame = palloc_get_page(flags);
    if(frame == NULL) PANIC("Virtual memory: frame full, evict failed!\n");
    return frame_add(frame);
  }
  return NULL;
}

// Free the given frame and remove it from frame_table.
void
frame_free(struct frame_table_entry *fte)
{
  lock_acquire(&frame_lock);
  list_remove(&fte->elem);
  lock_release(&frame_lock);

  palloc_free_page(fte->frame);
  free(fte);
}

// Create a new frame_table_entry and add the given frame into frame_table.
struct frame_table_entry*
frame_add(void *frame)
{
  struct frame_table_entry *f = malloc(sizeof(struct frame_table_entry));
  f->frame = frame;
  // f->thread = thread_current();
  f->swap_bitmap_index = -1;

  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &f->elem);
  lock_release(&frame_lock);
  return f;
}

/* Choose a frame to evict from the frame_table. 
   Implement a global page replacement algorithm that approximates LRU. */
void
frame_evict(void)
{
  lock_acquire(&frame_lock);
  

  lock_release(&frame_lock);
}