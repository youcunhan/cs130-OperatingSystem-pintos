#include <debug.h>
#include <string.h>
#include "devices/timer.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

#define CACHE_CNT 64

/* Buffer Caches. */
static struct cache caches[CACHE_CNT];

/* A lock for synchronizing cache operations. */
static struct lock buffer_cache_lock;

static void periodic_write (void* aux UNUSED);

void
buffer_cache_init (void)
{
  lock_init (&buffer_cache_lock);
  for (size_t i = 0; i < CACHE_CNT; ++i)
    caches[i].free = true;
    
  // thread_create ("periodic_write_thread", PRI_DEFAULT, periodic_write, NULL);
}

static void
buffer_cache_flush (struct cache *entry)
{
  ASSERT (lock_held_by_current_thread(&buffer_cache_lock));
  ASSERT (entry != NULL);

  if (entry->free)
    return;

  if (entry->dirty) {
    block_write (fs_device, entry->disk_sector, entry->buffer);
    entry->dirty = false;
  }
}

void
buffer_cache_close (void)
{
  lock_acquire (&buffer_cache_lock);

  size_t i;
  for (i = 0; i < CACHE_CNT; ++i)
  {
    if (caches[i].free == true)
      continue;
    
    buffer_cache_flush(&(caches[i]));
  }

  lock_release (&buffer_cache_lock);
}

static struct cache*
buffer_cache_lookup (block_sector_t sector)
{
  size_t i;
  for (i = 0; i < CACHE_CNT; ++i)
  {
    if (caches[i].free)
      continue;
    
    if (caches[i].disk_sector == sector) {
      return &(caches[i]);
    }
  }
  return NULL;
}

static struct cache*
buffer_cache_evict (void)
{

  /* Firstly, check if there's free cache space already */
  for (size_t i = 0; i < CACHE_CNT; ++i)
  {
    if (caches[i].free)
      return &(caches[i]);
  }

  /* Secondly, find the LRU cache to evict */
  int64_t LRU_time = INT64_MAX;
  size_t LRU_idx = 0;
  for (size_t i = 0; i < CACHE_CNT; ++i)
  {
    if (caches[i].time_stamp < LRU_time)
    {
      LRU_idx = i;
      LRU_time = caches[i].time_stamp;
    }
  }
  struct cache *evi_cache = &caches[LRU_idx];
  if (evi_cache->dirty)
    buffer_cache_flush (evi_cache);
  evi_cache->free = true;
  evi_cache->dirty = false;
  return evi_cache;
}

void
buffer_cache_read (block_sector_t sector, void *target)
{
  lock_acquire (&buffer_cache_lock);

  struct cache *slot = buffer_cache_lookup (sector);
  if (slot == NULL) {
    slot = buffer_cache_evict ();
    ASSERT (slot != NULL && slot->free == true);

    slot->free = false;
    slot->disk_sector = sector;
    slot->dirty = false;
    block_read (fs_device, sector, slot->buffer);
  }

  slot->time_stamp = timer_ticks();
  memcpy (target, slot->buffer, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}

void
buffer_cache_write (block_sector_t sector, const void *source)
{
  lock_acquire (&buffer_cache_lock);

  struct cache *slot = buffer_cache_lookup (sector);
  if (slot == NULL) {
    slot = buffer_cache_evict ();
    ASSERT (slot != NULL && slot->free == true);

    slot->free = false;
    slot->disk_sector = sector;
    slot->dirty = false;
    block_read (fs_device, sector, slot->buffer);
  }

  slot->dirty = true;
  slot->time_stamp = timer_ticks();
  memcpy (slot->buffer, source, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}

void cache_to_disk ()
{
    lock_acquire (&buffer_cache_lock);

    for (size_t i = 0; i < CACHE_CNT; i++)
    {
      if (caches[i].dirty)
	      block_write (fs_device, caches[i].disk_sector, caches[i].buffer);
    }

    lock_release (&buffer_cache_lock);
}

/* write priodically  */
static void periodic_write (void *aux UNUSED)
{
    while (true)
    {
      timer_sleep (100);
      cache_to_disk ();
    }
}