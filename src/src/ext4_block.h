#ifndef EXT4_BLOCK_H
#define EXT4_BLOCK_H

#include "ext4_inode.h"
#include "ext4_super.h"

#include <stdint.h>

enum ext4_block_ref_kind {
    EXT4_BLOCK_REF_DIRECT = 0,
    EXT4_BLOCK_REF_SINGLE_INDIRECT,
    EXT4_BLOCK_REF_SINGLE_INDIRECT_TABLE
};

typedef int (*ext4_block_callback)(uint32_t inode_number,
                                   uint64_t logical_index,
                                   uint64_t block_index,
                                   enum ext4_block_ref_kind kind,
                                   void *user);

int ext4_traverse_inode_blocks(const struct ext4_fs *fs,
                               const struct ext4_inode *inode,
                               ext4_block_callback callback,
                               void *user,
                               int *corruption_count);

#endif
