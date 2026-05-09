#include "ext4_inode.h"

#include "ext4_group.h"
#include "ext4_io.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum ext4_inode_type detect_inode_type(uint16_t mode)
{
    switch (mode & EXT4_S_IFMT) {
    case EXT4_S_IFREG:
        return EXT4_INODE_TYPE_REGULAR;
    case EXT4_S_IFDIR:
        return EXT4_INODE_TYPE_DIRECTORY;
    case EXT4_S_IFLNK:
        return EXT4_INODE_TYPE_SYMLINK;
    default:
        return EXT4_INODE_TYPE_UNKNOWN;
    }
}

const char *ext4_inode_type_name(enum ext4_inode_type type)
{
    switch (type) {
    case EXT4_INODE_TYPE_REGULAR:
        return "regular file";
    case EXT4_INODE_TYPE_DIRECTORY:
        return "directory";
    case EXT4_INODE_TYPE_SYMLINK:
        return "symbolic link";
    case EXT4_INODE_TYPE_UNKNOWN:
    default:
        return "unknown";
    }
}

static int validate_inode_number(const struct ext4_fs *fs,
                                 uint32_t inode_number)
{
    if (fs == NULL || inode_number == 0U ||
        inode_number > fs->superblock.s_inodes_count) {
        fprintf(stderr, "ext4 inode %" PRIu32 ": %s\n",
                inode_number, strerror(EINVAL));
        return 1;
    }

    if (fs->superblock.s_inodes_per_group == 0U) {
        fprintf(stderr, "ext4 inodes per group: %s\n", strerror(EINVAL));
        return 1;
    }

    return 0;
}

int ext4_inode_offset(const struct ext4_fs *fs, uint32_t inode_number,
                      uint64_t *offset)
{
    uint32_t zero_based = inode_number - 1U;
    uint32_t group_index = zero_based / fs->superblock.s_inodes_per_group;
    uint32_t index_in_group = zero_based % fs->superblock.s_inodes_per_group;
    uint64_t inode_table;
    uint64_t table_offset;
    uint64_t inode_offset_in_table;

    if (group_index >= fs->groups_count) {
        fprintf(stderr, "ext4 inode group %" PRIu32 ": %s\n",
                group_index, strerror(EINVAL));
        return 1;
    }

    if (ext4_get_inode_table(fs, group_index, &inode_table) != 0) {
        return 1;
    }

    if (inode_table > UINT64_MAX / fs->block_size ||
        index_in_group > UINT64_MAX / fs->inode_size) {
        fprintf(stderr, "ext4 inode offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    table_offset = inode_table * fs->block_size;
    inode_offset_in_table = (uint64_t)index_in_group * fs->inode_size;
    if (table_offset > UINT64_MAX - inode_offset_in_table) {
        fprintf(stderr, "ext4 inode offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    *offset = table_offset + inode_offset_in_table;
    if (!ext4_offset_range_valid(*offset, fs->inode_size)) {
        return 1;
    }

    return 0;
}

static int validate_extent_block(const struct ext4_fs *fs, uint64_t start,
                                 uint32_t length)
{
    if (start == 0U || length == 0U) {
        return 0;
    }

    return ext4_block_range_valid(fs, start, length) ? 0 : 1;
}

static int validate_extent_tree(const struct ext4_fs *fs,
                                const unsigned char *block_data)
{
    uint16_t magic = ext4_read_le16(block_data, 0U);
    uint16_t entries = ext4_read_le16(block_data, 2U);
    uint16_t max_entries = ext4_read_le16(block_data, 4U);
    uint16_t depth = ext4_read_le16(block_data, 6U);
    uint64_t entries_bytes;
    uint16_t i;

    if (magic != EXT4_EXTENT_HEADER_MAGIC || entries > max_entries) {
        fprintf(stderr, "ext4 extent header: %s\n", strerror(EINVAL));
        return 1;
    }

    entries_bytes = 12U + (uint64_t)entries * 12U;
    if (entries_bytes > EXT4_N_BLOCKS * 4U) {
        fprintf(stderr, "ext4 extent entries: %s\n", strerror(EINVAL));
        return 1;
    }

    if (depth == 0U) {
        for (i = 0U; i < entries; ++i) {
            uint64_t offset = 12U + (uint64_t)i * 12U;
            uint32_t len_raw = ext4_read_le16(block_data, offset + 4U);
            uint32_t length = len_raw & 0x7FFFU;
            uint64_t start = ((uint64_t)ext4_read_le16(block_data, offset + 6U) << 32) |
                             ext4_read_le32(block_data, offset + 8U);

            if (validate_extent_block(fs, start, length) != 0) {
                return 1;
            }
        }
    } else {
        for (i = 0U; i < entries; ++i) {
            uint64_t offset = 12U + (uint64_t)i * 12U;
            uint64_t leaf = ((uint64_t)ext4_read_le16(block_data, offset + 10U) << 32) |
                            ext4_read_le32(block_data, offset + 4U);

            if (!ext4_block_index_valid(fs, leaf)) {
                return 1;
            }
        }
    }

    return 0;
}

static int validate_inode_blocks(const struct ext4_fs *fs,
                                 const struct ext4_inode *inode,
                                 const unsigned char *raw_inode)
{
    uint32_t i;

    if (inode->mode == 0U) {
        fprintf(stderr, "ext4 inode %" PRIu32 ": unallocated inode\n",
                inode->inode_number);
        return 1;
    }

    if (inode->type == EXT4_INODE_TYPE_SYMLINK && inode->size <= 60U &&
        inode->blocks_count == 0U) {
        return 0;
    }

    if ((inode->flags & EXT4_EXTENTS_FL) != 0U) {
        return validate_extent_tree(fs, raw_inode + 40U);
    }

    for (i = 0U; i < EXT4_N_BLOCKS; ++i) {
        if (inode->block[i] != 0U &&
            !ext4_block_index_valid(fs, inode->block[i])) {
            return 1;
        }
    }

    return 0;
}

static void decode_inode(uint32_t inode_number, const unsigned char *buffer,
                         struct ext4_inode *inode)
{
    uint32_t uid_low;
    uint32_t uid_high;
    uint32_t gid_low;
    uint32_t gid_high;
    uint32_t i;

    inode->inode_number = inode_number;
    inode->mode = ext4_read_le16(buffer, 0U);
    inode->type = detect_inode_type(inode->mode);
    inode->permissions = inode->mode & 07777U;
    uid_low = ext4_read_le16(buffer, 2U);
    gid_low = ext4_read_le16(buffer, 24U);
    uid_high = ext4_read_le16(buffer, 120U);
    gid_high = ext4_read_le16(buffer, 122U);
    inode->uid = uid_low | (uid_high << 16);
    inode->gid = gid_low | (gid_high << 16);
    inode->size = ext4_read_le32(buffer, 4U);
    if (inode->type == EXT4_INODE_TYPE_REGULAR ||
        inode->type == EXT4_INODE_TYPE_DIRECTORY ||
        inode->type == EXT4_INODE_TYPE_SYMLINK) {
        inode->size |= (uint64_t)ext4_read_le32(buffer, 108U) << 32;
    }
    inode->atime = ext4_read_le32(buffer, 8U);
    inode->ctime = ext4_read_le32(buffer, 12U);
    inode->mtime = ext4_read_le32(buffer, 16U);
    inode->dtime = ext4_read_le32(buffer, 20U);
    inode->links_count = ext4_read_le16(buffer, 26U);
    inode->blocks_count = ext4_read_le32(buffer, 28U);
    inode->flags = ext4_read_le32(buffer, 32U);

    for (i = 0U; i < EXT4_N_BLOCKS; ++i) {
        inode->block[i] = ext4_read_le32(buffer, 40U + (uint64_t)i * 4U);
    }
}

int ext4_read_inode(const struct ext4_fs *fs, uint32_t inode_number,
                    struct ext4_inode *inode)
{
    unsigned char *buffer;
    uint64_t offset;
    int status = 0;

    if (inode == NULL) {
        fprintf(stderr, "ext4 inode output: %s\n", strerror(EINVAL));
        return 1;
    }

    if (validate_inode_number(fs, inode_number) != 0 ||
        ext4_inode_offset(fs, inode_number, &offset) != 0) {
        return 1;
    }

    buffer = malloc(fs->inode_size);
    if (buffer == NULL) {
        fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
        return 1;
    }

    if (ext4_io_read_exact_at(fs->fd, offset, buffer, fs->inode_size,
                              "read ext4 inode") != 0) {
        free(buffer);
        return 1;
    }

    decode_inode(inode_number, buffer, inode);
    if (validate_inode_blocks(fs, inode, buffer) != 0) {
        status = 1;
    }

    free(buffer);
    return status;
}
