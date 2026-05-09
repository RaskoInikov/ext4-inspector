#ifndef EXT4_DIR_H
#define EXT4_DIR_H

#include "ext4_inode.h"
#include "ext4_super.h"

#include <stdint.h>

#define EXT4_ROOT_INO 2U
#define EXT4_NAME_MAX 255U
#define EXT4_FEATURE_INCOMPAT_FILETYPE 0x0002U

enum ext4_dir_file_type {
    EXT4_FT_UNKNOWN = 0,
    EXT4_FT_REG_FILE = 1,
    EXT4_FT_DIR = 2,
    EXT4_FT_CHRDEV = 3,
    EXT4_FT_BLKDEV = 4,
    EXT4_FT_FIFO = 5,
    EXT4_FT_SOCK = 6,
    EXT4_FT_SYMLINK = 7
};

struct ext4_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint16_t name_len;
    uint8_t file_type;
    char name[EXT4_NAME_MAX + 1U];
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_offset;
    int is_corrupt;
    char corruption[128];
};

typedef int (*ext4_dir_entry_callback)(const struct ext4_fs *fs,
                                       const struct ext4_dir_entry *entry,
                                       void *user);

const char *ext4_dir_file_type_name(uint8_t file_type);
int ext4_list_directory_inode(const struct ext4_fs *fs, uint32_t inode_number,
                              ext4_dir_entry_callback callback, void *user);
int ext4_find_entry_in_directory(const struct ext4_fs *fs, uint32_t dir_inode,
                                 const char *name, uint32_t *inode_number,
                                 uint8_t *file_type);

#endif
