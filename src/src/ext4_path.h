#ifndef EXT4_PATH_H
#define EXT4_PATH_H

#include "ext4_super.h"

#include <stdint.h>

int ext4_resolve_path(const struct ext4_fs *fs, uint32_t start_inode,
                      const char *path, uint32_t *inode_number);
int ext4_print_directory_path(const struct ext4_fs *fs, const char *path);
int ext4_walk_directory_path(const struct ext4_fs *fs, const char *path);

#endif
