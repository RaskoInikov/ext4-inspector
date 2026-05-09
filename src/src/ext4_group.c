#include "ext4_group.h"

#include "ext4_io.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t combine_block(uint32_t lo, uint32_t hi)
{
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static int descriptor_table_offset(const struct ext4_fs *fs,
                                   uint32_t group_index,
                                   uint64_t *offset)
{
    uint64_t table_block;
    uint64_t table_offset;
    uint64_t entry_offset;

    if (fs == NULL || offset == NULL || group_index >= fs->groups_count) {
        fprintf(stderr, "ext4 group descriptor index %" PRIu32 ": %s\n",
                group_index, strerror(EINVAL));
        return 1;
    }

    table_block = (uint64_t)fs->superblock.s_first_data_block + 1U;
    if (!ext4_block_index_valid(fs, table_block)) {
        return 1;
    }

    if (table_block != 0U && fs->block_size > UINT64_MAX / table_block) {
        fprintf(stderr, "ext4 group descriptor offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    if (group_index != 0U &&
        fs->group_desc_size > UINT64_MAX / (uint64_t)group_index) {
        fprintf(stderr, "ext4 group descriptor offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    table_offset = table_block * fs->block_size;
    entry_offset = (uint64_t)group_index * fs->group_desc_size;
    if (table_offset > UINT64_MAX - entry_offset) {
        fprintf(stderr, "ext4 group descriptor offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    *offset = table_offset + entry_offset;
    if (!ext4_offset_range_valid(*offset, fs->group_desc_size)) {
        return 1;
    }

    return 0;
}

static int validate_group_desc(const struct ext4_fs *fs, uint32_t group_index,
                               const struct ext4_group_desc *desc)
{
    uint64_t inode_table_blocks;
    uint64_t inode_bytes;

    if (desc->bg_block_bitmap == 0U || desc->bg_inode_bitmap == 0U ||
        desc->bg_inode_table == 0U) {
        fprintf(stderr, "ext4 group %" PRIu32 ": zero metadata block reference\n",
                group_index);
        return 1;
    }

    if (!ext4_block_index_valid(fs, desc->bg_block_bitmap) ||
        !ext4_block_index_valid(fs, desc->bg_inode_bitmap) ||
        !ext4_block_index_valid(fs, desc->bg_inode_table)) {
        return 1;
    }

    inode_bytes = (uint64_t)fs->superblock.s_inodes_per_group * fs->inode_size;
    inode_table_blocks = (inode_bytes + fs->block_size - 1U) / fs->block_size;
    if (!ext4_block_range_valid(fs, desc->bg_inode_table, inode_table_blocks)) {
        fprintf(stderr, "ext4 group %" PRIu32 ": invalid inode table range\n",
                group_index);
        return 1;
    }

    return 0;
}

int ext4_read_group_desc(const struct ext4_fs *fs, uint32_t group_index,
                         struct ext4_group_desc *desc)
{
    unsigned char *buffer;
    uint64_t offset;
    uint32_t block_bitmap_hi = 0U;
    uint32_t inode_bitmap_hi = 0U;
    uint32_t inode_table_hi = 0U;
    int status = 0;

    if (desc == NULL) {
        fprintf(stderr, "ext4 group descriptor: %s\n", strerror(EINVAL));
        return 1;
    }

    if (descriptor_table_offset(fs, group_index, &offset) != 0) {
        return 1;
    }

    buffer = malloc(fs->group_desc_size);
    if (buffer == NULL) {
        fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
        return 1;
    }

    if (ext4_io_read_exact_at(fs->fd, offset, buffer, fs->group_desc_size,
                              "read ext4 group descriptor") != 0) {
        free(buffer);
        return 1;
    }

    if (fs->group_desc_size >= 64U) {
        block_bitmap_hi = ext4_read_le32(buffer, 32U);
        inode_bitmap_hi = ext4_read_le32(buffer, 36U);
        inode_table_hi = ext4_read_le32(buffer, 40U);
    }

    desc->bg_block_bitmap = combine_block(ext4_read_le32(buffer, 0U),
                                          block_bitmap_hi);
    desc->bg_inode_bitmap = combine_block(ext4_read_le32(buffer, 4U),
                                          inode_bitmap_hi);
    desc->bg_inode_table = combine_block(ext4_read_le32(buffer, 8U),
                                         inode_table_hi);
    desc->bg_free_blocks_count = ext4_read_le16(buffer, 12U);
    desc->bg_free_inodes_count = ext4_read_le16(buffer, 14U);
    desc->bg_used_dirs_count = ext4_read_le16(buffer, 16U);
    desc->bg_flags = ext4_read_le16(buffer, 18U);

    if (validate_group_desc(fs, group_index, desc) != 0) {
        status = 1;
    }

    free(buffer);
    return status;
}

int ext4_get_inode_table(const struct ext4_fs *fs, uint32_t group_index,
                         uint64_t *block_index)
{
    struct ext4_group_desc desc;

    if (block_index == NULL) {
        fprintf(stderr, "ext4 inode table output: %s\n", strerror(EINVAL));
        return 1;
    }

    if (ext4_read_group_desc(fs, group_index, &desc) != 0) {
        return 1;
    }

    *block_index = desc.bg_inode_table;
    return 0;
}
