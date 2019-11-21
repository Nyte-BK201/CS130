#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "vm/frame.h"

struct sup_page_table_entry
{
  void *user_vaddr; /* user virtual address */
  bool writable;
  struct file *file;
  off_t offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  struct frame_table_entry *fte;
  struct hash_elem elem; /* hash elem for hash table */
};

struct mem_map_entry
{
  int file_size;
  struct file *file;
  uint32_t *user_vaddr;
  mapid_t mapid;
  struct list_elem elem;
};

void page_table_init(struct hash *);
void page_table_free(struct hash *);
struct sup_page_table_entry *get_page_table_entry(void *);
bool page_add(void *user_vaddr, struct sup_page_table_entry **spte,
              struct file *file, off_t ofs, uint32_t read_bytes,
              uint32_t zero_bytes, bool writable, struct frame_table_entry *);
bool page_fault_handler(bool, bool, bool, void *, void *);
bool vaddr_invalid_check(void *, void *);

#endif /* vm/page.h */