#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"

struct sup_page_table_entry{
    uint32_t* user_vaddr; /* user virtual address */

    struct hash_elem elem; /* hash elem for hash table */
};


#endif /* vm/page.h */