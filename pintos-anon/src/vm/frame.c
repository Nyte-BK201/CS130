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
frame_allocate(enum palloc_flags flags, struct sup_page_table_entry *spte)
{
  void *frame = NULL;

  if (!(flags & PAL_USER)){
    return NULL;
  }
  frame = palloc_get_page(flags);

  if(frame != NULL){
    return frame_add(frame,spte);
  }else{
    // Fail to allocate a page, evict a frame from frame_table.
    frame_evict();

    frame = palloc_get_page(flags);
    if(frame == NULL) PANIC("Virtual memory: frame full, evict failed!\n");
    return frame_add(frame,spte);
  }
  return NULL;
}

/* Free the given frame and remove it from frame_table.
  IMPORTANT: This function will not free fte;
  !!!! We should manually free fte !!!!
  Owner thread is in charge of fte and a fte should be freed when corresponding
  spte is freed. We need swap_bitmap_index in fte to indicate if the frame is
  swapped or it should be loaded from file. 
  */
void
frame_free(struct frame_table_entry *fte)
{
  lock_acquire(&frame_lock);
  list_remove(&fte->elem);
  lock_release(&frame_lock);

  palloc_free_page(fte->frame);
  // free(fte);
}

// Create a new frame_table_entry and add the given frame into frame_table.
struct frame_table_entry*
frame_add(void *frame, struct sup_page_table_entry *spte)
{
  struct frame_table_entry *f = malloc(sizeof(struct frame_table_entry));
  f->frame = frame;
  f->thread = thread_current();
  f->swap_bitmap_index = -1;
  f->spte = spte;

  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &f->elem);
  lock_release(&frame_lock);
  return f;
}

/* Choose a frame to evict from the frame_table. 
   Implement a global page replacement algorithm that approximates LRU. 
   Evicted frame will be removed from list but will not be freed. Onwer thread
   will do this job when exiting.
   */
void
frame_evict(void)
{
  bool found = false;
  lock_acquire(&frame_lock);

  while(!found){
    struct list_elem *e = list_head(&frame_table);
    while((e = list_next (e)) != list_end (&frame_table)){
      struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);

      uint32_t *pd = fte->thread->pagedir;
      void *upage = fte->spte->user_vaddr;

      // not a desired victim
      if(pagedir_is_accessed(pd,upage)){
        pagedir_set_accessed(pd,upage,false);
      }else{
      // vicvim found

        // check if vicvim needs swap or mmap needs write back
        if(pagedir_is_dirty(pd,upage)){
          // mmap
          if(fte->spte->file != NULL){
            file_write_at(fte->spte->file,fte->frame,fte->spte->read_bytes,fte->spte->offset);
          }else{
          // swap  
            swap_out(fte);
          }
        }

        pagedir_clear_page(pd,upage);
        frame_free(fte);

        found = true;
        break;
      }
    }
  }

  lock_release(&frame_lock);
}