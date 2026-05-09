#include "ext4_super.h"

#include "errors.h"
#include "ext4_io.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

uint16_t ext4_read_le16(const unsigned char *buffer, uint64_t offset)
{
    return (uint16_t)buffer[offset] |
           (uint16_t)((uint16_t)buffer[offset + 1U] << 8);
}

uint32_t ext4_read_le32(const unsigned char *buffer, uint64_t offset)
{
    return (uint32_t)buffer[offset] |
           ((uint32_t)buffer[offset + 1U] << 8) |
           ((uint32_t)buffer[offset + 2U] << 16) |
           ((uint32_t)buffer[offset + 3U] << 24);
}

uint64_t ext4_read_le64(const unsigned char *buffer, uint64_t offset)
{
    return (uint64_t)ext4_read_le32(buffer, offset) |
           ((uint64_t)ext4_read_le32(buffer, offset + 4U) << 32);
}

static int calculate_group_count(uint64_t blocks_count,
                                 uint32_t blocks_per_group,
                                 uint64_t *groups_count)
{
    if (blocks_per_group == 0U) {
        fprintf(stderr, "ext4 blocks per group: %s\n", strerror(EINVAL));
        return 1;
    }

    *groups_count = (blocks_count + blocks_per_group - 1U) / blocks_per_group;
    if (*groups_count == 0U || *groups_count > UINT32_MAX) {
        fprintf(stderr, "ext4 group count: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    return 0;
}

int ext4_read_superblock_at(int fd, uint64_t offset,
                            struct ext4_superblock *superblock,
                            unsigned char raw[EXT4_SUPERBLOCK_SIZE])
{
    unsigned char buffer[EXT4_SUPERBLOCK_SIZE];

    if (superblock == NULL) {
        fprintf(stderr, "ext4 superblock: %s\n", strerror(EINVAL));
        return 1;
    }

    if (ext4_io_read_exact_at(fd, offset, buffer, sizeof(buffer),
                              "read ext4 superblock") != 0) {
        return 1;
    }

    superblock->s_inodes_count = ext4_read_le32(buffer, 0U);
    superblock->s_blocks_count_lo = ext4_read_le32(buffer, 4U);
    superblock->s_r_blocks_count_lo = ext4_read_le32(buffer, 8U);
    superblock->s_free_blocks_count_lo = ext4_read_le32(buffer, 12U);
    superblock->s_free_inodes_count = ext4_read_le32(buffer, 16U);
    superblock->s_first_data_block = ext4_read_le32(buffer, 20U);
    superblock->s_log_block_size = ext4_read_le32(buffer, 24U);
    superblock->s_log_cluster_size = ext4_read_le32(buffer, 28U);
    superblock->s_blocks_per_group = ext4_read_le32(buffer, 32U);
    superblock->s_clusters_per_group = ext4_read_le32(buffer, 36U);
    superblock->s_inodes_per_group = ext4_read_le32(buffer, 40U);
    superblock->s_mtime = ext4_read_le32(buffer, 44U);
    superblock->s_wtime = ext4_read_le32(buffer, 48U);
    superblock->s_magic = ext4_read_le16(buffer, 56U);
    superblock->s_lastcheck = ext4_read_le32(buffer, 64U);
    superblock->s_rev_level = ext4_read_le32(buffer, 76U);
    superblock->s_first_ino = ext4_read_le32(buffer, 84U);
    superblock->s_inode_size = ext4_read_le16(buffer, 88U);
    superblock->s_feature_compat = ext4_read_le32(buffer, 92U);
    superblock->s_feature_incompat = ext4_read_le32(buffer, 96U);
    superblock->s_feature_ro_compat = ext4_read_le32(buffer, 100U);
    memcpy(superblock->s_uuid, buffer + 104U, sizeof(superblock->s_uuid));
    superblock->s_desc_size = ext4_read_le16(buffer, 254U);
    superblock->s_mkfs_time = ext4_read_le32(buffer, 264U);
    superblock->s_blocks_count_hi = ext4_read_le32(buffer, 336U);
    superblock->s_r_blocks_count_hi = ext4_read_le32(buffer, 340U);
    superblock->s_free_blocks_count_hi = ext4_read_le32(buffer, 344U);
    superblock->s_checksum = ext4_read_le32(buffer, 1020U);

    if (raw != NULL) {
        memcpy(raw, buffer, sizeof(buffer));
    }

    return 0;
}

int ext4_read_superblock(int fd, struct ext4_superblock *superblock)
{
    return ext4_read_superblock_at(fd, EXT4_SUPERBLOCK_OFFSET, superblock, NULL);
}

uint64_t ext4_superblock_block_size(const struct ext4_superblock *superblock)
{
    if (superblock->s_log_block_size > 6U) {
        return 0U;
    }

    return (uint64_t)EXT4_MIN_BLOCK_SIZE << superblock->s_log_block_size;
}

uint64_t ext4_superblock_blocks_count(const struct ext4_superblock *superblock)
{
    uint64_t blocks_count = superblock->s_blocks_count_lo;

    if ((superblock->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) != 0U ||
        superblock->s_blocks_count_hi != 0U) {
        blocks_count |= (uint64_t)superblock->s_blocks_count_hi << 32;
    }

    return blocks_count;
}

uint16_t ext4_superblock_inode_size(const struct ext4_superblock *superblock)
{
    if (superblock->s_inode_size == 0U) {
        return EXT4_GOOD_OLD_INODE_SIZE;
    }

    return superblock->s_inode_size;
}

uint16_t ext4_superblock_group_desc_size(const struct ext4_superblock *superblock)
{
    if ((superblock->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) == 0U) {
        return EXT4_GOOD_OLD_DESC_SIZE;
    }

    if (superblock->s_desc_size == 0U) {
        return 64U;
    }

    return superblock->s_desc_size;
}

int ext4_validate_superblock(const struct ext4_superblock *superblock)
{
    uint64_t block_size;
    uint16_t inode_size;
    uint16_t desc_size;

    if (superblock == NULL) {
        fprintf(stderr, "ext4 superblock: %s\n", strerror(EINVAL));
        return 1;
    }

    if (superblock->s_magic != EXT4_SUPER_MAGIC) {
        fprintf(stderr, "ext4 superblock magic: invalid 0x%04X, expected 0x%04X\n",
                superblock->s_magic, EXT4_SUPER_MAGIC);
        return 1;
    }

    block_size = ext4_superblock_block_size(superblock);
    if (block_size < EXT4_MIN_BLOCK_SIZE || block_size > EXT4_MAX_BLOCK_SIZE) {
        fprintf(stderr, "ext4 block size: %s\n", strerror(EINVAL));
        return 1;
    }

    if (ext4_superblock_blocks_count(superblock) == 0U ||
        superblock->s_inodes_count == 0U ||
        superblock->s_blocks_per_group == 0U ||
        superblock->s_inodes_per_group == 0U) {
        fprintf(stderr, "ext4 superblock counters: %s\n", strerror(EINVAL));
        return 1;
    }

    inode_size = ext4_superblock_inode_size(superblock);
    if (inode_size < EXT4_GOOD_OLD_INODE_SIZE ||
        inode_size > block_size ||
        (inode_size & (uint16_t)(inode_size - 1U)) != 0U) {
        fprintf(stderr, "ext4 inode size: %s\n", strerror(EINVAL));
        return 1;
    }

    desc_size = ext4_superblock_group_desc_size(superblock);
    if (desc_size < EXT4_GOOD_OLD_DESC_SIZE || desc_size > block_size ||
        (desc_size % 8U) != 0U) {
        fprintf(stderr, "ext4 group descriptor size: %s\n", strerror(EINVAL));
        return 1;
    }

    return 0;
}

int ext4_offset_range_valid(uint64_t offset, uint64_t size)
{
    if (offset > (uint64_t)LLONG_MAX || size > (uint64_t)LLONG_MAX ||
        offset > (uint64_t)LLONG_MAX - size) {
        fprintf(stderr, "ext4 byte offset: %s\n", strerror(EOVERFLOW));
        return 0;
    }

    return 1;
}

int ext4_block_index_valid(const struct ext4_fs *fs, uint64_t block_index)
{
    if (fs == NULL || block_index >= fs->blocks_count) {
        fprintf(stderr, "ext4 block index %" PRIu64 ": %s\n",
                block_index, strerror(EINVAL));
        return 0;
    }

    return 1;
}

int ext4_block_range_valid(const struct ext4_fs *fs, uint64_t start_block,
                           uint64_t block_count)
{
    if (fs == NULL || block_count == 0U || start_block >= fs->blocks_count ||
        block_count > fs->blocks_count - start_block) {
        fprintf(stderr, "ext4 block range %" PRIu64 "+%" PRIu64 ": %s\n",
                start_block, block_count, strerror(EINVAL));
        return 0;
    }

    return 1;
}

int ext4_fs_open(const char *path, int readonly, struct ext4_fs *fs)
{
    if (fs == NULL) {
        fprintf(stderr, "ext4 filesystem: %s\n", strerror(EINVAL));
        return 1;
    }

    fs->fd = ext4_io_open(path, readonly ? 0 : 1);
    fs->readonly = readonly ? 1 : 0;
    fs->block_size = 0U;
    fs->blocks_count = 0U;
    fs->groups_count = 0U;
    fs->inode_size = 0U;
    fs->group_desc_size = 0U;

    if (fs->fd < 0) {
        return 1;
    }

    if (ext4_read_superblock(fs->fd, &fs->superblock) != 0 ||
        ext4_validate_superblock(&fs->superblock) != 0) {
        (void)fs_close(fs->fd, path);
        fs->fd = -1;
        return 1;
    }

    fs->block_size = ext4_superblock_block_size(&fs->superblock);
    fs->blocks_count = ext4_superblock_blocks_count(&fs->superblock);
    fs->inode_size = ext4_superblock_inode_size(&fs->superblock);
    fs->group_desc_size = ext4_superblock_group_desc_size(&fs->superblock);

    if (calculate_group_count(fs->blocks_count,
                              fs->superblock.s_blocks_per_group,
                              &fs->groups_count) != 0) {
        (void)fs_close(fs->fd, path);
        fs->fd = -1;
        return 1;
    }

    return 0;
}

int ext4_fs_close(struct ext4_fs *fs, const char *context)
{
    int status;

    if (fs == NULL || fs->fd < 0) {
        return 0;
    }

    status = fs_close(fs->fd, context);
    fs->fd = -1;
    return status;
}

int ext4_print_superblock_info(const char *path)
{
    struct ext4_fs fs;
    int status = 0;

    if (ext4_fs_open(path, 1, &fs) != 0) {
        return 1;
    }

    printf("Ext4 superblock: %s\n", path);
    printf("Magic number: 0x%04X (valid)\n", fs.superblock.s_magic);
    printf("Block size: %" PRIu64 " bytes\n", fs.block_size);
    printf("Inode count: %" PRIu32 "\n", fs.superblock.s_inodes_count);
    printf("Blocks count: %" PRIu64 "\n", fs.blocks_count);
    printf("Blocks per group: %" PRIu32 "\n", fs.superblock.s_blocks_per_group);
    printf("Inodes per group: %" PRIu32 "\n", fs.superblock.s_inodes_per_group);
    printf("Inode size: %" PRIu16 " bytes\n", fs.inode_size);
    printf("Group descriptor size: %" PRIu16 " bytes\n", fs.group_desc_size);
    printf("Block groups: %" PRIu64 "\n", fs.groups_count);

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }

    return status;
}
