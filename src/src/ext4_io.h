#ifndef EXT4_IO_H
#define EXT4_IO_H

#include <stddef.h>
#include <stdint.h>

int ext4_io_open(const char *path, int writable);
int ext4_io_read_exact_at(int fd, uint64_t offset, void *buffer, size_t size,
                          const char *context);
int ext4_io_write_exact_at(int fd, uint64_t offset, const void *buffer,
                           size_t size, const char *context);
int read_block(int fd, size_t block_size, uint64_t block_index, void *buffer);
int write_block(int fd, size_t block_size, uint64_t block_index,
                const void *buffer);

#endif
