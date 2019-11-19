#include "page.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "lib/kernel/hash.h"
#include "frame.h"
#include "threads/palloc.h"

static unsigned
page_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *sup_pt_entry = hash_entry(e, struct sup_page_table_entry, elem);
  return hash_int((int)sup_pt_entry->user_vaddr);
}

// Return true if a's user virtual address less than b's user virtual address.
static bool
page_less_func(const struct hash_elem *a,
                           const struct hash_elem *b, void *aux UNUSED)
{
  struct sup_page_table_entry *sup_pt_entry_a = hash_entry(a, struct sup_page_table_entry, elem);
  struct sup_page_table_entry *sup_pt_entry_b = hash_entry(b, struct sup_page_table_entry, elem);
  return sup_pt_entry_a->user_vaddr < sup_pt_entry_b->user_vaddr;
}

static void page_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *sup_pt_entry = hash_entry(e, struct sup_page_table_entry, elem);
  free(sup_pt_entry);
}

void
page_table_init(struct hash *sup_page_table)
{
  hash_init(sup_page_table, page_hash_func, page_less_func, NULL);
}

void
page_table_free(struct hash *sup_page_table)
{
  hash_destroy(sup_page_table, page_destroy_func);
}

// Get a supplymentary page table entry by the given user virtual address.
struct sup_page_table_entry *
get_page_table_entry(void *user_vaddr)
{
  struct hash cur_page_table = thread_current()->sup_page_table;
  struct sup_page_table_entry *sup_pt_entry;

  // Find the hash elem by key = user_vaddr
  sup_pt_entry->user_vaddr = user_vaddr;
  struct hash_elem *e = hash_find(&cur_page_table, &sup_pt_entry->elem);

  struct sup_page_table_entry *temp = hash_entry(e,struct sup_page_table_entry, elem);
  return temp;
}

/* Add a page into current thread's sup_page_table. Return false if 
   an equal element is already in the hash table, then it will not be
   inserted. */
bool
page_add(void *user_vaddr)
{
  struct thread *cur_thread = thread_current();
  struct sup_page_table_entry *sup_pt_entry = malloc(sizeof(struct sup_page_table_entry));

  // if(!sup_pt_entry) return false;

  sup_pt_entry->user_vaddr = user_vaddr;
  sup_pt_entry->access_time = timer_ticks();
  sup_pt_entry->accessed = false;
  sup_pt_entry->dirty = false;

  if(hash_insert(&cur_thread->sup_page_table, &sup_pt_entry->elem) == NULL){
    return true;
  }else{
    free(sup_pt_entry);
    return false;
  }
}

bool
page_fault_handler(bool not_present, bool write, bool user, void *fault_addr, void *esp)
{
  bool kill_process = false; /* True: the process should be killed, false: proceed as normal */
  if (!user || vaddr_invalid_check(fault_addr, esp) || !not_present){
    kill_process = true;
  }else{
    struct sup_page_table_entry *sup_pt_entry = get_page_table_entry(fault_addr);
    if (sup_pt_entry == NULL){
      kill_process = grow_stack(fault_addr);
    }
  }
  return kill_process;

}

/* Check if a virtual address is invalid when a page fault occurs.
   Called by page fault handler. Return true if the address is invalid */
bool
vaddr_invalid_check(void *fault_addr, void *esp)
{
  if (fault_addr == NULL) return true;
  if (fault_addr >= PHYS_BASE) return true; /* A kernel address */
  if (fault_addr < 0x0804800) return true; /* An address below user stack */
  if (fault_addr < (esp - 32)) return true; /* An address that does not appear to be a stack access.*/
  return false;
}

// Return false if success
bool
grow_stack(void *user_vaddr)
{
  // void *new_frame = frame_allocate(PAL_USER | PAL_ZERO);
  page_add(user_vaddr);
  


  return false;
}
