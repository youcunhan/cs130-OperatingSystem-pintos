#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "lib/user/syscall.h"


struct lock file_lock;  

void syscall_init (void);

bool validate_addr(void *ptr);
bool validate_string(char *str);

/* proj2 */
void halt (void);
void exit (int status);
pid_t exec (const char *cmd_line);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* proj3 */
mapid_t mmap(int fd, void* addr);
void munmap(mapid_t mapping);

#endif /* userprog/syscall.h */