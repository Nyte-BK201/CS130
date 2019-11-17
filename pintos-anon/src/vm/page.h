#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "lib/kernel/list.h"

struct sup_page_table_entry
{
  uint32_t *user_vaddr;
  uint64_t access_time;
  bool dirty;
  bool accessed;
  struct list_elem elem;
};

void page_table_init(struct list *);
void page_table_free(struct list *);
struct sup_page_table_entry *get_page_table_entry(void *);
void page_add(void *);

#endif /* vm/page.h */