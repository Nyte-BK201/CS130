#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_clear ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  if(!strcmp(name,"/")) return false;

  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;
  char *file_name = NULL;
  if(!filesys_path_parse(name,&dir,&file_name)) return false;

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(file_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  // special case since no name at all will make system crash
  if(!strcmp(name,"/")) return file_open (inode_open(ROOT_DIR_SECTOR));

  struct dir *dir = NULL;
  char *file_name = NULL;
  if(!filesys_path_parse(name,&dir,&file_name)) return false;
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  dir_close (dir);
  free(file_name);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = NULL;
  char *file_name = NULL;

  // exclude root
  if(!strcmp(name,"/")) return false;

  if(!filesys_path_parse(name,&dir,&file_name)) return false;

  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir); 
  free(file_name);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Parse a path, e.g. "/a/b/c" into dir and file. DIR_NAME points to "/a/b",
   FILE_NAME points to "c". FILE_NAME maybe "." or ".." which should be
   handled when using. Return false if any invalid path is given. 
   
   Important: this function will not function properly when inputing "/";
   need to handle outside this function */
bool
filesys_path_parse(const char *name, struct dir **dir_name, char **file_name)
{
  if(strlen(name) <= 0) return false;
  ASSERT(strcmp(name,"/"));

  char path[strlen(name) + 1];
  memcpy(path, name, strlen(name) + 1);

  struct dir *dir = NULL;

  char *front = path;
  /* skip fronting space */
  while(*front == ' ') front++;

  /* main thread can be NULL */
  if (*front == '/' || thread_current()->cwd == NULL){
    // absolute path
    dir = dir_open_root();
  }else{
    // relative path
    dir = dir_reopen(thread_current()->cwd);
  }

  // fail when dir open failed
  if(dir == NULL) return false;

  char *token = NULL;
  char *save_ptr = NULL;
  char *last_token = NULL;

  // get dir and last token
  for(token = strtok_r(front, "/", &save_ptr); ; ){
    last_token = strtok_r(NULL,"/",&save_ptr);
    if(last_token == NULL){
      last_token = token;
      break;
    }

    struct inode *inode = NULL;

    //lookup the subdirectory and return false if it doesn't exist
    if (!dir_lookup(dir, token, &inode)) {
      dir_close(dir);
      return false;
    }

    // go into subdirectory
    dir_close(dir);
    dir = dir_open(inode);   
  }

  *dir_name = dir;

  int file_name_length = strlen(last_token);
  *file_name = malloc(file_name_length + 1);
  memcpy(*file_name, last_token, file_name_length + 1);

  return true;
}