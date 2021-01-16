#ifndef SWAP_H
#define SWAP_H

#include <stdbool.h>
#include <stddef.h>

void swap_init (void);
void swap_in (void *, size_t);
size_t swap_out (void *);

void read_from_block(void*, int);
void write_from_block(void*, int);

#endif