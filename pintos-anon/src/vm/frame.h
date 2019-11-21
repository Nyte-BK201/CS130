#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include <list.h>
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "vm/page.h"

struct frame_table_entry
{
  void *frame;
  struct thread *thread;
  struct sup_page_table_entry *spte;
  size_t swap_bitmap_index; // denote the index of bitmap if swapped
  struct list_elem elem;
};

void frame_init(void);
struct frame_table_entry *frame_allocate(enum palloc_flags flags, struct sup_page_table_entry *spte);
void frame_free(struct frame_table_entry *);
void frame_remove_from_list(struct frame_table_entry *);
void frame_add_to_list(struct frame_table_entry *);
struct frame_table_entry *frame_add(void *, struct sup_page_table_entry *);

#endif /* vm/frame.h */