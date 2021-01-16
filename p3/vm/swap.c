#include "swap.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"


#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)


static struct block* global_swap_block; /* The block of the swap disk */
static struct bitmap *swap_bitmap;									
static struct lock swap_lock;


void
swap_init ()
{
  global_swap_block = block_get_role(BLOCK_SWAP);
  if (global_swap_block == NULL)
    PANIC ("Failed to get swap block!\n");
  
  swap_bitmap = bitmap_create (block_size (global_swap_block) / SECTORS_PER_PAGE);
  if (swap_bitmap == NULL)
    PANIC ("Failed to create bitmap!\n");
  
  bitmap_set_all (swap_bitmap, false);
  lock_init (&swap_lock);
}

void
swap_in (void * page, size_t swap_index){
  read_from_block(page, swap_index);
  bitmap_flip (swap_bitmap, swap_index);
}

size_t
swap_out (void * page){
  lock_acquire (&swap_lock);
  size_t swap_index = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  lock_release (&swap_lock);
  write_from_block(page, swap_index);
  return swap_index;
}

void
read_from_block(void* frame, int index){
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
  {
    block_read (global_swap_block, index * SECTORS_PER_PAGE + i,
                frame + i * BLOCK_SECTOR_SIZE);
  }
}

void
write_from_block(void* frame, int index){
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
  {
    block_write (global_swap_block, index * SECTORS_PER_PAGE + i,
                frame + i * BLOCK_SECTOR_SIZE);
  }
}