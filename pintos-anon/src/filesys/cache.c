#include "filesys/cache.h"
#include "devices/timer.h"
#include "threads/thread.h"

struct cache_entry* buffer_cache[BUFFER_CACHE_SIZE];

struct lock buffer_cache_lock;

static void cache_clear_periodic (void *aux UNUSED);

/* Initial the buffer cache. The buffer cache is 32KB and can store
   64 sectors with each sector 512B. */
void cache_init()
{
    lock_init(&buffer_cache_lock);
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        buffer_cache[i] = malloc(sizeof(struct cache_entry));
        buffer_cache[i]->accessed = false;
        buffer_cache[i]->dirty = false;
        buffer_cache[i]->sector = -1;
        lock_init(&buffer_cache[i]->cache_lock);
    }
    thread_create("write_behind", PRI_DEFAULT, cache_clear_periodic, NULL);
}

/* Read a whole sector from disk to memory. Search it in buffer
   cache, if it is not in cache, read it from disk and put into
   cache, if cache is full then evict one sector. Finally read
   sector data into the given buffer. */
void cache_read(block_sector_t sector, void *buffer, off_t offset, size_t size)
{
    int cache_index = cache_search(sector);

    lock_acquire(&buffer_cache_lock);
    // not in cache
    if (cache_index == -1){
        // get a free index
        cache_index = cache_evict();

        // put sector into cache
        lock_acquire(&buffer_cache[cache_index]->cache_lock);
        buffer_cache[cache_index]->sector = sector;
        buffer_cache[cache_index]->dirty = false;
        block_read(fs_device, sector, buffer_cache[cache_index]->data);
        lock_release(&buffer_cache[cache_index]->cache_lock);
    }
    
    // memory read from cache
    buffer_cache[cache_index]->accessed == true;
    memcpy(buffer, buffer_cache[cache_index]->data + offset, size);
    lock_release(&buffer_cache_lock);
}

/* Write buffer from memory to disk. Here just write into cache
   and write to disk by cache_clear(). Search it in buffer cache,
   if it is not in cache, put the sector into cache. Write buffer
   into cache, mark as dirty. */
void cache_write(block_sector_t sector, void *buffer, off_t offset, size_t size)
{
    int cache_index = cache_search(sector);

    lock_acquire(&buffer_cache_lock);
    // not in cache
    if (cache_index == -1){
        // get a free index
        cache_index = cache_evict();

        // put sector into cache
        lock_acquire(&buffer_cache[cache_index]->cache_lock);
        buffer_cache[cache_index]->sector = sector;
        buffer_cache[cache_index]->dirty = false;
        block_read(fs_device, sector, buffer_cache[cache_index]->data);
        lock_release(&buffer_cache[cache_index]->cache_lock);
    }
    
    // memory write to cache
    buffer_cache[cache_index]->accessed == true;
    buffer_cache[cache_index]->dirty = true;
    memcpy(buffer_cache[cache_index]->data + offset, buffer, size);
    lock_release(&buffer_cache_lock);
}

/* Write back all changes to block, reset the cache to 
   the initial state. */
void cache_clear()
{
    lock_acquire(&buffer_cache_lock);
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        if (buffer_cache[i]->dirty == true){
            buffer_cache[i]->dirty = false;
            block_write(fs_device, buffer_cache[i]->sector,buffer_cache[i]->data);
        }
        // buffer_cache[i]->accessed = false;
    }
    lock_release(&buffer_cache_lock);
}

/* Check if the required sector is in cache.
   Return the index or -1 if not found. */
int cache_search(block_sector_t sector)
{
    lock_acquire(&buffer_cache_lock);
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        if (buffer_cache[i]->sector == sector){
            lock_release(&buffer_cache_lock);
            return i;
        }
    }
    lock_release(&buffer_cache_lock);
    return -1;
}

/* Return an index where can put a new sector. When the cache is 
   not full, it returns a spare index; when the cache is full, 
   evict on sector by clock algorithm. There is no need to remove
   the evicted sector since we can just rewrite it. 
   This function is called when putting a new sector into cache. */
int cache_evict()
{
    lock_acquire(&buffer_cache_lock);
    int i = 0;

    // clock algorithm
    while(true){
        if (buffer_cache[i]->accessed){
            buffer_cache[i]->accessed = false;
        }else{
            if (buffer_cache[i]->dirty){
                block_write(fs_device,buffer_cache[i]->sector,buffer_cache[i]->data);
            }
            break;
        }
        i++;
        i = i % BUFFER_CACHE_SIZE;
    }
    
    lock_release(&buffer_cache_lock);
    
    return i;
}

void cache_clear_periodic (void *aux UNUSED)
{
	while (true){
		timer_sleep(TIMER_FREQ);
		cache_clear();
	}
}