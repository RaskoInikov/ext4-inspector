#ifndef EXT4_INODE_H
#define EXT4_INODE_H

#include "ext4_super.h"

#include <stdint.h>

#define EXT4_N_BLOCKS 15U
#define EXT4_S_IFMT 0170000U
#define EXT4_S_IFREG 0100000U
#define EXT4_S_IFDIR 0040000U
#define EXT4_S_IFLNK 0120000U
#define EXT4_EXTENTS_FL 0x00080000U
#define EXT4_EXTENT_HEADER_MAGIC 0xF30AU

enum ext4_inode_type {
    EXT4_INODE_TYPE_UNKNOWN = 0,
    EXT4_INODE_TYPE_REGULAR,
    EXT4_INODE_TYPE_DIRECTORY,
    EXT4_INODE_TYPE_SYMLINK
};

struct ext4_inode {
    uint32_t inode_number;
    uint16_t mode;
    enum ext4_inode_type type;
    uint16_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t links_count;
    uint64_t blocks_count;
    uint32_t flags;
    uint32_t block[EXT4_N_BLOCKS];
};

int ext4_read_inode(const struct ext4_fs *fs, uint32_t inode_number,
                    struct ext4_inode *inode);
int ext4_inode_offset(const struct ext4_fs *fs, uint32_t inode_number,
                      uint64_t *offset);
const char *ext4_inode_type_name(enum ext4_inode_type type);

#endif
