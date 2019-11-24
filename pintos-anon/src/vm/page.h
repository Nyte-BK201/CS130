#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "vm/frame.h"

enum spte_type{
  ERROR = 0,
  SWAP = 1,
  LAZY = 2, 
  MMAP = 3
};

struct sup_page_table_entry
{
  uint32_t *user_vaddr; /* user virtual address */
  bool writable;
  struct file *file;
  off_t offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  enum spte_type type;
  bool pinned;
  size_t swap_bitmap_index; // denote the index of bitmap if swapped
  struct hash_elem elem; /* hash elem for hash table */
};

struct mem_map_entry
{
  struct sup_page_table_entry *spte;
  mapid_t mapid;
  struct list_elem elem;
};

void page_table_init(struct hash *);
void page_table_free(struct hash *);
struct sup_page_table_entry *get_page_table_entry(void *);
bool page_add(void *, struct sup_page_table_entry *,
              struct file *, off_t, uint32_t,
              uint32_t, bool);
bool page_fault_handler(bool, bool, bool, void *, void *);
bool vaddr_invalid_check(void *, void *);
bool grow_stack(void *);
bool lazy_load(struct sup_page_table_entry *);
bool swap_page(struct sup_page_table_entry *);

#endif /* vm/page.h */