#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "vm/frame.h"

struct lock swap_lock;      /* a lock to edit disk */
struct block *swap_block;   /* the device to use in vm */
struct bitmap *swap_bitmap; /* the bitmap to indicate free sector in device */

void swap_init();
void swap_in(void *, size_t index);
size_t swap_out(void *);

#endif /* vm/swap.h */