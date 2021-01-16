#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define INODE_MAGIC 0x494e4f44    /* Identifies an inode. */
#define INDIRECT_BLOCKS_PER_SECTOR 128
#define DIRECT_BLOCKS_COUNT ((BLOCK_SECTOR_SIZE - (5 * sizeof(block_sector_t))) / sizeof(block_sector_t))

struct bitmap;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  block_sector_t direct_blocks[DIRECT_BLOCKS_COUNT];
  block_sector_t indirect_block;
  block_sector_t doubly_indirect_block;

  bool is_dir;                        /* Is dir? */
  off_t length;                       /* File size in bytes. */
  unsigned magic;                     /* Magic number. */
};

/* Sturcture containing indirect blocks */
struct inode_indirect_block_sector
{
  block_sector_t blocks[INDIRECT_BLOCKS_PER_SECTOR];
};

/* In-memory inode. */
struct inode
{
  struct list_elem elem;              /* Element in inode list. */
  block_sector_t sector;              /* Sector number of disk location. */
  int open_cnt;                       /* Number of openers. */
  bool removed;                       /* True if deleted, false otherwise. */
  int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  struct inode_disk data;             /* Inode content. */
};

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
