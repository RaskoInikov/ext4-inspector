#ifndef EXT4_GROUP_H
#define EXT4_GROUP_H

#include "ext4_super.h"

#include <stdint.h>

struct ext4_group_desc {
    uint64_t bg_block_bitmap;
    uint64_t bg_inode_bitmap;
    uint64_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_flags;
};

int ext4_read_group_desc(const struct ext4_fs *fs, uint32_t group_index,
                         struct ext4_group_desc *desc);
int ext4_get_inode_table(const struct ext4_fs *fs, uint32_t group_index,
                         uint64_t *block_index);

#endif
