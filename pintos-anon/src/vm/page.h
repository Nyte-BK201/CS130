#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"

struct sup_page_table_entry
{
  uint32_t *user_vaddr; /* user virtual address */
  uint64_t access_time;
  bool dirty;
  bool accessed;
  struct hash_elem elem; /* hash elem for hash table */
};

void page_table_init(struct hash *);
void page_table_free(struct hash *);
struct sup_page_table_entry *get_page_table_entry(void *);
bool page_add(void *);


#endif /* vm/page.h */