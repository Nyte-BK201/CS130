#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "vm/page.h"

void syscall_init (void);

void unmap_spte(struct mem_map_entry *, struct list_elem *, struct thread *);
struct lock file_lock;


#endif /* userprog/syscall.h */
