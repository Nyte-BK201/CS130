#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "devices/shutdown.h"
#include "lib/user/syscall.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "lib/stdio.h"
#include "lib/kernel/console.h"

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


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  /* Calculate stack pointer in 4 bytes */
  int *sp = (int *)f->esp;

  
  int call = *sp;
  if ((sp + 1) == NULL || !is_user_vaddr(sp + 1)){
    _exit_(-1);
  }
  if(call==SYS_HALT){
    _halt_();
  }else if(call==SYS_EXIT){
    _exit_(0);
  }else if(call==SYS_EXEC){
    f->eax = _exec_((char *)*(sp + 1)); 
  }else if(call==SYS_WAIT){
    _wait_(*(sp+1));
  }else if(call==SYS_CREATE){
    f->eax = _create_((char *)*(sp + 1), *(sp + 2));
  }else if(call==SYS_REMOVE){
    f->eax = _remove_((char *)*(sp + 1));
  }else if(call==SYS_OPEN){

  }else if(call==SYS_FILESIZE){

  }else if(call==SYS_READ){

  }else if(call==SYS_WRITE){
    f->eax = _write_(*(sp+1), (char *)*(sp+2), *(sp+3));
  }else if(call==SYS_SEEK){

  }else if(call==SYS_TELL){

  }else if(call==SYS_CLOSE){

  }
  
  thread_exit();
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
  pid_t pid = process_execute(cmd_line);
  /* Check if the child executed successfully */
  // if (_wait_(pid) == -1){
  //   return -1;
  // }else{
  //   return pid;
  // }
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

}

static int
_filesize_ (int fd){

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

}

static unsigned
_tell_ (int fd){
  // file_tell()
}

static void
_close_ (int fd){

}