#ifndef EXT4_SEARCH_H
#define EXT4_SEARCH_H

#include "ext4_super.h"

#include <stdint.h>

int ext4_search_filename(const struct ext4_fs *fs, const char *start_path,
                         const char *filename);
int ext4_search_inode_number(const struct ext4_fs *fs, const char *start_path,
                             uint32_t inode_number);
int ext4_search_corrupted_entries(const struct ext4_fs *fs,
                                  const char *start_path);

#endif
