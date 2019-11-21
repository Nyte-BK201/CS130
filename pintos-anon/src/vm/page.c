#include "vm/page.h"
#include <string.h>
#include "threads/vaddr.h"
#include "devices/timer.h"

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

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

// Get a supplymental page from hash table and destroy it.
static void page_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, elem);
  if(spte->fte != NULL){
    // in physical memory
    if(spte->fte->swap_bitmap_index == -1){
      frame_free(spte->fte);
      pagedir_clear_page(thread_current()->pagedir, spte->user_vaddr);
    }else{
    // in swap slot 
      swap_in(spte->fte);
      palloc_free_page(spte->fte->frame);
      pagedir_clear_page(thread_current()->pagedir, spte->user_vaddr);
    }
    free(spte->fte);
  }
  free(spte);
}

// Init a page table, called when a thread init.
void
page_table_init(struct hash *sup_page_table)
{
  hash_init(sup_page_table, page_hash_func, page_less_func, NULL);
}

// Destroy a page table(including all fte), called when a thread exit.
void
page_table_free(struct hash *sup_page_table)
{
  hash_destroy(sup_page_table, page_destroy_func);
}

/* Get a supplymentary page table entry by the given user virtual address.
   Return NULL if not in page_table. */
struct sup_page_table_entry *
get_page_table_entry(void *user_vaddr)
{
  struct sup_page_table_entry sup_pt_entry;

  // Find the hash elem by key = user_vaddr
  sup_pt_entry.user_vaddr = user_vaddr - ((unsigned)user_vaddr % 4096);
  struct hash_elem *e = hash_find(&(thread_current()->sup_page_table), &(sup_pt_entry.elem));

  // Return the entry or NULL if it isn't found.
  return e == NULL ? NULL : hash_entry(e, struct sup_page_table_entry, elem);
}

/* Add a page into current thread's sup_page_table. Return false if 
   an equal element is already in the hash table, then it will not be
   inserted. */
bool page_add(void *user_vaddr, struct sup_page_table_entry *spte,
              struct file *file, off_t ofs, uint32_t read_bytes,
              uint32_t zero_bytes, bool writable, struct frame_table_entry *fte)
{
  struct thread *cur_thread = thread_current();

  // if(!sup_pt_entry) return false;

  spte->user_vaddr = user_vaddr;
  spte->file = file;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  spte->offset = ofs;
  spte->fte = fte;
  spte->writable = writable;

  // Insert it into hash table, hash_insert() returns the old one with the same hash value.
  if(hash_insert(&cur_thread->sup_page_table, &spte->elem) == NULL){
    return true;
  }else{
    return false;
  }
}

/* Determine if the address requested by the process is valid to allocate a new
   page for the process. If yes, allocate and install a new page for the process
   and allow the process to continue as normal (rather than killing the thread) 
   after the page fault handler finishes. Return true if it doesn't need kill.*/
bool
page_fault_handler(bool not_present, bool write, bool user, void *fault_addr, void *esp)
{
  // invalid address 
  if (!user || vaddr_invalid_check(fault_addr, esp) || !not_present){
    return false;
  }

  struct sup_page_table_entry *sup_pt_entry = get_page_table_entry(fault_addr);
  // A: a page fault with an address not in the virtual page table
  if (sup_pt_entry == NULL){
    if (fault_addr < (esp - 32)){
      /* A1: Invalid address usage.
         An address that does not appear to be a stack access.
        */
      return false;
    }else{
      /* A2: valid. Try stack growth and return */
      return grow_stack(pg_round_down(fault_addr));
    }

  // B: a page fault with an address in the virtual page table but evicted
  }else{
    /* B1: lazy load/ the frame is evicted but not in disk.
       We allocate a frame and read the file into it again.
      */
    if(sup_pt_entry->fte == NULL || sup_pt_entry->fte->swap_bitmap_index == -1){
      return lazy_load(sup_pt_entry);
    }else{
    /* B2: swapped. A modified page in the disk, we should swap it back */
      return swap_page(sup_pt_entry);
    }
  }

  // situation not matched to any of the above kinds
  return false;
}

/* Check if a virtual address is invalid when a page fault occurs.
   Called by page fault handler. Return true if the address is invalid */
bool
vaddr_invalid_check(void *fault_addr, void *esp)
{
  if (fault_addr == NULL) return true;
  if (fault_addr >= PHYS_BASE) return true; /* A kernel address */
  if (fault_addr < 0x0804800) return true; /* An address below user stack */
  return false;
}

// Give more memory (one page) to the user. Return true if success.
bool
grow_stack(void *user_vaddr)
{
  struct sup_page_table_entry * spte = malloc(sizeof(struct sup_page_table_entry));
  struct frame_table_entry *fte = frame_allocate(PAL_ZERO | PAL_USER, spte);

  // Add a page into page table with no file and writable.
  if(!page_add(user_vaddr, spte, NULL, 0, 0, 0, 1, fte)){
    frame_free(fte);
    free(spte);
    free(fte);
    return false;
  }
  
  return install_page(spte->user_vaddr,spte->fte->frame,spte->writable);
}

bool
lazy_load(struct sup_page_table_entry *spte){
  // detele old fte if exsit, and allocate a new one 
  if(!spte->fte) free(spte->fte);
  struct frame_table_entry *fte = frame_allocate(PAL_USER, spte);
  if(fte == NULL) return false;

  spte->fte = fte;

  spte->file = file_reopen(spte->file);
  file_read_at(spte->file, fte->frame, spte->read_bytes, spte->offset);

  // Make the spare part of frame to be all zero.
  memset(fte->frame + (spte->read_bytes), 0, spte->zero_bytes);

  return install_page(spte->user_vaddr,spte->fte->frame,spte->writable);
}

bool
swap_page(struct sup_page_table_entry *spte){
  spte->fte->frame = palloc_get_page(PAL_USER);

  swap_in(spte->fte);
  frame_add_to_list(spte->fte);

  return install_page(spte->user_vaddr,spte->fte->frame,spte->writable);
}
