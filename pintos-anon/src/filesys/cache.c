#include "filesys/cache.h"

struct cache_entry buffer_cache[BUFFER_CACHE_SIZE];

struct lock buffer_cache_lock;

int buffer_cache_put;   /* number of sectors in cache */

int buffer_cache_suggested_index;   /* the next spare index */

/* Initial the buffer cache. The buffer cache is 32KB and can store
   64 sectors with each sector 512B. */
void cache_init()
{
    lock_init(&buffer_cache_lock);
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        buffer_cache[i].dirty = false;
        buffer_cache[i].accessed = false; 
    }
    buffer_cache_put = 0;
    buffer_cache_suggested_index = 0;
}

/* Read a whole sector from disk to memory. Search it in buffer
   cache, if it is not in cache, read it from disk and put into
   cache, if cache is full then evict one sector. Finally read
   sector data into the given buffer. */
void cache_read(block_sector_t sector, void *buffer, off_t offset, size_t size)
{
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        if (buffer_cache[i].sector == sector){
            buffer_cache[i].accessed == true;
            memcpy(buffer,buffer_cache[i].data,BLOCK_SECTOR_SIZE);
            return;
        }
    }

    // the block is not in cache, find a spare cache or evict
    if (buffer_cache_put = BLOCK_SECTOR_SIZE){
        buffer_cache_evict();
    }

    // put sector into cache
    buffer_cache[buffer_cache_suggested_index].sector = sector;
    buffer_cache[buffer_cache_suggested_index].accessed = true;
    buffer_cache[buffer_cache_suggested_index].dirty = false;
    block_read(fs_device, sector, buffer_cache[buffer_cache_suggested_index].data);
    
    // memory read from cache
    buffer_cache[buffer_cache_suggested_index].accessed == true;
    memcpy(buffer, buffer_cache[buffer_cache_suggested_index].data, BLOCK_SECTOR_SIZE);
    
    buffer_cache_suggested_index++;
    buffer_cache_put = buffer_cache_put < BUFFER_CACHE_SIZE ? buffer_cache_put + 1 :BUFFER_CACHE_SIZE;
}

/* Write buffer from memory to disk. Here just write into cache
   and write to disk by cache_clear(). Search it in buffer cache,
   if it is not in cache, put the sector into cache. Write buffer
   into cache, mark as dirty. */
void cache_write(block_sector_t sector, void *buffer, off_t offset, size_t size)
{

}

void cache_clear()
{
    lock_acquire(&buffer_cache_lock);
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        if (buffer_cache[i].dirty == true){
            buffer_cache[i].dirty = false;
            block_write(fs_device, buffer_cache[i].sector,buffer_cache[i].data);
        }
        // remove sector and data


    }
    lock_release(&buffer_cache_lock);
}

/* Check if the required sector is in cache.
   Problem: null == 0, it may confused between null and id 0. */
block_sector_t cache_search(block_sector_t sector)
{
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
        if (buffer_cache[i].sector == sector){
            return i;
        }
    }
    return NULL;
}

/* Evict one sector in cache by clock algorithm, the function is
   called when the cache is full. After evict(), there should be
   63 sectors in cache and the next index to use is the position
   of the evicted one. */
void buffer_cache_evict()
{
    lock_acquire(&buffer_cache_lock);
    int i = 0;

    // clock algorithm
    while(true){
        if (buffer_cache[i].accessed){
            buffer_cache[i].accessed = false;
        }else{
            if (buffer_cache[i].dirty){
                block_write(fs_device,buffer_cache[i].sector,buffer_cache[i].data);
            }
            break;
        }
        i++;
        i = i % BUFFER_CACHE_SIZE;
    }

    // remove it

    buffer_cache_suggested_index = i;
    buffer_cache_put -= 1;
    lock_release(&buffer_cache_lock);
}