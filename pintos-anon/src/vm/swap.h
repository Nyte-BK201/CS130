#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "vm/frame.h"

struct lock swap_lock;
struct block *swap_block;
struct bitmap *swap_bitmap;

void swap_init();
void swap_in(struct frame_table_entry *, size_t index);
size_t swap_out(struct frame_table_entry *);

#endif /* vm/swap.h */