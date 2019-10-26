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
    f->eax = _create_(*(sp + 1), *(sp + 2));
  }else if(call == SYS_REMOVE){
    f->eax = _remove_(*(sp + 1));
  }else if(call == SYS_OPEN){
    f->eax = _open_(*(sp + 1));
  }else if(call==SYS_FILESIZE){
    f->eax = _filesize_(*(sp + 1));
  }else if(call==SYS_READ){
    
  }else if(call==SYS_WRITE){
    f->eax = _write_(*(sp+1), (char *)*(sp+2), *(sp+3));
  }else if(call==SYS_SEEK){
    _seek_(*(sp + 5), *(sp + 4)); //uncertain offset
  }else if(call==SYS_TELL){
    if ((sp + 1) == NULL) _exit_(-1);
    if (!is_user_vaddr(sp + 1)) _exit_(-1);
    f->eax = _tell_(*(sp + 1));
  }else if(call==SYS_CLOSE){
    if ((sp + 1) == NULL) _exit_(-1);
    if (!is_user_vaddr(sp + 1)) _exit_(-1);
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
  return(filesys_create(file,initial_size));
}

static bool
_remove_ (const char *file){
  return(filesys_remove(file));
}

static int
_open_ (const char *file){
  if (file == NULL || !is_user_vaddr(file) 
      || !is_user_vaddr(file + strlen(file)) )
    _exit_(-1);

  int fd;
  struct thread *cur = thread_current();

  /* Open the file as the given name */
  struct file *curfile = filesys_open(file);

  /* Check if the file is opened */
  if (!curfile){
    fd = -1;
  }

  /* Put the file into the thread */
  if (cur->file_use[cur->fd_suggest] == NULL){
    cur->file_use[cur->fd_suggest] = curfile;
    fd = cur->fd_suggest;
    /* Find next suitable fd */
    while (cur->fd_suggest < 128 && cur->file_use[cur->fd_suggest] != NULL){
      cur->fd_suggest++;
    }
  }

  return fd;
}

static int
_filesize_ (int fd){
  if(&fd == 
    fd == STDOUT_FILENO || fd == STDIN_FILENO || )
  if ( == NULL) _exit_(-1);

  struct thread *cur = thread_current();

  /* Get the target file */
  struct file *curfile = cur->file_use[fd];

  /* Check if the file is opened */
  if (!curfile){
    return -1;
  }

  /* Calculate the size */
  return (file_length(curfile));
}

static int
_read_ (int fd, void *buffer, unsigned size){

}

static int
_write_ (int fd, const void *buffer, unsigned size){
  if(fd == STDOUT_FILENO){
    putbuf(buffer,size);
    return size;
  }
}

static void
_seek_ (int fd, unsigned position){
  struct thread *cur = thread_current();

  /* Get the target file */
  struct file *curfile = cur->file_use[fd];

  /* seek */
  if (curfile){
    file_seek(curfile, position);
  }
}

static unsigned
_tell_ (int fd){
  struct thread *cur = thread_current();

  /* Get the target file */
  struct file *curfile = cur->file_use[fd];

  /* Check if the file is opened */
  if (!curfile){
    return -1;
  }

  return(file_tell(curfile));
}

static void
_close_ (int fd){
  struct thread *cur = thread_current();

  /* Get the target file */
  struct file *curfile = cur->file_use[fd];

  /* Check if the file is opened */
  if (!curfile){
    return -1;
  }

  /* Close the file and remove from the thread */
  file_close(curfile);
  cur->file_use[fd] = NULL;

  /* Update the new suitable fd */
  if (fd < cur->fd_suggest){
    cur->fd_suggest = fd;
  }
}