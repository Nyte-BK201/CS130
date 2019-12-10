#include "filesys/cache.h"
#include "devices/timer.h"
#include "threads/thread.h"

struct cache_entry* buffer_cache[BUFFER_CACHE_SIZE];
struct lock buffer_cache_lock;

struct list read_ahead_list;
struct lock read_ahead_lock;
struct condition read_ahead_cond;

static void cache_clear_periodic_background(void *aux UNUSED);
static void cache_read_ahead_background(void *aux UNUSED);

/* Initial the buffer cache. The buffer cache is 32KB and can store
   64 sectors with each sector 512B. */
void cache_init()
{
    lock_init(&buffer_cache_lock);
    list_init(&read_ahead_list);
    lock_init(&read_ahead_lock);
    cond_init(&read_ahead_cond);

    // init every cache entry
    for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
        buffer_cache[i] = malloc(sizeof(struct cache_entry));
        buffer_cache[i]->accessed = false;
        buffer_cache[i]->dirty = false;
        buffer_cache[i]->sector = -1;
        lock_init(&buffer_cache[i]->cache_lock);
    }

    thread_create("write_behind", PRI_DEFAULT, cache_clear_periodic_background, NULL);
    thread_create("read_ahead", PRI_DEFAULT, cache_read_ahead_background, NULL);
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
    
    return i;
}

/* Write back all changes every second and reset to initial state.
   This function runs in the background. TIMER_FREQ defines ticks
   per second in timer.h. */
static void cache_clear_periodic_background(void *aux UNUSED)
{
	while (true){
		timer_sleep(TIMER_FREQ);
		cache_clear();
	}
}

/* Commit a request to read ahead the next sector. Push the sector
   into read_ahead_list and signal the background thread to wake up
   and read it. 
   Attention: parameter passed in should be the next sector. */
void cache_read_ahead(block_sector_t sector)
{
    lock_acquire(&read_ahead_lock);
    struct read_ahead_entry *rae = malloc(sizeof(struct read_ahead_entry));
    rae->sector = sector;
    list_push_back(&read_ahead_list,&rae->elem);
    cond_signal(&read_ahead_cond, &read_ahead_lock);
    lock_release(&read_ahead_lock);
}

/* Handle the read_ahead requests in read_ahead_list in the background.
   Wait for read_ahead_cond until there is request, then take it out and
   read into cache. */
static void cache_read_ahead_background(void *aux UNUSED)
{
    while(true){
        lock_acquire(&read_ahead_lock);
        
        // wait the condition until the list is not empty
        while(list_empty(&read_ahead_list)){
            cond_wait(&read_ahead_cond,&read_ahead_lock);
        }

        // get the sector id
        struct list_elem *e = list_pop_front(&read_ahead_list);
        struct read_ahead_entry *rae = list_entry(e, struct read_ahead_entry, elem);
        lock_release(&read_ahead_lock);

        // read the sector into cache, buffer doesn't need here.
        cache_read(rae->sector,NULL,0,BLOCK_SECTOR_SIZE);
        free(rae);
    }
}

