#ifndef EXT4_CHAIN_H
#define EXT4_CHAIN_H

#include "ext4_block.h"
#include "ext4_inode.h"
#include "ext4_super.h"

#include <stddef.h>
#include <stdint.h>

struct ext4_block_ref {
    uint64_t logical_index;
    uint64_t block_index;
    enum ext4_block_ref_kind kind;
};

struct ext4_inode_chain {
    uint32_t inode_number;
    enum ext4_inode_type type;
    struct ext4_block_ref *refs;
    size_t ref_count;
    size_t ref_capacity;
    int corruption_count;
};

struct ext4_block_owner {
    uint64_t block_index;
    uint32_t *inode_numbers;
    size_t owner_count;
    size_t owner_capacity;
};

struct ext4_chain_analysis {
    struct ext4_inode_chain *chains;
    size_t chain_count;
    size_t chain_capacity;
    struct ext4_block_owner *owners;
    size_t owner_count;
    size_t owner_capacity;
    int corruption_count;
};

int ext4_build_chain_for_inode(const struct ext4_fs *fs, uint32_t inode_number,
                               struct ext4_inode_chain *chain);
void ext4_inode_chain_free(struct ext4_inode_chain *chain);
int ext4_build_chain_analysis(const struct ext4_fs *fs,
                              struct ext4_chain_analysis *analysis);
void ext4_chain_analysis_free(struct ext4_chain_analysis *analysis);
int ext4_print_chain_for_path(const struct ext4_fs *fs, const char *path);
int ext4_print_shared_blocks(const struct ext4_fs *fs);
int ext4_print_corruption_scan(const struct ext4_fs *fs);

#endif
