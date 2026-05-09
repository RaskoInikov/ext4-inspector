#ifndef EXT4_SUPER_H
#define EXT4_SUPER_H

#include <stdint.h>

#define EXT4_SUPERBLOCK_OFFSET 1024U
#define EXT4_SUPERBLOCK_SIZE 1024U
#define EXT4_SUPER_MAGIC 0xEF53U
#define EXT4_MIN_BLOCK_SIZE 1024U
#define EXT4_MAX_BLOCK_SIZE 65536U
#define EXT4_GOOD_OLD_INODE_SIZE 128U
#define EXT4_GOOD_OLD_DESC_SIZE 32U
#define EXT4_FEATURE_INCOMPAT_64BIT 0x0080U
#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001U
#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400U

struct ext4_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_magic;
    uint32_t s_lastcheck;
    uint32_t s_rev_level;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    unsigned char s_uuid[16];
    uint32_t s_mkfs_time;
    uint16_t s_desc_size;
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint32_t s_checksum;
};

struct ext4_fs {
    int fd;
    int readonly;
    struct ext4_superblock superblock;
    uint64_t block_size;
    uint64_t blocks_count;
    uint64_t groups_count;
    uint16_t inode_size;
    uint16_t group_desc_size;
};

uint16_t ext4_read_le16(const unsigned char *buffer, uint64_t offset);
uint32_t ext4_read_le32(const unsigned char *buffer, uint64_t offset);
uint64_t ext4_read_le64(const unsigned char *buffer, uint64_t offset);
int ext4_read_superblock_at(int fd, uint64_t offset,
                            struct ext4_superblock *superblock,
                            unsigned char raw[EXT4_SUPERBLOCK_SIZE]);
int ext4_read_superblock(int fd, struct ext4_superblock *superblock);
int ext4_validate_superblock(const struct ext4_superblock *superblock);
uint64_t ext4_superblock_block_size(const struct ext4_superblock *superblock);
uint64_t ext4_superblock_blocks_count(const struct ext4_superblock *superblock);
uint16_t ext4_superblock_inode_size(const struct ext4_superblock *superblock);
uint16_t ext4_superblock_group_desc_size(const struct ext4_superblock *superblock);
int ext4_block_index_valid(const struct ext4_fs *fs, uint64_t block_index);
int ext4_block_range_valid(const struct ext4_fs *fs, uint64_t start_block,
                           uint64_t block_count);
int ext4_offset_range_valid(uint64_t offset, uint64_t size);
int ext4_fs_open(const char *path, int readonly, struct ext4_fs *fs);
int ext4_fs_close(struct ext4_fs *fs, const char *context);
int ext4_print_superblock_info(const char *path);

#endif
