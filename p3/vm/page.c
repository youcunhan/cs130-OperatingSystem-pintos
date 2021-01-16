#include "page.h"
#include "frame.h"
#include <debug.h>
#include <hash.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "swap.h"

void free_func (struct hash_elem *e, void *aux UNUSED);
 	
/* Returns a hash value for page p_. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct suppl_page_table_entry * e = hash_entry (p_, struct suppl_page_table_entry, hash_elem);
  return hash_bytes (&e->addr, sizeof (e->addr));
}

/* Returns true if page a_ is in the front of page b_. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct suppl_page_table_entry *a = hash_entry (a_, struct suppl_page_table_entry, hash_elem);
  const struct suppl_page_table_entry *b = hash_entry (b_, struct suppl_page_table_entry, hash_elem);

  return a->addr < b->addr;
}

struct suppl_page_table_entry *
page_hash_find (struct hash *table, uint8_t *upage)
{
  ASSERT (upage != NULL && table != NULL);

  struct suppl_page_table_entry tmp;
  tmp.addr = pg_round_down (upage);

  struct hash_elem *found_elem = hash_find (table, &tmp.hash_elem);
  if (found_elem == NULL)
    return NULL;

  return hash_entry (found_elem, struct suppl_page_table_entry, hash_elem);
}

bool 
page_load_file (struct suppl_page_table_entry *spte)
{
  void *frame = vm_get_frame (PAL_USER, spte);
  ASSERT (frame != NULL)

  lock_acquire(&spte->spte_lock);
  
  lock_acquire (&file_lock);
  off_t ofs = file_read_at (spte->file, frame, spte->read_bytes, spte->offset);
  lock_release (&file_lock);

  /* Check if actual read bytes equals to the request */
  if (ofs != (off_t) spte->read_bytes)
  {
    vm_free_frame (frame);
    return false;
  }

  /* Set zeros */
  if (spte->zero_bytes > 0)
    memset (frame + spte->read_bytes, 0, spte->zero_bytes);

  /* Install page */
  if (!install_page (spte->addr, frame, spte->writable))
  {
    vm_free_frame (frame);
    return false;
  }
  
  /* Set to loaded */
  spte->free = false;

  lock_release(&spte->spte_lock);
  return true;
}

bool page_load_swap (struct suppl_page_table_entry *spte)
{
  void *frame = vm_get_frame (PAL_USER, spte);
  ASSERT (frame != NULL)

  lock_acquire (&spte->spte_lock);

  swap_in (frame, spte->swap_idx);

  /* Install page */
  if (!install_page (spte->addr, frame, spte->writable))
  {
    vm_free_frame (frame);
    return false;
  }

  /* Set to loaded */
  spte->free = false;

  lock_release (&spte->spte_lock);
  return true;
}

bool page_lazy_load (struct file *file, off_t ofs, uint8_t *upage, 
                     uint32_t read_bytes, uint32_t zero_bytes,
                     bool writable, int type)
{
  /* Proceed until both read_bytes and zero_bytes become 0 */
  while (read_bytes > 0 || zero_bytes > 0) 
  {
    /* Actual read bytes cannont exceed PGSIZE, the rest should be filled with 0 */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct suppl_page_table_entry *spte = malloc (sizeof (struct suppl_page_table_entry));
    if (spte == NULL){
      return false;
    }
    /* Initialize spte */
    spte->type = type;
    spte->file = file;
    spte->offset = ofs;
    spte->addr = upage;
    spte->read_bytes = page_read_bytes;
    spte->zero_bytes = page_zero_bytes;
    spte->writable = writable;
    spte->timestamp = timer_ticks();
    spte->free = true;
    lock_init(&spte->spte_lock);

    /* Insert into suppl_page_table */
    struct thread *t = thread_current();
    struct hash_elem* old = hash_insert (&t->suppl_page_table, &spte->hash_elem);
    if (old != NULL)
    {
      free (spte);
      return false;
    }

    /* Update for next round */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
    ofs += page_read_bytes;
  }
  return true;
}

bool stack_grow (void *fault_addr)
{
  ASSERT (fault_addr != NULL);
  
  struct thread *cur = thread_current ();

  /* Create a spte */
  struct suppl_page_table_entry *spte = malloc (sizeof (struct suppl_page_table_entry));

  /* Get an empty frame */
  void *frame = vm_get_frame (PAL_USER, spte);
  if (frame == NULL)
  {
    free (spte);
    return false;
  }
  spte->free = false;
  spte->writable = true;
  spte->addr = pg_round_down (fault_addr);
  spte->type = type_swap;
  spte->timestamp = timer_ticks();
  lock_init (&spte->spte_lock);

  /* Install page */
  if (!install_page (spte->addr, frame, spte->writable))
  {
    free (spte);
    vm_free_frame (frame);
    return false;
  }

  /* Insert into suppl_page_table */
  if (hash_insert (&cur->suppl_page_table, &spte->hash_elem))
  {
    free (spte);
    vm_free_frame (frame);
    return false;
  }
  return true;
}

void
free_suppl_page_table (struct hash *spte)
{
  hash_destroy (spte, free_func);
}

void
free_func (struct hash_elem *e, void *aux UNUSED)
{
  free (hash_entry (e, struct suppl_page_table_entry, hash_elem));
}