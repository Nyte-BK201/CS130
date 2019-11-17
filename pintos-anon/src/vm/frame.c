#include "threads/palloc.h"
#include "vm/frame.h"

// List of all frames.
static struct list frame_table;

// Lock to synchronize on frame table.
static struct lock frame_lock;

void frame_init(void)
{
  lock_init(&frame_lock);
  list_init(&frame_table);
}

/* Allocate a page from USER_POOL, and add to frame_table. 
   Return the newly allocated frame. */
void *frame_allocate(enum palloc_flags flags)
{
  void *frame = NULL;

  if (!(flags & PAL_USER)){
    return NULL;
  }
  frame = palloc_get_page(flags);

  if(frame){
    frame_add(frame);
  }else{
    // Fail to allocate a page, evict a frame from frame_table.
    frame_evict();
  }

  return frame;
}

// Free the given frame and remove it from frame_table.
void frame_free(void *frame)
{
  lock_acquire(&frame_lock);

  // Traverse the frame_table and match.
  for (struct list_elem *e = list_begin(&frame_table);
    e != list_end(&frame_table);
    e = list_next(e)){
    struct frame_table_entry *frame_temp = list_entry(e, struct frame_table_entry, elem);

    if (frame_temp->frame == frame){
      list_remove(e);
      free(frame_temp);
      palloc_free_page(frame);
      break;
    }
  }

  lock_release(&frame_lock);
}

// Add the given frame into frame_table.
void frame_add(void *frame)
{
  struct frame_table_entry *f = malloc(sizeof(struct frame_table_entry));
  f->frame = frame;
  f->thread = thread_current();

  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &f->elem);
  lock_release(&frame_lock);
}

/* Choose a frame to evict from the frame_table. 
   Implement a global page replacement algorithm that approximates LRU. */
void *frame_evict(void)
{

}