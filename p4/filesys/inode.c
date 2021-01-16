#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static bool inode_reserve (struct inode_disk *disk_inode, off_t length);
static bool inode_unreserve (struct inode *inode);

static uint8_t zeros[BLOCK_SECTOR_SIZE];

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static block_sector_t
index_to_sector (const struct inode_disk *idisk, off_t index)
{
  off_t level_base = 0;     /* The lower bound of this level blocks */
  off_t level_limit = 0;    /* The upper bound of this level blocks */
  block_sector_t ret;

  /* Firstly, try get block in the direct blocks */
  level_limit += DIRECT_BLOCKS_COUNT * 1;
  if (index < level_limit)
    return idisk->direct_blocks[index];
  level_base = level_limit;

  /* Secondly, try get block in the indirect blocks */
  level_limit += 1 * INDIRECT_BLOCKS_PER_SECTOR;
  if (index < level_limit)
  {
    /* Get block */
    struct inode_indirect_block_sector *indirect_idisk = calloc(1, sizeof(struct inode_indirect_block_sector));
    buffer_cache_read (idisk->indirect_block, indirect_idisk);
    ret = indirect_idisk->blocks[index - level_base];

    free(indirect_idisk);
    return ret;
  }
  level_base = level_limit;

  /* Lastly, try get block in the doubly indirect blocks */
  level_limit += 1 * INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR;
  if (index < level_limit)
  {
    /* Split index to two levels */
    off_t index_first =  (index - level_base) / INDIRECT_BLOCKS_PER_SECTOR;   /* Index in the first level */
    off_t index_second = (index - level_base) % INDIRECT_BLOCKS_PER_SECTOR;   /* Index in the second level */

    /* Get block */
    struct inode_indirect_block_sector *indirect_idisk = calloc(1, sizeof(struct inode_indirect_block_sector));
    buffer_cache_read (idisk->doubly_indirect_block, indirect_idisk);
    block_sector_t second_level_sector = indirect_idisk->blocks[index_first];
    buffer_cache_read (second_level_sector, indirect_idisk);
    ret = indirect_idisk->blocks[index_second];

    free(indirect_idisk);
    return ret;
  }
  else
  {
    /* Not found within 3-level limits, illegal */
    return -1;
  }
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if(pos < 0 || pos >= inode->data.length)
    return -1;
  else
  {
    off_t index = pos / BLOCK_SECTOR_SIZE;
    return index_to_sector (&inode->data, index);
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->is_dir = is_dir;
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    if (inode_reserve (disk_inode, disk_inode->length))
    {
      buffer_cache_write (sector, disk_inode);
      success = true;
    }
    free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  buffer_cache_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          inode_unreserve (inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = MIN(inode_left, sector_left);

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = MIN(size, min_left);
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          buffer_cache_read (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          buffer_cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode).
   */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* Extend the file is the write position is after EOF */
  if(byte_to_sector(inode, offset + size - 1) == (uint32_t)(-1))
  {
    /* Try reserve space for the inode */
    bool success = inode_reserve (&inode->data, offset + size);
    if (!success)
      return 0;

    /* Update inode */
    inode->data.length = offset + size;
    buffer_cache_write (inode->sector, & inode->data);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = MIN(inode_left, sector_left);

      /* Number of bytes to actually write into this sector. */
      int chunk_size = MIN(size, min_left);
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          buffer_cache_write (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            buffer_cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          buffer_cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

static bool
inode_reserve_direct (block_sector_t *p_entry)
{
  if (*p_entry == 0) 
  {
    if(!free_map_allocate (1, p_entry))
      return false;
    buffer_cache_write (*p_entry, zeros);
  }
  return true;
}

static bool
inode_reserve_indirect (block_sector_t *p_entry, size_t num_sectors, int level)
{
  if (level == 0)
    return inode_reserve_direct (p_entry);

  struct inode_indirect_block_sector indirect_block;
  if(*p_entry == 0)
  {
    /* Allocate if necessary */
    free_map_allocate (1, p_entry);
    buffer_cache_write (*p_entry, zeros);
  }
  buffer_cache_read(*p_entry, &indirect_block);

  size_t level_unit;
  if (level == 1)
    level_unit = 1;
  else
    level_unit = INDIRECT_BLOCKS_PER_SECTOR;
  
  /* Recursively reserve */
  size_t l = DIV_ROUND_UP (num_sectors, level_unit);    /* How many to reserve in this level */
  for (size_t i = 0; i < l; ++i)
  {
    /* How many to reserve in level - 1 */
    size_t l_ = MIN(num_sectors, level_unit);
    if(!inode_reserve_indirect (&indirect_block.blocks[i], l_, level - 1))
      return false;
    num_sectors -= l_;
  }

  buffer_cache_write (*p_entry, &indirect_block);
  return true;
}

/* Extend inode blocks to at least LENGTH */
static bool
inode_reserve (struct inode_disk *disk_inode, off_t length)
{
  if (length < 0)
    return false;
  

  /* Calculate how many sectors to extend in total */
  size_t sectors_to_reserve = bytes_to_sectors(length);
  size_t i, level_rest;

  /* Firstly, try extend in direct blocks */
  level_rest = MIN(sectors_to_reserve, 1 * DIRECT_BLOCKS_COUNT);
  for (i = 0; i < level_rest; ++i)
  {
    if (disk_inode->direct_blocks[i] == 0)
    {
      if (!inode_reserve_direct (&disk_inode->direct_blocks[i]))
        return false;
    }
  }
  sectors_to_reserve -= level_rest;
  if(sectors_to_reserve == 0)
    return true;

  /* Secondly, try extend in indirect blocks */
  level_rest = MIN(sectors_to_reserve, 1 * INDIRECT_BLOCKS_PER_SECTOR);
  if(!inode_reserve_indirect (&disk_inode->indirect_block, level_rest, 1))
    return false;
  sectors_to_reserve -= level_rest;
  if(sectors_to_reserve == 0)
    return true;

  /* Lastly, try extend in doubly indirect blocks */
  level_rest = MIN(sectors_to_reserve, INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR);
  if(!inode_reserve_indirect (&disk_inode->doubly_indirect_block, level_rest, 2))
    return false;
  sectors_to_reserve -= level_rest;
  if(sectors_to_reserve == 0)
    return true;

  /* Fail to extend to the require size */
  return false;
}

static void
inode_unreserve_indirect (block_sector_t entry, size_t num_sectors, int level)
{
  if (level == 0) {
    free_map_release (entry, 1);
    return;
  }

  struct inode_indirect_block_sector indirect_block;
  buffer_cache_read(entry, &indirect_block);

  size_t level_unit;
  if (level == 1)
    level_unit = 1;
  else
    level_unit = INDIRECT_BLOCKS_PER_SECTOR;

  /* Recursively unreserve */
  size_t l = DIV_ROUND_UP (num_sectors, level_unit);    /* How many to delocate in this level */
  for (size_t i = 0; i < l; ++i)
  {
    /* How many to deallocate in level - 1 */
    size_t l_ = MIN(num_sectors, level_unit);
    inode_unreserve_indirect (indirect_block.blocks[i], l_, level - 1);
    num_sectors -= l_;
  }

  free_map_release (entry, 1);
}

static
bool inode_unreserve (struct inode *inode)
{
  off_t file_length = inode->data.length;
  if(file_length < 0)
    return false;

  /* Calculate how many sectors to unreserve in total */
  size_t sectors_to_unreserve = bytes_to_sectors(file_length);
  size_t i, level_unreserve;

  /* Firstly, try deallocate in the direct blocks */
  level_unreserve = MIN(sectors_to_unreserve, 1 * DIRECT_BLOCKS_COUNT);
  for (i = 0; i < sectors_to_unreserve; ++i)
    free_map_release (inode->data.direct_blocks[i], 1);
  sectors_to_unreserve -= level_unreserve;

  /* Secondly, try deallocate in the indirect blocks */
  level_unreserve = MIN(sectors_to_unreserve, 1 * INDIRECT_BLOCKS_PER_SECTOR);
  if(level_unreserve > 0)
  {
    inode_unreserve_indirect (inode->data.indirect_block, level_unreserve, 1);
    sectors_to_unreserve -= level_unreserve;
  }

  /* Thirdly, try deallocate in the doubly indirect blocks */
  level_unreserve = MIN(sectors_to_unreserve, INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR);
  if(level_unreserve > 0)
  {
    inode_unreserve_indirect (inode->data.doubly_indirect_block, level_unreserve, 2);
    sectors_to_unreserve -= level_unreserve;
  }
  
  return true;
}

