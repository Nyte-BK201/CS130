#include "page.h"
#include "threads/thread.h"
#include "devices/timer.h"

void page_table_init(struct list *sup_page_table)
{
  list_init(sup_page_table);
}

void page_table_free(struct list *sup_page_table)
{
  for (struct list_elem *e = list_begin(sup_page_table);
        e != list_end(sup_page_table);
        e = list_next(e)){

    struct sup_page_table_entry *sup_pt_entry = list_entry(e, struct sup_page_table_entry, elem);
    list_remove(e);
    free(sup_pt_entry);
  }
  free(sup_page_table);
}

// Get a supplymentary page table entry by the given user virtual address.
struct sup_page_table_entry *get_page_table_entry(void *user_vaddr)
{
  struct list cur_page_table = thread_current()->sup_page_table;
  for (struct list_elem *e = list_begin(&cur_page_table);
       e != list_end(&cur_page_table);
       e = list_next(e)){
    struct sup_page_table_entry *sup_pt_entry = list_entry(e, struct sup_page_table_entry, elem);

    if (sup_pt_entry->user_vaddr == user_vaddr){
      return sup_pt_entry;
    }
  }
  return NULL;
}

// Add a page into current thread's sup_page_table.
void page_add(void *user_vaddr)
{
  struct thread *cur_thread = thread_current();
  struct sup_page_table_entry *sup_pt_entry = malloc(sizeof(struct sup_page_table_entry));
  sup_pt_entry->user_vaddr = user_vaddr;
  sup_pt_entry->access_time = timer_ticks();
  sup_pt_entry->accessed = false;
  sup_pt_entry->dirty = false;

  list_push_back(&cur_thread->sup_page_table, &sup_pt_entry->elem);
}
