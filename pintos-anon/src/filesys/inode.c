#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define LV0_INDEX 123
#define LV1_INDEX 1
#define LV2_INDEX 1

/* LV0 contains 123*512 Bytes
  LV1 contains 1*128*512 Bytes
  LV2 contains 1*128*LV1 Bytes
  */
static int32_t LV0_SIZE = (LV0_INDEX * BLOCK_SECTOR_SIZE);
static int32_t LV1_SIZE = ((BLOCK_SECTOR_SIZE / 4) * BLOCK_SECTOR_SIZE);
static int32_t LV2_SIZE = ((BLOCK_SECTOR_SIZE / 4) * (BLOCK_SECTOR_SIZE / 4) * BLOCK_SECTOR_SIZE); 

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sectors[125];        /* Lv0 0~122, lv1 123, lv2 124 */
    uint32_t isdir;                     /* 0 file, 1 dir */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock lock;             /* lock to manipulate this inode */
    struct inode_disk data;             /* Inode content. */
  };

static block_sector_t
lv0_translate(const struct inode_disk *inode_disk, off_t pos){
  return inode_disk->sectors[pos/BLOCK_SECTOR_SIZE];
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  // longer than this file
  if(pos>inode->data.length) return -1;

  // minus 1 to move 512th byte to sector 0
  // pos -= 1;
  if(pos<LV0_SIZE)return lv0_translate(&inode->data,pos);
  pos -= LV0_SIZE;

  // level 1: read level 1 into memory and translate with lv0
  if(pos<LV1_SIZE){
    struct inode_disk *lv1_sector = malloc(sizeof(struct inode_disk));
    cache_read(inode->data.sectors[LV0_INDEX],lv1_sector,0,BLOCK_SECTOR_SIZE);
    block_sector_t ans = lv0_translate(lv1_sector,pos);
    free(lv1_sector);
    return ans;
  }
  pos -= LV1_SIZE;
  
  // level 2: read level2 and relocate level 1 block then translate
  if(pos<LV2_SIZE){
    struct inode_disk *inode_disk = malloc(sizeof(struct inode_disk));
    cache_read(inode->data.sectors[LV0_INDEX+LV1_INDEX],inode_disk,0,BLOCK_SECTOR_SIZE);
    cache_read(inode_disk->sectors[pos/LV1_SIZE],inode_disk,0,BLOCK_SECTOR_SIZE);
    pos-=(pos/LV1_SIZE)*LV1_SIZE;
    block_sector_t ans = lv0_translate(inode_disk,pos);
    free(inode_disk);
    return ans;
  }
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock; /* lock to sync open_inodes */

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init(&open_inodes_lock);
}

/*  allocate a sector in disk and fill with zeros to use
  */
static block_sector_t
sector_allocate_disk(){
  block_sector_t new_sec;
  char *sec_fill = malloc(BLOCK_SECTOR_SIZE);
  if(!free_map_allocate(1,&new_sec) || sec_fill==NULL) return -1;
  memset(sec_fill,0,BLOCK_SECTOR_SIZE);

  cache_write(new_sec,sec_fill,0,BLOCK_SECTOR_SIZE);
  free(sec_fill);
  return new_sec;
}

/* add new_sec to lv0
  */
static bool
sector_allocate_lv0(struct inode_disk *inode_disk, block_sector_t new_sec, off_t pos){
  // -1 for moving 512th byte to sector[0]
  inode_disk->sectors[pos/BLOCK_SECTOR_SIZE] = new_sec;
  return true;
}

/* add new_sec to head's lv1
  */
static bool
sector_allocate_lv1(struct inode_disk *head, block_sector_t new_sec, off_t pos){
  struct inode_disk *lv1_sector = malloc(sizeof(struct inode_disk));
  // check if lv1 sector is allocated, if not allocate one
  if(head->length < LV0_SIZE){
    block_sector_t lv1 = sector_allocate_disk();
    head->sectors[LV0_INDEX] = lv1;
    cache_read(lv1,lv1_sector,0,BLOCK_SECTOR_SIZE);
  }else{
    cache_read(head->sectors[LV0_INDEX],lv1_sector,0,BLOCK_SECTOR_SIZE);
  }
  // add new_sec to proper place
  sector_allocate_lv0(lv1_sector,new_sec,pos);
  cache_write(head->sectors[LV0_INDEX],lv1_sector,0,BLOCK_SECTOR_SIZE);
  free (lv1_sector);
  return true;
}

/* add a new sector to head's lv2
  */
static bool
sector_allocate_lv2(struct inode_disk *head, block_sector_t new_sec, off_t pos){
  struct inode_disk *lv2_sector = malloc(sizeof(struct inode_disk));
  /* lv1 sector have been allocated, check if lv2 sector is allocated
    If not, allocate a sector */
  if(head->length < LV0_SIZE+LV1_SIZE){
    block_sector_t lv2 = sector_allocate_disk();
    head->sectors[LV0_INDEX+LV1_INDEX] = lv2;
    cache_read(lv2,lv2_sector,0,BLOCK_SECTOR_SIZE);
  }else{
    cache_read(head->sectors[LV0_INDEX+LV1_INDEX],lv2_sector,0,BLOCK_SECTOR_SIZE);
  }

  struct inode_disk *lv1_sector = malloc(sizeof(struct inode_disk));
  // check if desired lv1 sector of lv2 is allocated */
  off_t ori_sec_num = (head->length-LV0_SIZE-LV1_SIZE-1)/LV1_SIZE;
  off_t new_sec_num = pos/LV1_SIZE;
  if(ori_sec_num < new_sec_num){
    block_sector_t lv1 = sector_allocate_disk();
    lv2_sector->sectors[new_sec_num] = lv1;
    cache_write(head->sectors[LV0_INDEX+LV1_INDEX],lv2_sector,0,BLOCK_SECTOR_SIZE);
    cache_read(lv1,lv1_sector,0,BLOCK_SECTOR_SIZE);
  }else{
    cache_read(lv2_sector->sectors[new_sec_num],lv1_sector,0,BLOCK_SECTOR_SIZE);
  }

  // add new_sec to proper place
  sector_allocate_lv0(lv1_sector,new_sec,pos);
  cache_write(lv2_sector->sectors[new_sec_num],lv1_sector,0,BLOCK_SECTOR_SIZE);
  free(lv2_sector);
  free(lv1_sector);
  return true;
}

/* Allocate one more sector to head inode.
  */
static bool
sector_allocate_one(struct inode_disk *head){
  // allocate a new sector
  block_sector_t new_sec = sector_allocate_disk();
  if(new_sec == -1)return false;
  
  // add new sector to head inode
  uint32_t new_length = head->length + BLOCK_SECTOR_SIZE - 1;
  // new sector fits in LV0
  if(new_length < LV0_SIZE){
    return sector_allocate_lv0(head,new_sec,new_length);
  }
  new_length -= LV0_SIZE;

  // fits in lv1
  if(new_length < LV1_SIZE){
    return sector_allocate_lv1(head,new_sec,new_length);
  }
  new_length -= LV1_SIZE;

  // fits in lv2
  if(new_length < LV2_SIZE){
    return sector_allocate_lv2(head,new_sec,new_length);
  }

  // failed(exceed maximum allocation range)
  return false;
}

/* Allocate more LENGTH bytes blocks and zero them 
  */
static bool
sector_allocate(struct inode_disk *head, off_t length){
  // see if required more length fits in the last sector
  off_t block_end = ROUND_UP(head->length,BLOCK_SECTOR_SIZE);
  if(block_end - head->length >= length){
    head->length += length;
    return true;
  }else{
    // not fit, allocate new sectors
    while(length>0){
      if(!sector_allocate_one(head))return false;

      if(length > BLOCK_SECTOR_SIZE){
        head->length += BLOCK_SECTOR_SIZE;
        length -= BLOCK_SECTOR_SIZE;
      }else{
        head->length += length;
        length = 0;
      }
      
    }
    return true;
  }
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = 0;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->isdir = isdir;
      if(sector_allocate(disk_inode,length)) success=true;
    }

  if(success) cache_write(sector,disk_inode,0,BLOCK_SECTOR_SIZE);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&open_inodes_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  cache_read (sector, &inode->data, 0,BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* deallocate sectors lv2-lv1-lv0
  */
static void
sector_deallocate(struct inode *inode){
  int len = inode->data.length -1;

  block_sector_t sector;

  // release all lv0 sectors
  for(int i=0;i<len/BLOCK_SECTOR_SIZE;i++){
    sector = byte_to_sector(inode,i*BLOCK_SECTOR_SIZE);
    free_map_release(sector,1);
  }
  
  // release lv1 sector if exsits
  if(len >= LV0_SIZE) free_map_release(inode->data.sectors[LV0_INDEX],1);

  // release lv1 sectors inside lv2 if exsits
  if(len >= LV0_SIZE+LV1_SIZE){
    struct inode_disk *lv1_sector = malloc(sizeof(struct inode_disk));
    cache_read(inode->data.sectors[LV0_INDEX+LV1_INDEX],lv1_sector,0,BLOCK_SECTOR_SIZE);
    for(int i=0;i<(len-LV0_SIZE-LV1_SIZE)/LV1_SIZE;i++)
      free_map_release(lv1_sector->sectors[i],1);

    free(lv1_sector);
    free_map_release(inode->data.sectors[LV0_INDEX+LV1_INDEX],1);
  }

  // free head inode_disk
  free_map_release(inode->sector,1);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire(&inode->lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire(&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release(&open_inodes_lock);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          sector_deallocate(inode);
        }

      lock_release(&inode->lock);
      free (inode); 
    }else{
      lock_release(&inode->lock);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
         
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
    
  if (inode->data.length- offset > 512)
    cache_read_ahead(byte_to_sector (inode, offset + 512));

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  // not sufficient sectors, allocate more
  lock_acquire(&inode->lock);
  if(inode_length(inode)<size+offset){
    if(!sector_allocate(&inode->data,size+offset-inode_length(inode))){
      return 0;
    }
    cache_write(inode->sector,&inode->data,0,BLOCK_SECTOR_SIZE);
  }
  lock_release(&inode->lock);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  lock_release(&inode->lock);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_acquire(&inode->lock);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
