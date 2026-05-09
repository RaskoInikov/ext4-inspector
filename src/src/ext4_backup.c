#include "ext4_backup.h"

#include "errors.h"
#include "ext4_io.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_power_of(uint64_t value, uint64_t base)
{
    if (value < 1U) {
        return 0;
    }

    while ((value % base) == 0U) {
        value /= base;
    }

    return value == 1U;
}

static int has_sparse_super(uint64_t group)
{
    return group == 0U || group == 1U ||
           is_power_of(group, 3U) ||
           is_power_of(group, 5U) ||
           is_power_of(group, 7U);
}

static uint32_t crc32c_update(uint32_t crc, const unsigned char *data,
                              size_t size)
{
    size_t i;

    crc = ~crc;
    for (i = 0U; i < size; ++i) {
        unsigned int bit;

        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0x82F63B78U;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static uint32_t superblock_checksum(const struct ext4_superblock *superblock,
                                    const unsigned char raw[EXT4_SUPERBLOCK_SIZE])
{
    unsigned char copy[EXT4_SUPERBLOCK_SIZE];
    uint32_t crc;

    memcpy(copy, raw, sizeof(copy));
    copy[1020U] = 0U;
    copy[1021U] = 0U;
    copy[1022U] = 0U;
    copy[1023U] = 0U;
    crc = crc32c_update(0xFFFFFFFFU, superblock->s_uuid,
                        sizeof(superblock->s_uuid));
    return crc32c_update(crc, copy, sizeof(copy));
}

static int safe_group_count(const struct ext4_superblock *superblock,
                            uint64_t *block_size, uint64_t *blocks_count,
                            uint64_t *groups_count)
{
    *block_size = ext4_superblock_block_size(superblock);
    *blocks_count = ext4_superblock_blocks_count(superblock);

    if (*block_size < EXT4_MIN_BLOCK_SIZE ||
        *block_size > EXT4_MAX_BLOCK_SIZE ||
        *blocks_count == 0U ||
        superblock->s_blocks_per_group == 0U) {
        fprintf(stderr, "backup discovery geometry: %s\n", strerror(EINVAL));
        return 1;
    }

    *groups_count = (*blocks_count + superblock->s_blocks_per_group - 1U) /
                    superblock->s_blocks_per_group;
    if (*groups_count == 0U || *groups_count > UINT32_MAX) {
        fprintf(stderr, "backup discovery group count: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    return 0;
}

static int backup_offset(const struct ext4_superblock *superblock,
                         uint64_t group, uint64_t block_size,
                         uint64_t *block, uint64_t *offset)
{
    uint64_t group_start;

    if (group > UINT64_MAX / superblock->s_blocks_per_group) {
        fprintf(stderr, "backup group offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    group_start = group * superblock->s_blocks_per_group;
    if (group_start > UINT64_MAX - superblock->s_first_data_block) {
        fprintf(stderr, "backup block offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    *block = group_start + superblock->s_first_data_block;
    if (*block > UINT64_MAX / block_size) {
        fprintf(stderr, "backup byte offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    *offset = *block * block_size;
    if (!ext4_offset_range_valid(*offset, EXT4_SUPERBLOCK_SIZE)) {
        return 1;
    }

    if ((*offset % block_size) != 0U) {
        fprintf(stderr, "backup byte offset: %s\n", strerror(EINVAL));
        return 1;
    }

    return 0;
}

static int append_backup(struct ext4_backup_report *report,
                         const struct ext4_backup_superblock *backup)
{
    struct ext4_backup_superblock *resized;
    size_t new_capacity;

    if (report->count == report->capacity) {
        new_capacity = (report->capacity == 0U) ? 8U : report->capacity * 2U;
        resized = realloc(report->items, new_capacity * sizeof(*report->items));
        if (resized == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
            return 1;
        }
        report->items = resized;
        report->capacity = new_capacity;
    }

    report->items[report->count] = *backup;
    report->items[report->count].index = report->count;
    ++report->count;
    return 0;
}

static void validate_backup(struct ext4_backup_superblock *backup)
{
    int should_check_checksum;

    backup->magic_ok = backup->superblock.s_magic == EXT4_SUPER_MAGIC;
    backup->structure_ok = backup->magic_ok &&
                           ext4_validate_superblock(&backup->superblock) == 0;
    should_check_checksum =
        (backup->superblock.s_feature_ro_compat &
         EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) != 0U ||
        backup->superblock.s_checksum != 0U;
    backup->checksum_checked = should_check_checksum;
    if (should_check_checksum) {
        backup->computed_checksum =
            superblock_checksum(&backup->superblock, backup->raw);
        backup->checksum_ok =
            backup->computed_checksum == backup->superblock.s_checksum;
    }
}

static int read_backup_at(int fd, uint64_t group, uint64_t block,
                          uint64_t offset, int is_primary,
                          struct ext4_backup_superblock *backup)
{
    memset(backup, 0, sizeof(*backup));
    backup->group = group;
    backup->block = block;
    backup->offset = offset;
    backup->is_primary = is_primary;

    if (ext4_read_superblock_at(fd, offset, &backup->superblock,
                                backup->raw) != 0) {
        backup->read_ok = 0;
        return 1;
    }

    backup->read_ok = 1;
    validate_backup(backup);
    return 0;
}

int ext4_backup_discover(const char *path, int writable,
                         struct ext4_backup_report *report)
{
    uint64_t block_size;
    uint64_t blocks_count;
    uint64_t groups_count;
    uint64_t group;
    int status = 0;

    memset(report, 0, sizeof(*report));
    report->fd = -1;
    report->fd = ext4_io_open(path, writable);
    if (report->fd < 0) {
        return 1;
    }

    if (read_backup_at(report->fd, 0U, 0U, EXT4_SUPERBLOCK_OFFSET, 1,
                       &report->primary) != 0) {
        (void)fs_close(report->fd, path);
        report->fd = -1;
        return 1;
    }

    if (safe_group_count(&report->primary.superblock, &block_size,
                         &blocks_count, &groups_count) != 0) {
        (void)blocks_count;
        (void)fs_close(report->fd, path);
        report->fd = -1;
        return 1;
    }

    for (group = 1U; group < groups_count; ++group) {
        struct ext4_backup_superblock backup;
        uint64_t block;
        uint64_t offset;

        if (!has_sparse_super(group)) {
            continue;
        }

        if (backup_offset(&report->primary.superblock, group, block_size,
                          &block, &offset) != 0) {
            status = 1;
            continue;
        }

        if (read_backup_at(report->fd, group, block, offset, 0, &backup) != 0) {
            backup.read_ok = 0;
        }

        if (append_backup(report, &backup) != 0) {
            status = 1;
            break;
        }
    }

    return status;
}

void ext4_backup_report_close(struct ext4_backup_report *report,
                              const char *path)
{
    if (report != NULL && report->fd >= 0) {
        (void)fs_close(report->fd, path);
        report->fd = -1;
    }
}

void ext4_backup_report_free(struct ext4_backup_report *report)
{
    if (report == NULL) {
        return;
    }

    free(report->items);
    report->items = NULL;
    report->count = 0U;
    report->capacity = 0U;
}

static void print_uuid(const unsigned char uuid[16])
{
    printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           uuid[0], uuid[1], uuid[2], uuid[3],
           uuid[4], uuid[5], uuid[6], uuid[7],
           uuid[8], uuid[9], uuid[10], uuid[11],
           uuid[12], uuid[13], uuid[14], uuid[15]);
}

static void print_backup_summary(const struct ext4_backup_superblock *backup)
{
    printf("[%zu] group=%" PRIu64 " block=%" PRIu64 " offset=%" PRIu64 "%s\n",
           backup->index, backup->group, backup->block, backup->offset,
           backup->is_primary ? " primary" : "");
    if (!backup->read_ok) {
        printf("  status: unreadable\n");
        return;
    }
    printf("  magic: 0x%04X %s\n", backup->superblock.s_magic,
           backup->magic_ok ? "valid" : "invalid");
    printf("  structure: %s\n", backup->structure_ok ? "valid" : "invalid");
    printf("  blocks=%" PRIu64 " inodes=%" PRIu32 " block_size=%" PRIu64 "\n",
           ext4_superblock_blocks_count(&backup->superblock),
           backup->superblock.s_inodes_count,
           ext4_superblock_block_size(&backup->superblock));
    printf("  features: compat=0x%08" PRIX32 " incompat=0x%08" PRIX32
           " ro=0x%08" PRIX32 "\n",
           backup->superblock.s_feature_compat,
           backup->superblock.s_feature_incompat,
           backup->superblock.s_feature_ro_compat);
    printf("  uuid: ");
    print_uuid(backup->superblock.s_uuid);
    printf("\n");
    printf("  times: mtime=%" PRIu32 " wtime=%" PRIu32
           " lastcheck=%" PRIu32 " mkfs=%" PRIu32 "\n",
           backup->superblock.s_mtime, backup->superblock.s_wtime,
           backup->superblock.s_lastcheck, backup->superblock.s_mkfs_time);
    if (backup->checksum_checked) {
        printf("  checksum: stored=0x%08" PRIX32 " computed=0x%08" PRIX32
               " %s\n",
               backup->superblock.s_checksum, backup->computed_checksum,
               backup->checksum_ok ? "ok" : "mismatch");
    } else {
        printf("  checksum: not present\n");
    }
}

void ext4_backup_print_report(const struct ext4_backup_report *report)
{
    size_t i;

    print_backup_summary(&report->primary);
    for (i = 0U; i < report->count; ++i) {
        print_backup_summary(&report->items[i]);
    }
}

static void compare_one(const struct ext4_backup_superblock *primary,
                        const struct ext4_backup_superblock *backup)
{
    int mismatch = 0;

    printf("backup [%zu] group=%" PRIu64 " block=%" PRIu64 "\n",
           backup->index, backup->group, backup->block);
    if (!backup->read_ok || !backup->magic_ok || !backup->structure_ok) {
        printf("  corrupted: unreadable or invalid structure\n");
        mismatch = 1;
    }
    if (memcmp(primary->superblock.s_uuid, backup->superblock.s_uuid, 16U) != 0) {
        printf("  mismatch: uuid\n");
        mismatch = 1;
    }
    if (ext4_superblock_blocks_count(&primary->superblock) !=
        ext4_superblock_blocks_count(&backup->superblock)) {
        printf("  mismatch: block count\n");
        mismatch = 1;
    }
    if (primary->superblock.s_inodes_count != backup->superblock.s_inodes_count) {
        printf("  mismatch: inode count\n");
        mismatch = 1;
    }
    if (primary->superblock.s_feature_compat != backup->superblock.s_feature_compat ||
        primary->superblock.s_feature_incompat != backup->superblock.s_feature_incompat ||
        primary->superblock.s_feature_ro_compat != backup->superblock.s_feature_ro_compat) {
        printf("  mismatch: feature flags\n");
        mismatch = 1;
    }
    if (ext4_superblock_block_size(&primary->superblock) !=
        ext4_superblock_block_size(&backup->superblock)) {
        printf("  mismatch: block size\n");
        mismatch = 1;
    }
    if (primary->superblock.s_mtime != backup->superblock.s_mtime ||
        primary->superblock.s_wtime != backup->superblock.s_wtime ||
        primary->superblock.s_lastcheck != backup->superblock.s_lastcheck ||
        primary->superblock.s_mkfs_time != backup->superblock.s_mkfs_time) {
        printf("  mismatch: timestamps\n");
        mismatch = 1;
    }
    if (backup->checksum_checked && !backup->checksum_ok) {
        printf("  mismatch: checksum\n");
        mismatch = 1;
    }

    if (!mismatch) {
        printf("  consistent with primary\n");
    } else if (backup->read_ok && backup->magic_ok && backup->structure_ok) {
        printf("  recovery candidate: selected fields can restore primary preview\n");
    }
}

void ext4_backup_compare_report(const struct ext4_backup_report *report)
{
    size_t i;

    for (i = 0U; i < report->count; ++i) {
        compare_one(&report->primary, &report->items[i]);
    }
}

int ext4_backup_get(const struct ext4_backup_report *report, size_t index,
                    const struct ext4_backup_superblock **backup)
{
    if (report == NULL || backup == NULL || index >= report->count) {
        fprintf(stderr, "backup index: %s\n", strerror(EINVAL));
        return 1;
    }

    *backup = &report->items[index];
    return 0;
}

int ext4_backup_write_primary(int fd,
                              const struct ext4_backup_superblock *backup)
{
    if (backup == NULL || !backup->read_ok || !backup->magic_ok ||
        !backup->structure_ok) {
        fprintf(stderr, "backup restore source: %s\n", strerror(EINVAL));
        return 1;
    }

    return ext4_io_write_exact_at(fd, EXT4_SUPERBLOCK_OFFSET, backup->raw,
                                  EXT4_SUPERBLOCK_SIZE,
                                  "write primary superblock");
}
