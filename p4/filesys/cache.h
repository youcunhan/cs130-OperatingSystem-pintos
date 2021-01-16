#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include "devices/block.h"

struct cache
{
  /* Whether this cacheline is free. */
  bool free;
  /* Dirty bit */
  bool dirty;
  /* The time_ticks last used, for LRU evict. */
  int64_t time_stamp;

  /* The corresponding sector */
  block_sector_t disk_sector;
  /* Data */
  uint8_t buffer[BLOCK_SECTOR_SIZE];
};

void buffer_cache_init (void);
void buffer_cache_close (void);
void buffer_cache_read (block_sector_t sector, void *target);
void buffer_cache_write (block_sector_t sector, const void *source);

#endif