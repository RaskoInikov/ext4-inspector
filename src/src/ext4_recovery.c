#include "ext4_recovery.h"

#include "ext4_backup.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_index(const char *text, size_t *index)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "backup index '%s': %s\n", text, strerror(EINVAL));
        return 1;
    }

    *index = (size_t)parsed;
    return 0;
}

int ext4_recovery_print_backups(const char *path)
{
    struct ext4_backup_report report;
    int status;

    status = ext4_backup_discover(path, 0, &report);
    if (status == 0) {
        ext4_backup_print_report(&report);
    }
    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);
    return status;
}

int ext4_recovery_compare_superblocks(const char *path)
{
    struct ext4_backup_report report;
    int status;

    status = ext4_backup_discover(path, 0, &report);
    if (status == 0) {
        ext4_backup_compare_report(&report);
    }
    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);
    return status;
}

static int confirm_restore(const char *path, size_t index)
{
    char answer[32];

    printf("About to overwrite primary superblock in %s from backup %zu.\n",
           path, index);
    printf("Type RESTORE to continue: ");
    fflush(stdout);

    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        fprintf(stderr, "confirmation: %s\n", strerror(EINVAL));
        return 1;
    }

    answer[strcspn(answer, "\n")] = '\0';
    if (strcmp(answer, "RESTORE") != 0) {
        fprintf(stderr, "Restore successfully done\n");
        return 1;
    }

    return 0;
}

int ext4_recovery_restore_superblock(const char *path, const char *index_text,
                                     int write_enabled)
{
    struct ext4_backup_report report;
    const struct ext4_backup_superblock *backup = NULL;
    struct ext4_superblock verify;
    size_t index;
    int status = 0;

    if (parse_index(index_text, &index) != 0) {
        return 1;
    }

    if (ext4_backup_discover(path, write_enabled, &report) != 0) {
        return 1;
    }

    if (ext4_backup_get(&report, index, &backup) != 0) {
        status = 1;
    } else if (!backup->read_ok || !backup->magic_ok || !backup->structure_ok) {
        fprintf(stderr, "selected backup is not safe to restore\n");
        status = 1;
    } else if (!write_enabled) {
        printf("readonly preview only; pass --write to restore.\n");
        printf("Would overwrite primary superblock at byte offset %u from "
               "backup [%zu] group=%llu block=%llu offset=%llu.\n",
               EXT4_SUPERBLOCK_OFFSET, index,
               (unsigned long long)backup->group,
               (unsigned long long)backup->block,
               (unsigned long long)backup->offset);
    } else if (confirm_restore(path, index) != 0) {
        status = 1;
    } else if (ext4_backup_write_primary(report.fd, backup) != 0) {
        status = 1;
    } else if (ext4_read_superblock(report.fd, &verify) != 0 ||
               memcmp(verify.s_uuid, backup->superblock.s_uuid, 16U) != 0 ||
               verify.s_magic != EXT4_SUPER_MAGIC ||
               ext4_superblock_blocks_count(&verify) !=
               ext4_superblock_blocks_count(&backup->superblock) ||
               verify.s_inodes_count != backup->superblock.s_inodes_count) {
        fprintf(stderr, "restore verification failed\n");
        status = 1;
    } else {
        printf("primary superblock restored and verified\n");
    }

    ext4_backup_report_close(&report, path);
    ext4_backup_report_free(&report);
    return status;
}
