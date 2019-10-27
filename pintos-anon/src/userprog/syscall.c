#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "lib/stdio.h"
#include "lib/kernel/console.h"
#include "pagedir.h"

#define ADDR_UNDER_CODE_SEG (0x08048000)

static void syscall_handler (struct intr_frame *);

/* ============================ project 2 ============================= */
static void _halt_(void);
static void _exit_(int status);
static pid_t _exec_(const char *cmd_line);
static int _wait_ (pid_t pid);
static bool _create_ (const char *file, unsigned initial_size);
static bool _remove_ (const char *file);
static int _open_ (const char *file);
static int _filesize_ (int fd);
static int _read_ (int fd, void *buffer, unsigned size);
static int _write_ (int fd, const void *buffer, unsigned size);
static void _seek_ (int fd, unsigned position);
static unsigned _tell_ (int fd);
static void _close_ (int fd);

/* return true if a pointer is valid */
bool check_ptr(char *ptr){
  if(ptr == NULL || !is_user_vaddr(ptr) || ptr < ADDR_UNDER_CODE_SEG)
    _exit_(-1);

  if(pagedir_get_page(thread_current()->pagedir,ptr) == NULL)
    _exit_(-1);

  return true;
}

/* check ptr and ptr + 4 (in bytes) */
bool check_ptr_length(char *ptr, int length){
  check_ptr(ptr);
  check_ptr(ptr+length-1);
  return true;
}


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Calculate stack pointer in 4 bytes */
  int *sp = (int *)f->esp;
  check_ptr_length(sp,4);

  int call = *sp;
  printf ("system call: %d\n", call);

  if(call == SYS_HALT){
    _halt_();
  }else if(call == SYS_EXIT){
    check_ptr_length(sp+1,4);
    _exit_(*(sp + 1));
  }else if(call == SYS_EXEC){
    check_ptr_length(sp+1,4);
    f->eax = _exec_(*(sp + 1)); 
  }else if(call == SYS_WAIT){
    check_ptr_length(sp+1,4);
    _wait_(*(sp+1));
  }else if(call == SYS_CREATE){
    check_ptr_length(sp+1,4);
    check_ptr_length(sp+2,4);
    f->eax = _create_(*(sp + 1), *(sp + 2));
  }else if(call == SYS_REMOVE){
    check_ptr_length(sp+1,4);
    f->eax = _remove_(*(sp + 1));
  }else if(call == SYS_OPEN){
    check_ptr_length(sp+1,4);
    f->eax = _open_(*(sp + 1));
  }else if(call==SYS_FILESIZE){
    check_ptr_length(sp+1,4);
    f->eax = _filesize_(*(sp + 1));
  }else if(call==SYS_READ){
    check_ptr_length(sp+1,4);
    check_ptr_length(sp+2,4);
    check_ptr_length(sp+3,4);
    f->eax = _read_(*(sp + 1), *(sp + 2), *(sp + 3));
  }else if(call==SYS_WRITE){
    check_ptr_length(sp+1,4);
    check_ptr_length(sp+2,4);
    check_ptr_length(sp+3,4);
    f->eax = _write_(*(sp+1), *(sp+2), *(sp+3));
  }else if(call==SYS_SEEK){
    check_ptr_length(sp+1,4);
    check_ptr_length(sp+2,4);
    _seek_(*(sp + 1), *(sp + 2));
  }else if(call==SYS_TELL){
    check_ptr_length(sp+1,4);
    f->eax = _tell_(*(sp + 1));
  }else if(call==SYS_CLOSE){
    check_ptr_length(sp+1,4);
    _close_(*(sp + 1));
  }
  
}


static void
_halt_(void){
  shutdown_power_off();
}

static void
_exit_(int status){
  thread_current()->ret = status;
  thread_exit();
}

static pid_t
_exec_(const char *cmd_line){
  check_ptr_length(cmd_line,strlen(cmd_line)+1);

  return process_execute(cmd_line);
}

static int
_wait_(pid_t pid){
  return process_wait(pid);
}

static bool
_create_(const char *file, unsigned initial_size){
  check_ptr_length(file,strlen(file)+1);
  return(filesys_create(file,initial_size));
}

static bool
_remove_ (const char *file){
  check_ptr_length(file,strlen(file)+1);
  return(filesys_remove(file));
}

static int
_open_ (const char *file){
  check_ptr_length(file,strlen(file)+1);

  int fd;
  struct thread *cur = thread_current();

  /* Open the file as the given name */
  struct file *curfile = filesys_open(file);

  /* Check if the file is opened */
  if (!curfile){
    return -1;
  }

  /* Put the file into the thread */
  if (cur->file_use[cur->fd_suggest] == NULL){
    cur->file_use[cur->fd_suggest] = curfile;
    fd = cur->fd_suggest;
    /* Find next suitable fd; we don't use 0,1 */
    for(cur->fd_suggest = 2;
        cur->fd_suggest < 130 && cur->file_use[cur->fd_suggest] != NULL;
        cur->fd_suggest++);
  }

  return fd;
}

static int
_filesize_ (int fd){
  if(fd == STDOUT_FILENO || fd == STDIN_FILENO) _exit_(-1);

  /* Get the target file */
  struct file *curfile = thread_current()->file_use[fd];
  if(curfile == NULL) _exit_(-1);

  /* Calculate the size */
  return (file_length(curfile));
}

static int
_read_ (int fd, void *buffer, unsigned size){
  if(fd == STDOUT_FILENO) _exit_(-1);
  
  /* check every buffer page validity */
  for(void *ptr=buffer; ptr<buffer+size; ptr+=PGSIZE){
    check_ptr(ptr);
  }

  int res = 0;
  
  if (size == 0){
    return 0;
  }else if(fd == STDIN_FILENO){
    char *buf = (char *)buffer;
    for(int i=0; i<size; i++){
      buf[i] = input_putc();
      res++;
    }
  }else{
    struct file *curfile = thread_current()->file_use[fd];
    if(curfile == NULL) _exit_(-1);

    res = file_read(curfile, buffer, size);
  }

  return res;
}

static int
_write_ (int fd, const void *buffer, unsigned size){
  if(fd == STDIN_FILENO) _exit_(-1);

  for(void *ptr=buffer; ptr<buffer+size; ptr+=PGSIZE){
    check_ptr(ptr);
  }

  if (size == 0){
    return 0;
  }else if(fd == STDOUT_FILENO){
    putbuf(buffer,size);
    return size;
  }else{
    struct file *curfile = thread_current()->file_use[fd];
    if(curfile == NULL) _exit_(-1);

    return file_write(curfile, buffer, size);
  }
}

static void
_seek_ (int fd, unsigned position){
  // if(fd == STDOUT_FILENO || fd == STDIN_FILENO) _exit_(-1);

  /* Get the target file */
  struct file *curfile = thread_current()->file_use[fd];
  if(curfile == NULL) _exit_(-1);

  /* seek */
  if (curfile){
    file_seek(curfile, position);
  }
}

static unsigned
_tell_ (int fd){
  // if(fd == STDOUT_FILENO || fd == STDIN_FILENO) _exit_(-1);

  /* Get the target file */
  struct file *curfile = thread_current()->file_use[fd];
  if(curfile == NULL) _exit_(-1);

  return file_tell(curfile);
}

static void
_close_ (int fd){
  if(fd == STDOUT_FILENO || fd == STDIN_FILENO) _exit_(-1);

  struct thread *cur = thread_current();

  /* Get the target file */
  struct file *curfile = cur->file_use[fd];
  if(curfile == NULL) _exit_(-1);

  /* Close the file and remove from the thread */
  file_close(curfile);
  cur->file_use[fd] = NULL;
}