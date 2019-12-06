#include "filesys/cache.h"

struct cache_entry buffer_cache[BUFFER_CACHE_SIZE];

struct lock buffer_cache_lock;

/* Initial the buffer cache. The buffer cache is 32KB and can store
   64 sectors with each sector 512B. */
void cache_init()
{
    lock_init(&buffer_cache_lock);
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        buffer_cache[i].dirty = false;
        buffer_cache[i].accessed = false; 
    }
}

/* Read a whole sector from disk to memory. Search it in buffer
   cache, if it is not in cache, read it from disk and put into
   cache, if cache is full then evict one sector. Finally read
   sector data into the given buffer. */
void cache_read(block_sector_t sector, void *buffer, off_t offset, size_t size)
{

}

/* Write buffer from memory to disk. Here just write into cache
   and write to disk by cache_clear(). Search it in buffer cache,
   if it is not in cache, put the sector into cache. Write buffer
   into cache, mark as dirty. */
void cache_write(block_sector_t sector, void *buffer, off_t offset, size_t size)
{

}

