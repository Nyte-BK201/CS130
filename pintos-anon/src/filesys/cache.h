#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

#define BUFFER_CACHE_SIZE 64

struct cache_entry
{
    block_sector_t sector;             /* sector number of disk location */
    uint8_t data[BLOCK_SECTOR_SIZE];   /* data of the sector */
    bool dirty;                        
    bool accessed;
};

void cache_init();
void cache_read(block_sector_t, void *, off_t, size_t);
void cache_write(block_sector_t, void *, off_t, size_t);
void cache_clear();
void cache_read_ahead();

#endif /* filesys/cache.h */