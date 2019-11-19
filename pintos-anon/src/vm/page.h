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
  bool read_only;
  struct file *file;
  off_t offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  struct hash_elem elem; /* hash elem for hash table */
};

void page_table_init(struct hash *);
void page_table_free(struct hash *);
struct sup_page_table_entry *get_page_table_entry(void *);
bool page_add(void *);
bool page_fault_handler(bool, bool, bool, void *, void *);
bool vaddr_invalid_check(void *, void *);
bool grow_stack(void *);

#endif /* vm/page.h */