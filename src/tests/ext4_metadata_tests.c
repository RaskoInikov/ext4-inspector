#include "errors.h"
#include "ext4_backup.h"
#include "ext4_chain.h"
#include "ext4_dir.h"
#include "ext4_group.h"
#include "ext4_inode.h"
#include "ext4_path.h"
#include "ext4_search.h"
#include "ext4_super.h"
#include "ext4_recovery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_IMAGE_SIZE 32768U
#define TEST_BLOCK_SIZE 1024U
#define TEST_BLOCKS_COUNT 32U
#define TEST_INODES_COUNT 32U
#define TEST_BLOCKS_PER_GROUP 8U
#define TEST_INODES_PER_GROUP 8U
#define TEST_INODE_SIZE 128U

static void put_le16(unsigned char *buffer, uint64_t offset, uint16_t value)
{
    buffer[offset] = (unsigned char)(value & 0xFFU);
    buffer[offset + 1U] = (unsigned char)((value >> 8) & 0xFFU);
}

static void put_le32(unsigned char *buffer, uint64_t offset, uint32_t value)
{
    buffer[offset] = (unsigned char)(value & 0xFFU);
    buffer[offset + 1U] = (unsigned char)((value >> 8) & 0xFFU);
    buffer[offset + 2U] = (unsigned char)((value >> 16) & 0xFFU);
    buffer[offset + 3U] = (unsigned char)((value >> 24) & 0xFFU);
}

static int write_all_at(int fd, uint64_t offset, const void *buffer, size_t size)
{
    size_t total = 0U;

    while (total < size) {
        ssize_t written = pwrite(fd, (const unsigned char *)buffer + total,
                                 size - total, (off_t)(offset + total));

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "test pwrite: %s\n", strerror(errno));
            return 1;
        }

        if (written == 0) {
            fprintf(stderr, "test pwrite: zero byte write\n");
            return 1;
        }

        total += (size_t)written;
    }

    return 0;
}

static int write_superblock(int fd)
{
    unsigned char buffer[EXT4_SUPERBLOCK_SIZE];

    memset(buffer, 0, sizeof(buffer));
    put_le32(buffer, 0U, TEST_INODES_COUNT);
    put_le32(buffer, 4U, TEST_BLOCKS_COUNT);
    put_le32(buffer, 20U, 1U);
    put_le32(buffer, 24U, 0U);
    put_le32(buffer, 32U, TEST_BLOCKS_PER_GROUP);
    put_le32(buffer, 36U, TEST_BLOCKS_PER_GROUP);
    put_le32(buffer, 40U, TEST_INODES_PER_GROUP);
    put_le16(buffer, 56U, EXT4_SUPER_MAGIC);
    put_le32(buffer, 76U, 1U);
    put_le32(buffer, 84U, 11U);
    put_le16(buffer, 88U, TEST_INODE_SIZE);
    put_le32(buffer, 96U, EXT4_FEATURE_INCOMPAT_FILETYPE);
    put_le16(buffer, 254U, EXT4_GOOD_OLD_DESC_SIZE);

    return write_all_at(fd, EXT4_SUPERBLOCK_OFFSET, buffer, sizeof(buffer));
}

static int copy_primary_superblock_to(int fd, uint64_t offset)
{
    unsigned char buffer[EXT4_SUPERBLOCK_SIZE];
    ssize_t bytes_read = pread(fd, buffer, sizeof(buffer),
                               (off_t)EXT4_SUPERBLOCK_OFFSET);

    if (bytes_read != (ssize_t)sizeof(buffer)) {
        fprintf(stderr, "copy primary superblock: %s\n",
                bytes_read < 0 ? strerror(errno) : "short read");
        return 1;
    }

    return write_all_at(fd, offset, buffer, sizeof(buffer));
}

static int write_group_desc(int fd, uint32_t group, uint32_t block_bitmap,
                            uint32_t inode_bitmap, uint32_t inode_table)
{
    unsigned char buffer[EXT4_GOOD_OLD_DESC_SIZE];
    uint64_t offset = 2U * TEST_BLOCK_SIZE +
                      (uint64_t)group * EXT4_GOOD_OLD_DESC_SIZE;

    memset(buffer, 0, sizeof(buffer));
    put_le32(buffer, 0U, block_bitmap);
    put_le32(buffer, 4U, inode_bitmap);
    put_le32(buffer, 8U, inode_table);
    put_le16(buffer, 12U, 4U);
    put_le16(buffer, 14U, 4U);
    put_le16(buffer, 16U, 1U);

    return write_all_at(fd, offset, buffer, sizeof(buffer));
}

static int write_inode_blocks(int fd, uint32_t inode_number, uint16_t mode,
                              const uint32_t blocks[EXT4_N_BLOCKS],
                              uint32_t size, uint16_t links_count)
{
    unsigned char buffer[TEST_INODE_SIZE];
    uint32_t zero_based = inode_number - 1U;
    uint32_t index = zero_based % TEST_INODES_PER_GROUP;
    uint64_t offset = 5U * TEST_BLOCK_SIZE + (uint64_t)index * TEST_INODE_SIZE;

    memset(buffer, 0, sizeof(buffer));
    put_le16(buffer, 0U, mode);
    put_le16(buffer, 2U, 1000U);
    put_le32(buffer, 4U, size);
    put_le32(buffer, 8U, 1U);
    put_le32(buffer, 12U, 2U);
    put_le32(buffer, 16U, 3U);
    put_le16(buffer, 24U, 1000U);
    put_le16(buffer, 26U, links_count);
    put_le32(buffer, 28U, 2U);
    for (uint32_t i = 0U; i < EXT4_N_BLOCKS; ++i) {
        put_le32(buffer, 40U + (uint64_t)i * 4U, blocks[i]);
    }

    return write_all_at(fd, offset, buffer, sizeof(buffer));
}

static int write_inode_record(int fd, uint32_t inode_number, uint16_t mode,
                              uint32_t block_ref, uint32_t size,
                              uint16_t links_count)
{
    uint32_t blocks[EXT4_N_BLOCKS];

    memset(blocks, 0, sizeof(blocks));
    blocks[0] = block_ref;
    return write_inode_blocks(fd, inode_number, mode, blocks, size,
                              links_count);
}

static void write_dir_entry(unsigned char *block, uint32_t offset,
                            uint32_t inode, uint16_t rec_len,
                            const char *name, uint8_t file_type)
{
    size_t name_len = strlen(name);

    put_le32(block, offset, inode);
    put_le16(block, offset + 4U, rec_len);
    block[offset + 6U] = (unsigned char)name_len;
    block[offset + 7U] = file_type;
    memcpy(block + offset + 8U, name, name_len);
}

static int write_directory_blocks(int fd, int corrupt_root)
{
    unsigned char root[TEST_BLOCK_SIZE];
    unsigned char sub[TEST_BLOCK_SIZE];
    unsigned char nested[TEST_BLOCK_SIZE];

    memset(root, 0, sizeof(root));
    memset(sub, 0, sizeof(sub));
    memset(nested, 0, sizeof(nested));

    if (corrupt_root) {
        write_dir_entry(root, 0U, 2U, 7U, ".", EXT4_FT_DIR);
    } else {
        write_dir_entry(root, 0U, 2U, 12U, ".", EXT4_FT_DIR);
        write_dir_entry(root, 12U, 2U, 12U, "..", EXT4_FT_DIR);
        write_dir_entry(root, 24U, 3U, 12U, "file", EXT4_FT_REG_FILE);
        write_dir_entry(root, 36U, 4U, 12U, "sub", EXT4_FT_DIR);
        write_dir_entry(root, 48U, 6U, TEST_BLOCK_SIZE - 48U, "nested",
                        EXT4_FT_DIR);
    }

    write_dir_entry(sub, 0U, 4U, 12U, ".", EXT4_FT_DIR);
    write_dir_entry(sub, 12U, 2U, 12U, "..", EXT4_FT_DIR);
    write_dir_entry(sub, 24U, 5U, 12U, "deep", EXT4_FT_REG_FILE);
    write_dir_entry(sub, 36U, 2U, TEST_BLOCK_SIZE - 36U, "back", EXT4_FT_DIR);

    write_dir_entry(nested, 0U, 6U, 12U, ".", EXT4_FT_DIR);
    write_dir_entry(nested, 12U, 2U, 12U, "..", EXT4_FT_DIR);
    write_dir_entry(nested, 24U, 4U, TEST_BLOCK_SIZE - 24U, "level2",
                    EXT4_FT_DIR);

    if (write_all_at(fd, 7U * TEST_BLOCK_SIZE, root, sizeof(root)) != 0 ||
        write_all_at(fd, 8U * TEST_BLOCK_SIZE, sub, sizeof(sub)) != 0 ||
        write_all_at(fd, 9U * TEST_BLOCK_SIZE, nested, sizeof(nested)) != 0) {
        return 1;
    }

    return 0;
}

static int create_image(char *path, size_t path_size, uint32_t inode_block_ref,
                        uint32_t group0_inode_table, int corrupt_root)
{
    char template[] = "/tmp/fs_tool_ext4_test_XXXXXX";
    int fd = mkstemp(template);
    uint32_t group;

    if (fd < 0) {
        fprintf(stderr, "mkstemp: %s\n", strerror(errno));
        return -1;
    }

    if (snprintf(path, path_size, "%s", template) >= (int)path_size) {
        fprintf(stderr, "test image path truncated\n");
        (void)fs_close(fd, template);
        return -1;
    }

    if (ftruncate(fd, TEST_IMAGE_SIZE) != 0) {
        fprintf(stderr, "ftruncate: %s\n", strerror(errno));
        (void)fs_close(fd, template);
        return -1;
    }

    if (write_superblock(fd) != 0) {
        (void)fs_close(fd, template);
        return -1;
    }

    for (group = 0U; group < 4U; ++group) {
        uint32_t base = 3U + group * TEST_BLOCKS_PER_GROUP;
        uint32_t inode_table = (group == 0U) ? group0_inode_table : base + 2U;

        if (write_group_desc(fd, group, base, base + 1U, inode_table) != 0) {
            (void)fs_close(fd, template);
            return -1;
        }
    }

    if (write_inode_record(fd, 2U, EXT4_S_IFDIR | 0755U, inode_block_ref,
                           TEST_BLOCK_SIZE, 2U) != 0 ||
        write_inode_record(fd, 3U, EXT4_S_IFREG | 0644U, 0U, 0U, 1U) != 0 ||
        write_inode_record(fd, 4U, EXT4_S_IFDIR | 0755U, 8U,
                           TEST_BLOCK_SIZE, 2U) != 0 ||
        write_inode_record(fd, 5U, EXT4_S_IFREG | 0644U, 0U, 0U, 1U) != 0 ||
        write_inode_record(fd, 6U, EXT4_S_IFDIR | 0755U, 9U,
                           TEST_BLOCK_SIZE, 2U) != 0 ||
        write_directory_blocks(fd, corrupt_root) != 0) {
        (void)fs_close(fd, template);
        return -1;
    }

    if (fs_close(fd, template) != 0) {
        return -1;
    }

    return 0;
}

static int create_chain_image(char *path, size_t path_size)
{
    char template[] = "/tmp/fs_tool_ext4_chain_test_XXXXXX";
    int fd = mkstemp(template);
    uint32_t group;
    uint32_t file7[EXT4_N_BLOCKS];
    uint32_t file8[EXT4_N_BLOCKS];
    uint32_t file9[EXT4_N_BLOCKS];
    uint32_t file10[EXT4_N_BLOCKS];
    unsigned char indirect[TEST_BLOCK_SIZE];

    if (fd < 0) {
        fprintf(stderr, "mkstemp: %s\n", strerror(errno));
        return -1;
    }
    if (snprintf(path, path_size, "%s", template) >= (int)path_size) {
        fprintf(stderr, "test image path truncated\n");
        (void)fs_close(fd, template);
        return -1;
    }
    if (ftruncate(fd, TEST_IMAGE_SIZE) != 0 || write_superblock(fd) != 0) {
        fprintf(stderr, "chain image setup: %s\n", strerror(errno));
        (void)fs_close(fd, template);
        return -1;
    }
    for (group = 0U; group < 4U; ++group) {
        uint32_t base = 3U + group * TEST_BLOCKS_PER_GROUP;

        if (write_group_desc(fd, group, base, base + 1U, group == 0U ? 5U : base + 2U) != 0) {
            (void)fs_close(fd, template);
            return -1;
        }
    }

    memset(file7, 0, sizeof(file7));
    memset(file8, 0, sizeof(file8));
    memset(file9, 0, sizeof(file9));
    memset(file10, 0, sizeof(file10));
    memset(indirect, 0, sizeof(indirect));
    file7[0] = 11U;
    file7[1] = 13U;
    file8[0] = 13U;
    file8[1] = 14U;
    file9[0] = 99U;
    file10[12] = 15U;
    put_le32(indirect, 0U, 16U);
    put_le32(indirect, 4U, 15U);
    if (write_inode_blocks(fd, 7U, EXT4_S_IFREG | 0644U, file7,
                           TEST_BLOCK_SIZE * 2U, 1U) != 0 ||
        write_inode_blocks(fd, 8U, EXT4_S_IFREG | 0644U, file8,
                           TEST_BLOCK_SIZE * 2U, 1U) != 0 ||
        write_inode_blocks(fd, 6U, EXT4_S_IFREG | 0644U, file9,
                           TEST_BLOCK_SIZE, 1U) != 0 ||
        write_inode_blocks(fd, 5U, EXT4_S_IFREG | 0644U, file10,
                           TEST_BLOCK_SIZE, 1U) != 0 ||
        write_all_at(fd, 15U * TEST_BLOCK_SIZE, indirect, sizeof(indirect)) != 0) {
        (void)fs_close(fd, template);
        return -1;
    }
    if (fs_close(fd, template) != 0) {
        return -1;
    }

    return 0;
}

static int create_backup_image(char *path, size_t path_size)
{
    char template[] = "/tmp/fs_tool_ext4_backup_test_XXXXXX";
    int fd = mkstemp(template);

    if (fd < 0) {
        fprintf(stderr, "mkstemp: %s\n", strerror(errno));
        return -1;
    }
    if (snprintf(path, path_size, "%s", template) >= (int)path_size) {
        fprintf(stderr, "test image path truncated\n");
        (void)fs_close(fd, template);
        return -1;
    }
    if (ftruncate(fd, TEST_IMAGE_SIZE) != 0 || write_superblock(fd) != 0 ||
        copy_primary_superblock_to(fd, 9U * TEST_BLOCK_SIZE) != 0 ||
        copy_primary_superblock_to(fd, 25U * TEST_BLOCK_SIZE) != 0) {
        fprintf(stderr, "backup image setup: %s\n", strerror(errno));
        (void)fs_close(fd, template);
        return -1;
    }
    if (fs_close(fd, template) != 0) {
        return -1;
    }

    return 0;
}

static int test_valid_image(void)
{
    char path[128];
    struct ext4_fs fs = {.fd = -1};
    struct ext4_group_desc desc;
    struct ext4_inode inode;
    int status = 0;

    if (create_image(path, sizeof(path), 7U, 5U, 0) != 0) {
        return 1;
    }

    if (ext4_fs_open(path, 1, &fs) != 0 ||
        ext4_read_group_desc(&fs, 0U, &desc) != 0 ||
        ext4_read_inode(&fs, 2U, &inode) != 0) {
        status = 1;
    } else if (desc.bg_inode_table != 5U ||
               inode.type != EXT4_INODE_TYPE_DIRECTORY ||
               inode.permissions != 0755U ||
               inode.block[0] != 7U) {
        fprintf(stderr, "valid image decoded unexpected metadata\n");
        status = 1;
    }

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_corrupted_inode_image(void)
{
    char path[128];
    struct ext4_fs fs = {.fd = -1};
    struct ext4_inode inode;
    int status = 0;

    if (create_image(path, sizeof(path), 99U, 5U, 0) != 0) {
        return 1;
    }

    if (ext4_fs_open(path, 1, &fs) != 0) {
        status = 1;
    } else if (ext4_read_inode(&fs, 2U, &inode) == 0) {
        fprintf(stderr, "corrupted inode image unexpectedly succeeded\n");
        status = 1;
    }

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_invalid_group_descriptor_image(void)
{
    char path[128];
    struct ext4_fs fs = {.fd = -1};
    struct ext4_group_desc desc;
    int status = 0;

    if (create_image(path, sizeof(path), 7U, 99U, 0) != 0) {
        return 1;
    }

    if (ext4_fs_open(path, 1, &fs) != 0) {
        status = 1;
    } else if (ext4_read_group_desc(&fs, 0U, &desc) == 0) {
        fprintf(stderr, "invalid group descriptor unexpectedly succeeded\n");
        status = 1;
    }

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_directory_navigation(void)
{
    char path[128];
    struct ext4_fs fs = {.fd = -1};
    uint32_t resolved = 0U;
    int status = 0;

    if (create_image(path, sizeof(path), 7U, 5U, 0) != 0) {
        return 1;
    }

    if (ext4_fs_open(path, 1, &fs) != 0 ||
        ext4_resolve_path(&fs, EXT4_ROOT_INO, "/sub/deep", &resolved) != 0 ||
        resolved != 5U ||
        ext4_resolve_path(&fs, EXT4_ROOT_INO, "nested/level2/deep",
                          &resolved) != 0 ||
        resolved != 5U ||
        ext4_print_directory_path(&fs, "/") != 0 ||
        ext4_walk_directory_path(&fs, "/") != 0 ||
        ext4_search_filename(&fs, "/", "deep") != 0 ||
        ext4_search_inode_number(&fs, "/", 5U) != 0) {
        status = 1;
    }

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_corrupted_directory_entries(void)
{
    char path[128];
    struct ext4_fs fs = {.fd = -1};
    int status = 0;

    if (create_image(path, sizeof(path), 7U, 5U, 1) != 0) {
        return 1;
    }

    if (ext4_fs_open(path, 1, &fs) != 0 ||
        ext4_search_corrupted_entries(&fs, "/") != 0) {
        status = 1;
    }

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_block_chain_analysis(void)
{
    char path[128];
    struct ext4_fs fs = {.fd = -1};
    struct ext4_inode_chain chain;
    struct ext4_chain_analysis analysis;
    int status = 0;

    if (create_chain_image(path, sizeof(path)) != 0) {
        return 1;
    }

    if (ext4_fs_open(path, 1, &fs) != 0) {
        status = 1;
    } else {
        if (ext4_build_chain_for_inode(&fs, 7U, &chain) != 0 ||
            chain.ref_count != 2U ||
            chain.refs[0].block_index != 11U ||
            chain.refs[1].block_index != 13U) {
            fprintf(stderr, "direct chain analysis failed\n");
            status = 1;
        }
        ext4_inode_chain_free(&chain);

        if (ext4_build_chain_for_inode(&fs, 5U, &chain) != 0 ||
            chain.corruption_count == 0 ||
            chain.ref_count != 2U) {
            fprintf(stderr, "indirect corruption analysis failed\n");
            status = 1;
        }
        ext4_inode_chain_free(&chain);

        if (ext4_build_chain_analysis(&fs, &analysis) != 0 ||
            analysis.owner_count == 0U ||
            analysis.corruption_count == 0) {
            fprintf(stderr, "global chain analysis failed\n");
            status = 1;
        } else {
            int found_shared = 0;
            for (size_t i = 0U; i < analysis.owner_count; ++i) {
                if (analysis.owners[i].block_index == 13U &&
                    analysis.owners[i].owner_count == 2U) {
                    found_shared = 1;
                }
            }
            if (!found_shared) {
                fprintf(stderr, "shared block was not detected\n");
                status = 1;
            }
        }
        ext4_chain_analysis_free(&analysis);
    }

    if (ext4_fs_close(&fs, path) != 0) {
        status = 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_backup_superblocks(void)
{
    char path[128];
    struct ext4_backup_report report;
    int status = 0;

    if (create_backup_image(path, sizeof(path)) != 0) {
        return 1;
    }

    if (ext4_backup_discover(path, 0, &report) != 0 ||
        report.count != 2U ||
        !report.items[0].magic_ok ||
        !report.items[1].magic_ok) {
        fprintf(stderr, "backup discovery failed\n");
        status = 1;
    }
    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);

    if (status == 0) {
        int fd = open(path, O_RDWR);

        if (fd < 0) {
            fprintf(stderr, "open: %s\n", strerror(errno));
            status = 1;
        } else if (write_all_at(fd, EXT4_SUPERBLOCK_OFFSET + 56U,
                                "\0\0", 2U) != 0 ||
                   fs_close(fd, path) != 0) {
            status = 1;
        }
    }

    if (status == 0 &&
        (ext4_backup_discover(path, 0, &report) != 0 ||
         report.primary.magic_ok ||
         !report.items[0].magic_ok)) {
        fprintf(stderr, "corrupted primary backup discovery failed\n");
        status = 1;
    }
    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);

    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_corrupted_backup_and_readonly_restore(void)
{
    char path[128];
    struct ext4_backup_report report;
    int fd;
    int status = 0;

    if (create_backup_image(path, sizeof(path)) != 0) {
        return 1;
    }

    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open: %s\n", strerror(errno));
        return 1;
    }
    if (write_all_at(fd, 9U * TEST_BLOCK_SIZE + 56U, "\0\0", 2U) != 0 ||
        fs_close(fd, path) != 0) {
        status = 1;
    }

    if (status == 0 &&
        (ext4_backup_discover(path, 0, &report) != 0 ||
         report.items[0].magic_ok ||
         !report.items[1].magic_ok)) {
        fprintf(stderr, "corrupted backup detection failed\n");
        status = 1;
    }
    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);

    if (status == 0 &&
        ext4_recovery_restore_superblock(path, "1", 0) != 0) {
        fprintf(stderr, "readonly restore preview failed\n");
        status = 1;
    }

    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

static int test_unreadable_backup_copy(void)
{
    char path[128];
    struct ext4_backup_report report;
    int fd;
    int status = 0;

    if (create_backup_image(path, sizeof(path)) != 0) {
        return 1;
    }

    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open: %s\n", strerror(errno));
        return 1;
    }
    if (ftruncate(fd, 16U * TEST_BLOCK_SIZE) != 0 || fs_close(fd, path) != 0) {
        fprintf(stderr, "truncate backup image: %s\n", strerror(errno));
        status = 1;
    }

    if (status == 0 &&
        (ext4_backup_discover(path, 0, &report) != 0 ||
         report.count != 2U ||
         report.items[1].read_ok)) {
        fprintf(stderr, "unreadable backup was not reported safely\n");
        status = 1;
    }
    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);

    if (unlink(path) != 0) {
        fprintf(stderr, "unlink: %s\n", strerror(errno));
        status = 1;
    }

    return status;
}

int main(void)
{
    if (test_valid_image() != 0 ||
        test_corrupted_inode_image() != 0 ||
        test_invalid_group_descriptor_image() != 0 ||
        test_directory_navigation() != 0 ||
        test_corrupted_directory_entries() != 0 ||
        test_block_chain_analysis() != 0 ||
        test_backup_superblocks() != 0 ||
        test_corrupted_backup_and_readonly_restore() != 0 ||
        test_unreadable_backup_copy() != 0) {
        return 1;
    }

    printf("ext4 metadata tests passed\n");
    return 0;
}
