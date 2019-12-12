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
static uint32_t LV0_SIZE = (LV0_INDEX * BLOCK_SECTOR_SIZE);
static uint32_t LV1_SIZE = ((BLOCK_SECTOR_SIZE / 4) * BLOCK_SECTOR_SIZE);
static uint32_t LV2_SIZE = ((BLOCK_SECTOR_SIZE / 4) * (BLOCK_SECTOR_SIZE / 4) * BLOCK_SECTOR_SIZE); 

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
// this function is wrong by Dec 12 10:50p.m
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  // longer than this file
  if(pos>inode->data.length) return -1;

  if(pos<LV0_SIZE)return lv0_translate(&inode->data,pos);
  pos -= LV0_SIZE;

  // level 1: read level 1 into memory and translate with lv0
  if(pos<LV1_SIZE){
    struct inode_disk *inode_disk = malloc(sizeof(struct inode_disk));
    cache_read(inode->data.sectors[LV0_INDEX],inode_disk,0,BLOCK_SECTOR_SIZE);
    block_sector_t ans = lv0_translate(inode_disk,pos);
    free(inode_disk);
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

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
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
  inode_disk->sectors[(pos-1)/BLOCK_SECTOR_SIZE] = new_sec;
  return true;
}

/* add new_sec to lv1
  */
static bool
sector_allocate_lv1(struct inode_disk *inode_disk, block_sector_t new_sec, off_t pos){
  struct inode_disk *lv1_sector = malloc(sizeof(struct inode_disk));
  cache_read(inode_disk->sectors[LV0_INDEX],lv1_sector,0,BLOCK_SECTOR_SIZE);
  sector_allocate_lv0(lv1_sector,new_sec,pos);
  cache_write(inode_disk->sectors[LV0_INDEX],lv1_sector,0,BLOCK_SECTOR_SIZE);
  free (lv1_sector);
  return true;
}

static bool
sector_allocate_lv2(struct inode_disk *head, block_sector_t new_sec, off_t pos){

}

/* Allocate one more sector to head inode.
  */
static bool
sector_allocate_one(struct inode_disk *head){
  // allocate a new sector
  block_sector_t new_sec = sector_allocate_disk();
  if(new_sec == -1)return false;
  
  // add new sector to head inode
  uint32_t new_length = head->length + BLOCK_SECTOR_SIZE;
  // new sector fits in LV0
  if(new_length < LV0_SIZE){
    return sector_allocate_lv0(head,new_sec,new_length);
  }
  new_length -= LV0_SIZE;

  if(new_length < LV1_SIZE){
    return sector_allocate_lv1(head,new_sec,new_length);
  }
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
    while(length>0){
      if(!sector_allocate_one(head))return false;

      head->length += length;
      length -= BLOCK_SECTOR_SIZE;
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
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
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

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
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
      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     block_read (fs_device, sector_idx, buffer + bytes_read);
      //   }
      // else 
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     // cache_read(sector_idx,bounce, 0, BLOCK_SECTOR_SIZE);
      //     block_read (fs_device, sector_idx, bounce);
      //  memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //  }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

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
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

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
      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Write full sector directly to disk. */
      //     block_write (fs_device, sector_idx, buffer + bytes_written);
      //   }
      // else 
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left) 
      //       block_read (fs_device, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, BLOCK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     block_write (fs_device, sector_idx, bounce);
      //   }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
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
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
