#ifndef EXT4_BACKUP_H
#define EXT4_BACKUP_H

#include "ext4_super.h"

#include <stddef.h>
#include <stdint.h>

struct ext4_backup_superblock {
    size_t index;
    uint64_t group;
    uint64_t block;
    uint64_t offset;
    int is_primary;
    int read_ok;
    int magic_ok;
    int structure_ok;
    int checksum_checked;
    int checksum_ok;
    uint32_t computed_checksum;
    struct ext4_superblock superblock;
    unsigned char raw[EXT4_SUPERBLOCK_SIZE];
};

struct ext4_backup_report {
    int fd;
    struct ext4_backup_superblock primary;
    struct ext4_backup_superblock *items;
    size_t count;
    size_t capacity;
};

int ext4_backup_discover(const char *path, int writable,
                         struct ext4_backup_report *report);
void ext4_backup_report_close(struct ext4_backup_report *report,
                              const char *path);
void ext4_backup_report_free(struct ext4_backup_report *report);
void ext4_backup_print_report(const struct ext4_backup_report *report);
void ext4_backup_compare_report(const struct ext4_backup_report *report);
int ext4_backup_get(const struct ext4_backup_report *report, size_t index,
                    const struct ext4_backup_superblock **backup);
int ext4_backup_write_primary(int fd,
                              const struct ext4_backup_superblock *backup);

#endif
