#include <list.h>
#include <string.h>
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "swap.h"
#include "frame.h"


void *find_unaccessed_page (struct suppl_page_table_entry *);
void *find_longest_time_page (struct suppl_page_table_entry *);

struct lock frame_lock;

void 
vm_frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

void *
vm_get_frame (enum palloc_flags flags, void *spte)
{
  if (!(flags & PAL_USER))
    return NULL;
  
  /* Get a frame from memory */
  void *frame = palloc_get_page (flags);

  /* Evict a frame if no frame can be get */
  if (frame == NULL)
    return try_evict_frame ((struct suppl_page_table_entry *) spte);

  /* Create a ft_entry */
  struct frame_tab_entry *ft_entry = malloc (sizeof (struct frame_tab_entry));
  if (ft_entry == NULL)
    return NULL;
  
  ft_entry->spte = (struct suppl_page_table_entry *) spte;
  ft_entry->frame = frame;
  ft_entry->owner = thread_tid();

  lock_acquire (&frame_lock);
  list_push_front (&frame_table, &ft_entry->elem);
  lock_release (&frame_lock);
  return frame;
}

void 
vm_free_frame (void *frame)
{
  if (frame != NULL)
  {
    struct list_elem *e = list_begin (&frame_table);
    struct list_elem *next;
    struct frame_tab_entry *ft_entry;

    lock_acquire (&frame_lock);
    while (e != list_end (&frame_table))
    {
      next = list_next (e);
      ft_entry = list_entry (e, struct frame_tab_entry, elem);

      /* Found the correponding entry, remove it */
      if (ft_entry->frame == frame)
      {
        list_remove (e);
        free (ft_entry);
        palloc_free_page (frame);
        break;
      }
      e = next;
    }
    lock_release (&frame_lock);
  }
}

void
vm_clear_process_frame_table (pid_t pid)
{
  if (pid != NULL)
  {
    struct list_elem *e = list_begin (&frame_table);
    struct list_elem *next;
    struct frame_tab_entry *ft_entry;

    lock_acquire (&frame_lock);
    while (e != list_end (&frame_table))
    {
      next = list_next (e);
      ft_entry = list_entry (e, struct frame_tab_entry, elem);
      if (ft_entry->owner == pid)
      {
        /* Just clear entries */
        list_remove (e);
        free (ft_entry);
      }
      e = next;
    }
    lock_release (&frame_lock);
  }
}

void *
try_evict_frame (struct suppl_page_table_entry *e)
{
  struct thread *cur = thread_current ();
  
  /* First round search, try to find unaccessed upage */
  struct frame_tab_entry *f_entry = find_unaccessed_page (e);
  if (f_entry != NULL)
  {
    return do_evict_frame (e, f_entry);
  }
    
  /* Second round search, try to find the upage with longest time */
  f_entry = find_longest_time_page (e);
  if (f_entry != NULL)
    return do_evict_frame (e, f_entry);
  else
    return NULL;
}

void *
find_unaccessed_page (struct suppl_page_table_entry *e)
{
  struct thread *cur = thread_current();
  struct list_elem* search = list_begin(&frame_table);
  struct list_elem* search_end = list_end(&frame_table);

  while(search != search_end)
  {
    lock_acquire (&frame_lock);
    struct frame_tab_entry* f_entry = list_entry (search, struct frame_tab_entry, elem);

    /* If current page is not accessed, evict it */
    if (!pagedir_is_accessed (cur->pagedir, f_entry->spte->addr))
    {
      /* Note: lock is not released here, and will be released in do_evict_frame() */
      lock_release (&frame_lock);
      return f_entry;
    }
    else
    {
      /* Reset timestamp, prepare for the second round */
      f_entry->spte->timestamp = timer_ticks();
    }
    
    search = list_next(search);
    lock_release (&frame_lock);
  }

  return NULL;
}

void *
find_longest_time_page (struct suppl_page_table_entry *e)
{
  struct list_elem* search = list_begin(&frame_table);
  struct list_elem* search_end = list_end(&frame_table);
  int64_t first_begin_time = INT64_MAX;
  struct frame_tab_entry *first_begin_entry = NULL;

  while(search != search_end)
  {
    lock_acquire (&frame_lock);
    struct frame_tab_entry* f_entry = list_entry (search, struct frame_tab_entry, elem);

    /* Get the one with least timer_ticks */
    if (f_entry->spte->timestamp < first_begin_time)
    {
      first_begin_time = f_entry->spte->timestamp;
      first_begin_entry = f_entry;
    }

    search = list_next(search);
    lock_release (&frame_lock);
  }

  return first_begin_entry;
}

void *
do_evict_frame (struct suppl_page_table_entry *e, struct frame_tab_entry *f_entry)
{
  ASSERT(e != NULL && f_entry != NULL);
  
  lock_acquire (&frame_lock);
  lock_acquire (&e->spte_lock);
  struct thread *cur = thread_current();
  
  void *evi_frame = f_entry->frame;
  struct suppl_page_table_entry *evi_pte = f_entry->spte;
  uint8_t *evi_upage = evi_pte->addr;
  pid_t owner = f_entry->owner;

  /* If the page is dirty (modified), write it back to file */
  if (pagedir_is_dirty (cur->pagedir, evi_pte->addr) && 
      (evi_pte->type == type_mmap))
  {
    lock_acquire (&file_lock);
    file_write_at (evi_pte->file, evi_pte->addr,
                    evi_pte->read_bytes, evi_pte->offset);
    lock_release (&file_lock);
  } 
  else
  {
    /* Use swap */
    f_entry->spte->type = type_swap;
    f_entry->spte->swap_idx = swap_out (evi_frame);
  }

  /* Set the page to not present and fill it with 0 */
  pagedir_clear_page (thread_get_by_tid(owner)->pagedir, evi_upage);
  memset (evi_frame, 0, PGSIZE);

  /* Remove the original frame_tab_entry */
  list_remove (&f_entry->elem);
  free (f_entry);

  /* Create a new frame_tab_entry */
  struct frame_tab_entry *new_ft_entry = malloc (sizeof (struct frame_tab_entry));
  new_ft_entry->frame = evi_frame;
  new_ft_entry->spte = e;
  new_ft_entry->owner = thread_tid();
  list_push_back (&frame_table, &new_ft_entry->elem);

  lock_release (&e->spte_lock);
  lock_release (&frame_lock);
  return evi_frame;
}