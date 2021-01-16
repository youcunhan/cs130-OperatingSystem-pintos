#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <hash.h>
#include "filesys/file.h"

typedef int mapid_t;
struct mmap_entry
{
  mapid_t mmap_id;        /* The map id */
  void *uvaddr;           /* The user virtual address */
  struct file *file;      /* The pointer of the mapped file */
  unsigned int page_num;  /* Number of pages in this map */
  struct list_elem elem;  /* The hash_elem used to become an elem in a hash table */
};

void free_mmap_entry (struct mmap_entry *);
void free_mmap_list (struct list *);

#endif