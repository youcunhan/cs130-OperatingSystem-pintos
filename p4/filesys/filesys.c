#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  buffer_cache_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  buffer_cache_close ();
}

/* Creates a file or directory (set by `is_dir`) of
   full path `path` with the given `initial_size`.
   The path to file consists of two parts: path directory and filename.

   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;

  /* Get dir and filename */
  char *directory = (char*) malloc(sizeof(char) * (strlen(path) + 1));
  char *filename = (char*) malloc(sizeof(char) * (strlen(path) + 1));
  name_resolution(path, directory, filename);
  struct dir *dir = dir_open_path (directory);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, filename, inode_sector, is_dir));

  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  free (directory);
  free (filename);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  int l = strlen(name);
  if (l == 0)
    return NULL;

  char *directory = (char*) malloc(sizeof(char) * (l + 1));
  char *filename = (char*) malloc(sizeof(char) * (l + 1));
  name_resolution(name, directory, filename);
  struct dir *dir = dir_open_path (directory);

  if (dir == NULL)
  {
    free (directory);
    free (filename);
    return NULL;
  }

  struct inode *inode = NULL;
  if (strlen(filename) > 0)
  {
    dir_lookup (dir, filename, &inode);
    dir_close (dir);
  }
  else
    inode = dir_get_inode (dir);

  /* If file is removed */
  if (inode == NULL || inode->removed)
  {
    free (directory);
    free (filename);
    return NULL;
  }

  struct file *f = file_open (inode);
  free (directory);
  free (filename);
  return f;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  int l = strlen(name);
  char *directory = (char*) malloc(sizeof(char) * (l + 1));
  char *filename = (char*) malloc(sizeof(char) * (l + 1));
  name_resolution(name, directory, filename);
  struct dir *dir = dir_open_path (directory);

  bool success = (dir != NULL && dir_remove (dir, filename));
  dir_close (dir);

  free (directory);
  free (filename);
  return success;
}

/* Change CWD for the current thread. */
bool
filesys_cd (const char *name)
{
  struct dir *dir = dir_open_path (name);
  if(dir == NULL)
    return false;

  /* Change dir now */
  dir_close (thread_current()->cwd);
  thread_current()->cwd = dir;
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
