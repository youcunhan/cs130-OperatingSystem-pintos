#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/file.h"
#include "frame.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/timer.h"

#define spte_init(suppl_page_table) hash_init((suppl_page_table), page_hash, page_less, NULL)
#define STACK_LIMIT (1 << 23)

static const int type_mmap = 1;
static const int type_file = 2;
static const int type_swap = 3;

struct suppl_page_table_entry 
{
  struct file *file;            /* File to load */
  int type;                     /* Type of the file */
  void *addr;                   /* User virtual address, key to he hash table */
  off_t offset;                 /* File offset */
  uint32_t read_bytes;          /* Bytes to read from file after offset */
  uint32_t zero_bytes;          /* Bytes to be zeroed, after read bytes */
  bool writable;                /* Whether the page is writable */
  bool free;                    /* False if the page hasn't been loaded */
  size_t swap_idx;              /* Index on swap bitmap returned by swap_out() */
  int64_t timestamp;            /* Creation and accessing time */
  struct lock spte_lock;        /* Lock in case synchronization */
  struct hash_elem hash_elem;   /* Hash table element */
};

unsigned page_hash (const struct hash_elem *, void *);
bool page_less (const struct hash_elem *, const struct hash_elem *, void *);

struct suppl_page_table_entry *page_hash_find (struct hash *, uint8_t *);
bool page_load_file (struct suppl_page_table_entry *);
bool page_load_swap (struct suppl_page_table_entry *);
bool page_lazy_load (struct file *f, off_t, uint8_t *, uint32_t, uint32_t, bool, int);

bool stack_grow (void *);

void free_suppl_page_table (struct hash *);

#endif