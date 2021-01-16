#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "lib/user/syscall.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"


static void syscall_handler (struct intr_frame *);
static struct file_descriptor *getfile (struct thread *t, int fd, int searchdir);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  int sys_code = *(int*)f->esp;
  uint32_t *esp = f->esp;

  /* Validate potential addrs */
  if (!validate_addr((void *) esp))
  {
    exit(-1);
  }
  
  /* Switch by the syscall code. For each syscall, check each argument before calling. */
  switch (sys_code)
  {
    case SYS_HALT:
    {
      halt();
      break;
    }
    case SYS_EXIT:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      int status = *(esp + 1);
      exit(status);
      break;
    }
    case SYS_EXEC:
    {
      if (!validate_addr((void *) esp) 
      || !validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }

      char *cmd_line = (char *)(*(esp + 1));
      if(!validate_string(cmd_line))
        exit(-1);
      
      f->eax = (uint32_t) exec(cmd_line);
      break;
    }
    case SYS_WAIT:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      pid_t pid = *(esp + 1);
      f->eax = (uint32_t) wait (pid);
      break;
    }
    case SYS_CREATE:
    {
      if (!validate_addr((void *) (esp + 1)) 
      || !validate_addr((void *) (esp + 2)))
      {
        exit(-1);
      }
      char *file = (char *) *(esp + 1);
      unsigned initial_size = *(esp + 2);
      f->eax = (uint32_t) create (file, initial_size);
      break;
    }
    case SYS_REMOVE:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      char *file = (char *)* (esp + 1);
      f->eax = (uint32_t) remove (file);
      break;
    }
    case SYS_OPEN:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      char *file = (char *)* (esp + 1);
      f->eax = (uint32_t) open (file);
      break;
    }
    case SYS_FILESIZE:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      f->eax = (uint32_t) filesize (fd);
      break;
    }
    case SYS_READ:
    {
      if (!validate_addr((void *) (esp + 1)) 
      || !validate_addr((void *) (esp + 2)) 
      || !validate_addr((void *) (esp + 3)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      void *buffer = (void *)*(esp + 2);
      unsigned size = *(esp + 3);
      f->eax = (uint32_t) read (fd, buffer, size);
      break;
    }
    case SYS_WRITE:
    {
      if (!validate_addr((void *) (esp + 1)) 
      || !validate_addr((void *) (esp + 2)) 
      || !validate_addr((void *) (esp + 3)))
      {
        exit(-1);
      }
      int fd = *((int*)f->esp + 1);
      void* buffer = (void*)(*((int*)f->esp + 2));
      unsigned size = *((unsigned*)f->esp + 3);
      f->eax = write(fd, buffer, size);
      break;
    }
    case SYS_SEEK:
    {
      if (!validate_addr((void *) (esp + 1)) 
      || !validate_addr((void *) (esp + 2)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      unsigned position = *(esp + 2);
      seek (fd, position);
      break;
    }
    case SYS_TELL:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      f->eax = (uint32_t) tell (fd);
      break;
    }
    case SYS_CLOSE:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      close (fd);
      break;
    }
    case SYS_CHDIR:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      const char* dir = (const char*) *(esp + 1);
      f->eax = (uint32_t) chdir(dir);
      break;
    }
    case SYS_MKDIR:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      const char* dir = (const char*) *(esp + 1);
      f->eax = (uint32_t) mkdir(dir);
      break;
    }
    case SYS_READDIR:
    {
      if (!validate_addr((void *) (esp + 1)) 
      || !validate_addr((void *) (esp + 2)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      char *name = (char*) *(esp + 2);

      f->eax = (uint32_t) readdir(fd, name);
      break;
    }
    case SYS_ISDIR:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      f->eax = (uint32_t) isdir(fd);
      break;
    }
    case SYS_INUMBER:
    {
      if (!validate_addr((void *) (esp + 1)))
      {
        exit(-1);
      }
      int fd = *(esp + 1);
      f->eax = inumber(fd);
      break;
    }
    default:
      break;
  }
}

/* Terminates Pintos by calling shutdown_power_off() 
(declared in devices/shutdown.h). */
void
halt(void) {
  shutdown_power_off();
}

/* Terminates the current user program, returning 
status to the kernel. If the process's parent waits 
for it (see below), this is the status that will be 
returned. Conventionally, a status of 0 indicates 
success and nonzero values indicate errors. */
void
exit(int status) {
  struct thread *cur = thread_current();
  struct thread *parent = thread_get_by_tid(cur->parent_tid);
  cur->exit_code = status;

  /* Give child's exit info to parent */
  if (parent != NULL && !list_empty(&parent->child_thread_list))
  {
    struct list_elem *e = list_begin (&parent->child_thread_list);
    struct child_status *this_child;
    struct list_elem *child_list_end = list_end (&parent->child_thread_list);

    while (e != child_list_end)
    {
      this_child = list_entry (e, struct child_status, child_elem);
      if (this_child->child_tid == cur->tid)
      {
          /* If find the child, set exit_code for this_child */
          this_child->child_exit_code = status;
          break;
      }
      e = list_next (e);
    }
  }

  thread_exit ();
}

/* Runs the executable whose name is given in cmd_line, 
passing any given arguments, and returns the new process's 
program id (pid). Must return pid -1, which otherwise 
should not be a valid pid, if the program cannot load 
or run for any reason. Thus, the parent process cannot 
return from the exec until it knows whether the child 
process successfully loaded its executable. */
pid_t
exec (const char *cmd_line){
  lock_acquire(&file_lock);
  pid_t pid = process_execute(cmd_line);
  lock_release(&file_lock);

  struct thread *cur = thread_current();
  sema_down(&(cur->load_sema));

  if (cur->load_state == LOAD_SUCCESS)
    return pid;
  else
    return -1;
}

/* Waits for a child process pid and 
retrieves the child's exit status. */
int
wait (pid_t pid){
  return process_wait (pid);
}

/* Creates a new file called file initially initial_size 
bytes in size. Returns true if successful, false otherwise. 
Creating a new file does not open it: opening the new file 
is a separate operation which would require a open system call. */
bool
create (const char *file, unsigned initial_size){
  if(!validate_addr((void *) file)){
    exit(-1);
  }
  lock_acquire (&file_lock);
  bool status = filesys_create (file, initial_size, false);
  lock_release (&file_lock);
  return status;
}

/* Deletes the file called file. Returns true if successful, 
false otherwise. */
bool
remove (const char *file){
  if(!validate_addr((void *) file)){
    exit(-1);
  }
  bool status = false;
  lock_acquire (&file_lock);
  status = filesys_remove (file);
  lock_release (&file_lock);
  return status;
}

/* Opens the file called file. Returns a nonnegative 
integer handle called a "file descriptor" (fd), 
or -1 if the file could not be opened.
File descriptors numbered 0 and 1 are reserved for the console */
int
open (const char *file){
  if(!validate_addr((void *) file)){
    exit(-1);
  }
  struct file_descriptor *file_desc = malloc (sizeof (struct file_descriptor));
  struct thread *cur = thread_current ();
  lock_acquire (&file_lock);
  struct file *f = filesys_open (file);
  lock_release (&file_lock);
  
  if (f == NULL)
    return -1;
  
  file_desc->file = f;
  file_desc->fd = cur->file_num++;
  struct inode *inode = file_get_inode(file_desc->file);
  if(inode != NULL && inode->data.is_dir)
  {
    file_desc->dir = dir_open(inode_reopen(inode));
  }
  else
    file_desc->dir = NULL;
  list_push_back (&cur->fd_list, &file_desc->elem);
  
  return file_desc->fd;
}

/* Returns the size, in bytes, of the file open as fd. */
int
filesize (int fd){
  int size = -1;
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 0);
  if (file_desc != NULL)
  {
    lock_acquire (&file_lock);
    size = file_length (file_desc->file);
    lock_release (&file_lock);
  }
  return size;
}

/* Reads size bytes from the file open as fd into buffer. 
Returns the number of bytes actually read (0 at end of file), 
or -1 if the file could not be read (due to a condition other 
than end of file). Fd 0 reads from the keyboard using input_getc(). */
int
read (int fd, void *buffer, unsigned length){
  if (fd == 1)
    return -1;

  if(!validate_addr((void *) buffer)){
    exit(-1);
  }
  
  int size = 0;
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 0);

  if (fd == 0)
  {
    uint8_t *buf = buffer;
    for (unsigned int i = 0; i < length; i++)
      buf[i] = input_getc ();
    return length;
  }
  
  if (file_desc == NULL || file_desc->file == NULL)
    return -1;

  lock_acquire (&file_lock);
  size = file_read (file_desc->file, buffer, length);
  lock_release (&file_lock);
  return size;
}

/* Writes size bytes from buffer to the open file fd. 
Returns the number of bytes actually written, which 
may be less than size if some bytes could not be written.
Fd 1 writes to the console. */
int
write (int fd, const void *buffer, unsigned length){
  if (fd == 0)
    return -1;

  if(!validate_addr((void *) buffer)){
    exit(-1);
  }
  int size = 0 ;

  
  if (fd == 1)
  { 
    putbuf ((char *)buffer, (size_t)length);
    return length;
  }
  struct file_descriptor* file_desc = getfile (thread_current(), fd, 0);
  if (file_desc == NULL || file_desc->file == NULL)
    return -1;
  
  lock_acquire (&file_lock);
  size = file_write (file_desc->file, buffer, length);
  lock_release (&file_lock);
  
  return size;
}

/* Changes the next byte to be read or written in open 
file fd to position, expressed in bytes from the beginning 
of the file. (Thus, a position of 0 is the file's start.) */
void 
seek (int fd, unsigned position)
{
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 0);
  lock_acquire (&file_lock);
  if (file_desc != NULL)
    file_seek (file_desc->file, position);
  lock_release (&file_lock);
}

/* Returns the position of the next byte to be read or written 
in open file fd, expressed in bytes from the beginning of the file. */
unsigned 
tell (int fd)
{
  unsigned position = -1;
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 0);
  lock_acquire (&file_lock);
  if (file_desc != NULL)
    position = (unsigned) file_tell (file_desc->file);  
  lock_release (&file_lock);
  return position;  
}

/* Closes file descriptor fd. */
void 
close (int fd)
{
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 0);
  lock_acquire (&file_lock);
  if (file_desc != NULL)
    {
      file_close (file_desc->file);
      if(file_desc->dir)
        dir_close(file_desc->dir);
      list_remove (&file_desc->elem);
      free (file_desc);    
    }
  lock_release (&file_lock);
}

/* Changes the current working directory of the process to dir, 
which may be relative or absolute. Returns true if successful, 
false on failure. */
bool
chdir (const char *dir)
{
  if(!validate_addr((void *) dir)){
    exit(-1);
  }

  bool status = false;

  lock_acquire (&file_lock);
  status = filesys_cd(dir);
  lock_release (&file_lock);

  return status;
}

/* Creates the directory named dir, which may be relative or absolute. 
Returns true if successful, false on failure. Fails if dir already exists 
or if any directory name in dir, besides the last, does not already exist. */
bool
mkdir (const char *dir){
  if(!validate_addr((void *) dir)){
    exit(-1);
  }

  bool status = false;

  lock_acquire (&file_lock);
  status = filesys_create(dir, 0, true);
  lock_release (&file_lock);

  return status;
}

/* Reads a directory entry from file descriptor fd, 
which must represent a directory. If successful, 
stores the null-terminated file name in name, 
which must have room for READDIR_MAX_LEN + 1 bytes, 
and returns true. If no entries are left in the directory, 
returns false. */
bool
readdir (int fd, char *name){
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 1);
  bool status = false;

  if (file_desc == NULL)
    return status;

  struct inode *inode;
  inode = file_get_inode(file_desc->file);
  if(inode == NULL)
    return status;

  if(!inode->data.is_dir)
    return status;

  lock_acquire (&file_lock);
  status = dir_readdir (file_desc->dir, name);
  lock_release (&file_lock);
  return status;

}

/* Returns true if fd represents a directory, false if it represents an ordinary file. */
bool
isdir (int fd){
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 2);
  lock_acquire (&file_lock);
  bool status = file_get_inode(file_desc->file)->data.is_dir;
  lock_release (&file_lock);
  return status;
}

/* Returns the inode number of the inode associated with fd, 
which may represent an ordinary file or a directory. */
int
inumber (int fd){
  struct file_descriptor *file_desc = getfile (thread_current(), fd, 2);
  lock_acquire (&file_lock);
  int num = inode_get_inumber (file_get_inode(file_desc->file));
  lock_release (&file_lock);
  return num;
}


/*------------------------- Helper functions -------------------------*/

/* Return the file by given fd in the given thread */
/* searchdir: 0 is file, 1 is dir, 2 is any */
struct file_descriptor*
getfile (struct thread *t, int fd, int searchdir)
{
  struct list_elem *e = NULL;
  struct list *l = &t->fd_list;
  struct file_descriptor *file_desc = NULL;
  for (e = list_begin (l); e != list_end (l); e = list_next (e))
  {
    file_desc = list_entry (e, struct file_descriptor, elem);
    if (file_desc->fd == fd)
    {
      if (searchdir == 2)
        return file_desc;
      else if (file_desc->dir != NULL && searchdir == 1)
        return file_desc;
      else if (file_desc->dir == NULL && searchdir == 0)
        return file_desc;
    }
  }
  return NULL;
}

bool validate_addr(void *ptr)
{
  if (ptr == NULL)
    return false;
  else
  {
    int byte_count = 0;
    for (int i = 0; i < 4; i++)
    {
      /* If every byte of ptr < PHYS_BASE and if belongs to current thread */
      if (is_user_vaddr(ptr+i) && (pagedir_get_page(thread_current()->pagedir, ptr+i) != NULL))
        byte_count++;
    }
    if (byte_count == 4)
      return true;
  }
  return false;
}

bool validate_string (char *str)
{
  if (str == NULL)
    return false;

  char *ch = str;
  while(true)
  {
    if (!validate_addr((void *) ch))
      return false;
    if (*ch == '\0')
      return true;
    ++ch;
  }
  return false;
}
