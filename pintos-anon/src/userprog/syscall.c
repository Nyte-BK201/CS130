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
// int num = *(int*)f->esp;
//   if (num == SYS_WRITE)
//   {
//     int fd = *(int*)(f->esp + 4);
//     char* buf = (char*)(f->esp + 8);
//     size_t size = *(int*)(f->esp + 12);
//     putbuf(buf,size);
//     f->eax = size;
//   }

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  /* Calculate stack pointer in 4 bytes */
  int *sp = (int *)f->esp;

  int call = *sp;
  if(call==SYS_HALT){
    _halt_();
  }else if(call==SYS_EXIT){
    _exit_(0);
  }else if(call==SYS_EXEC){
    _exec_(*(sp+1));
  }else if(call==SYS_WAIT){
    _wait_(*(sp+1));
  }else if(call==SYS_CREATE){
    // _create_(*(sp+1),)
  }else if(call==)

  thread_exit ();
}

static void
_halt_(void){
  shutdown_power_off();
}

static void
_exit_(int status){
  thread_current()->ret = status;
  thread_exit ();
}

static pid_t
_exec_(const char *cmd_line){

}

static int
_wait_(pid_t pid){
  return process_wait(pid);
}

static bool
_create_(const char *file, unsigned initial_size){

}

static bool
_remove_ (const char *file){

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