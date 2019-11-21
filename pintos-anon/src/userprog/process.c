#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
/* ====================  Project 3 ================== */
#include "vm/frame.h"
#include "vm/page.h"


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
bool argument_pass(const char *cmdline, void **esp){
  /* Setup stack success, allocate arguments for process 
    Max 256 arguments saved in argv Left-To-Right */
  const int arg_limit = 256;
  char *argv[arg_limit];
  char *save_ptr = NULL;
  /* Stack Pointer calculate in bytes */
  char *sp = (char *) *esp;  
  int argc = 0;
  int size = 4 + 4 + 4 + 4;
  char *token = NULL;

  /* Already skip the file name;
    Impose a 4kB limit on arguments passing */
  for (token = strtok_r (cmdline, " ", &save_ptr); token != NULL;
      token = strtok_r (NULL, " ", &save_ptr)){

        size += strlen(token)+1  + 4;

        if(argc+1 == arg_limit || size > PGSIZE){
          printf("Passing arguments over %d numbers or length over 4kB\n",
                arg_limit);
          return false;
        }

        sp -= strlen(token)+1;
        strlcpy(sp, token, strlen(token)+1);
        argv[argc++] = sp;
      }

  /* align; no guarante what inside this gap */
  while((int)sp % 4 != 0) sp--;

  /* Very Important:
    we need cast in the rest to make it insert in 4bytes */
  /* push a zero */
  sp -= 4;
  *(int *)sp = 0;

  /* push arg pointer Right-To-Left */
  for (int i=argc-1; i>=0; i--){
    sp -= 4;
    *(char **)sp = argv[i];
  }

  /* push arg pointer */
  sp -= 4;
  *(char **)sp = sp+4;
    
  /* push arg counter */
  sp -= 4;
  *(int *)sp = argc;

  /* push return address */
  sp -= 4;
  *(int *)sp = 0;

  *esp = sp;
  return true;
}

/* struct use to convey info between parent and child */
struct process_load_arg{
  struct semaphore sema;  /* sync */
  char *cmdline;          /* cmdline */
  bool success;           /* is child loaded successful */
  struct wait_status *status_as_child; /* node in child list */
};

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* copy file_name for a bug i can not fix */
  char cmdline[strlen(file_name)+1];
  strlcpy(cmdline, file_name, strlen (file_name)+1);
  /* slpit file_name from cmd */
  char *save_ptr = NULL;
  char *thread_name = strtok_r(cmdline," ", &save_ptr);

  /* initialized arguments to execute */
  struct process_load_arg load_arg;
  load_arg.success = false;
  load_arg.cmdline = fn_copy;
  /* new a node in child list */
  load_arg.status_as_child = (struct wait_status*) 
                              malloc(sizeof(struct wait_status));
  load_arg.status_as_child->parent_alive = true;
  load_arg.status_as_child->child_list_lock = 
                          &thread_current()->child_list_lock;

  sema_init(&load_arg.sema,0);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (thread_name, PRI_DEFAULT, start_process, &load_arg);
  /* wait until load finish */
  sema_down(&load_arg.sema);

  /* after load is done, we can free copy
    and return child's status */
  palloc_free_page (fn_copy); 
  if (tid == TID_ERROR || !load_arg.success){
    free(load_arg.status_as_child);
    return -1;
  }else{
    lock_acquire(load_arg.status_as_child->child_list_lock);
    load_arg.status_as_child->child_pid = tid;
    /* load success; add it to list */
    list_push_back (&thread_current()->child_list, 
                    &load_arg.status_as_child->elem);
    lock_release(load_arg.status_as_child->child_list_lock);
    return tid;
  }
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux)
{
  struct process_load_arg *load_arg = (struct process_load_arg*)aux;
  char *file_name = load_arg->cmdline;

  struct intr_frame if_;
  bool load_success;
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* make a copy to argument_passing */
  char cmdline[strlen(file_name)+1];
  strlcpy(cmdline, file_name, strlen (file_name)+1);

  /* slpit file_name from cmd */
  char *save_ptr = NULL;
  char *thread_name = strtok_r(file_name," ", &save_ptr);
  load_success = load (thread_name, &if_.eip, &if_.esp);

  if(load_success){
    /* if argument_passing is successfully, load is successful */
    load_arg->success = argument_pass(cmdline,&if_.esp);
    // hex_dump(if_.esp,if_.esp,64,true));

    if(load_arg->success){
      thread_current ()->status_as_child = load_arg->status_as_child;

      /* mark alive */
      lock_acquire(load_arg->status_as_child->child_list_lock);
      load_arg->status_as_child->child_alive = true;
      sema_init(&load_arg->status_as_child->sema,0);
      lock_release(load_arg->status_as_child->child_list_lock);

      /* file deny writes when executing */
      thread_current ()->process_exec_file = filesys_open(thread_name);
      file_deny_write (thread_current ()->process_exec_file);
    }    
  }

  /* wake up parent process */
  sema_up(&load_arg->sema);

  /* If load failed, quit. */
  if (!load_success || !load_arg->success)  {
    thread_current ()->ret = -1;
    thread_exit ();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *cur = thread_current();

  lock_acquire(&cur->child_list_lock);
  /* tranverse list to find match pid child */
  for(struct list_elem *e = list_begin(&cur->child_list);
      e != list_end(&cur->child_list);
      e = list_next(e)){
        struct wait_status *child_status = 
                          list_entry(e,struct wait_status, elem);
        /* find child with given pid */
        if(child_status->child_pid == child_tid){

          lock_release(&cur->child_list_lock);
          if(child_status->child_alive){
            /* child alive, wait until terminate */
            sema_down(&child_status->sema);
          }
          int ret = child_status->child_ret;
          list_remove(e);
          free(child_status);
          return ret;
        }
      }
  lock_release(&cur->child_list_lock);
  /* child not found */
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* A： Remove memory map and unmap all pages. */
  if (list_size(&cur->mem_map_table) != 0)
  {
    /* Traverse memory map table and operate every map. */
    for (struct list_elem *e = list_begin(&cur->mem_map_table);
          e != list_end(&cur->mem_map_table);
          e = list_next(e))
    {
      struct mem_map_entry *mem_map_e = list_entry(e, struct mem_map_entry, elem);

      /* Remove pages of the file one by one. */
      for (uint32_t offset = 0; offset < file_length(mem_map_e->spte->file); offset += PGSIZE)
      {
        struct sup_page_table_entry *spte = get_page_table_entry(mem_map_e->spte->user_vaddr + offset);
        if (spte == NULL) PANIC("Mmap: A failed mapping not recycled during mmap");

        /* Write back if the page has been written */
        if (pagedir_is_dirty(cur->pagedir, spte->user_vaddr))
        {
          file_write_at(spte->file, spte->user_vaddr, spte->read_bytes, spte->offset);
        }

        /* Let the page die. */
        frame_free(spte->fte);
        free(spte->fte);
        pagedir_clear_page(cur->pagedir, spte->user_vaddr);
        hash_delete(&cur->sup_page_table, &spte->elem);
        free(spte);
      }

      list_remove(&mem_map_e->elem);
    }
  }

  /* B: check child_list as a parent role */
  /* free nodes in child_list if node's child already terminated */
  lock_acquire(&cur->child_list_lock);
  for(struct list_elem *e = list_begin(&cur->child_list);
      e != list_end(&cur->child_list);
      e = list_next (e)){
        struct wait_status *child_status = 
                          list_entry(e,struct wait_status, elem);
        /* find dead child */
        if(!child_status->child_alive){
          list_remove(e);
          free(child_status);
        }else{
          /* mark parent as dead to notice child to clean up */
          child_status->parent_alive = false;
        }
      }
  lock_release(&cur->child_list_lock);

  /* C: check status_as_child as child role */
  if(cur->status_as_child != NULL){
    lock_acquire(cur->status_as_child->child_list_lock);
    /* free the thread as child if parent is dead */
    if(!cur->status_as_child->parent_alive){

      /* the step here may have problems since child_list may be destoryed
        already; It seems like we can ignore the list node because parent 
        already exited */
      if(cur->status_as_child->elem.next != NULL && 
         cur->status_as_child->elem.prev != NULL)
        list_remove(&cur->status_as_child->elem);

      free(cur->status_as_child);
    }else{
      /* parent is alive; mark dead and save exit code then wake up in case 
        parent may be waiting */
      cur->status_as_child->child_ret = cur->ret;
      cur->status_as_child->child_alive = false;
      sema_up(&cur->status_as_child->sema);
    }
    lock_release(cur->status_as_child->child_list_lock);
  }

  /* D: close all opened files */
  for(int fd=2;fd<130;fd++){
    if(cur->file_use[fd] != NULL){
      file_close (cur->file_use[fd]);
    }
  }

  if(cur->process_exec_file != NULL){
    file_allow_write(cur->process_exec_file);
    file_close(cur->process_exec_file);
  }

  /* E: recycle all virtual memories */
  // page_table_free(&cur->sup_page_table);

  /* F: destory page allocated */
  /* Destroy the current process's page directory and switch back
    to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
  {
    /* Correct ordering here is crucial.  We must set
        cur->pagedir to NULL before switching page directories,
        so that a timer interrupt can't switch back to the
        process page directory.  We must activate the base page
        directory before destroying the process's page
        directory, or our active page directory will be one
        that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate (NULL);
    pagedir_destroy (pd);

    /* A process with pagedir must be a user process */
    printf ("%s: exit(%d)\n", cur->name, cur->ret);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  // file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  // struct thread *cur = thread_current ();
  // page_table_init(&cur->sup_page_table);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* old implementation: load immediately
      // Get a page of memory.
      uint8_t *kpage = frame_allocate (PAL_USER);
      if (kpage == NULL)
        return false;

      // Load this page.
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          frame_free (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      // Add the page to the process's address space.
      if (!install_page (upage, kpage, writable)) 
        {
          frame_free (kpage);
          return false; 
        }
      */ 
      /* new implementation: load lazily */
      struct sup_page_table_entry *spte = malloc(sizeof(struct sup_page_table_entry));
      if(!page_add(upage, spte, file, ofs, page_read_bytes, page_zero_bytes, writable, NULL)){
        free(spte);
        return false;
      }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  bool success = false;

  struct sup_page_table_entry *spte = malloc(sizeof(struct sup_page_table_entry));
  if(spte == NULL) return false;

  struct frame_table_entry *fte = frame_allocate (PAL_USER | PAL_ZERO, spte);
  if(fte == NULL){
    free(spte);
    return false;
  }

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, fte->frame, true);
  if (success){
    *esp = PHYS_BASE;
    success = page_add(esp,spte,NULL,0,0,0,1,fte);
  }

  // install_page fail or page_add fail
  if(!success){
    frame_free (fte->frame);
    free(spte);
    free(fte);
  }
  
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
