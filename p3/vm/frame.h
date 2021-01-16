#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "page.h"

typedef int pid_t;

struct list frame_table;

struct frame_tab_entry
{
  void *frame;                            /* Actual frame addr */
  struct suppl_page_table_entry *spte;    /* Corresponding suppl_page_table_entry */
  pid_t owner;                            /* Used to identify the owner thread/process */
  struct list_elem elem;
};

void vm_frame_table_init (void);
void *vm_get_frame (enum palloc_flags, void *);
void vm_free_frame (void *frame);
void vm_clear_process_frame_table (pid_t pid);
void *try_evict_frame (struct suppl_page_table_entry *);
void *do_evict_frame (struct suppl_page_table_entry *, struct frame_tab_entry *);

#endif