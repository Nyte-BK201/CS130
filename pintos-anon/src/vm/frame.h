#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
// #include <list.h>
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"

struct frame_table_entry
{
  void *frame;
  struct thread *thread;
  size_t swap_bitmap_index; // denote the index of bitmap if swapped
  struct list_elem elem;
};

void frame_init(void);
void *frame_allocate(enum palloc_flags flags);
void frame_free(void *);

void frame_add(void *);
void *frame_evict(void);

#endif /* vm/frame.h */