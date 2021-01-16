#include "mmap.h"
#include "page.h"
#include "frame.h"
#include "swap.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"

void 
free_mmap_entry (struct mmap_entry *e)
{
  struct thread *cur = thread_current ();
  struct suppl_page_table_entry *spte;
  unsigned int page_num = e->page_num;

  for (unsigned i = 0; i < page_num; i++)
  {
    void *upage = e->uvaddr + i * PGSIZE;
    spte = page_hash_find (&cur->suppl_page_table, upage);
    if (spte == NULL) 
      continue;
    lock_acquire (&spte->spte_lock);
    
    /* Write back to file if the page is dirty */
    if (pagedir_is_dirty (cur->pagedir, upage))
    { 
      lock_acquire (&file_lock);
      file_write_at (spte->file, upage, spte->read_bytes, spte->offset);
      lock_release (&file_lock);
    }
    /* Free the frame if allocated. */
    if (spte->free == false)
    {       
      vm_free_frame (pagedir_get_page (cur->pagedir, spte->addr));
      pagedir_clear_page (cur->pagedir, spte->addr);
    }

    hash_delete (&cur->suppl_page_table, &spte->hash_elem); 
    lock_release (&spte->spte_lock);
  }
  file_close (e->file);
}

void
free_mmap_list (struct list *mmap_list)
{
  ASSERT(mmap_list != NULL);

  while(!list_empty(mmap_list))
  {
    struct mmap_entry *e = list_entry(list_begin(mmap_list), struct mmap_entry, elem);
    munmap(e->mmap_id);
  }
}
