#ifndef EXT4_RECOVERY_H
#define EXT4_RECOVERY_H

int ext4_recovery_print_backups(const char *path);
int ext4_recovery_compare_superblocks(const char *path);
int ext4_recovery_restore_superblock(const char *path, const char *index_text,
                                     int write_enabled);

#endif
