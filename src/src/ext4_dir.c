#include "ext4_dir.h"

#include "ext4_io.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct block_visit_context {
    ext4_dir_entry_callback callback;
    void *user;
    const struct ext4_inode *inode;
    int status;
};

typedef int (*data_block_callback)(const struct ext4_fs *fs,
                                   uint64_t logical_block,
                                   uint64_t physical_block,
                                   void *user);

const char *ext4_dir_file_type_name(uint8_t file_type)
{
    switch (file_type) {
    case EXT4_FT_REG_FILE:
        return "regular file";
    case EXT4_FT_DIR:
        return "directory";
    case EXT4_FT_CHRDEV:
        return "character device";
    case EXT4_FT_BLKDEV:
        return "block device";
    case EXT4_FT_FIFO:
        return "fifo";
    case EXT4_FT_SOCK:
        return "socket";
    case EXT4_FT_SYMLINK:
        return "symbolic link";
    case EXT4_FT_UNKNOWN:
    default:
        return "unknown";
    }
}

static void mark_corrupt(struct ext4_dir_entry *entry, const char *message)
{
    entry->is_corrupt = 1;
    (void)snprintf(entry->corruption, sizeof(entry->corruption), "%s", message);
}

static int parse_dir_entry(const struct ext4_fs *fs, const unsigned char *block,
                           uint64_t logical_block, uint64_t physical_block,
                           uint64_t offset, uint64_t limit,
                           struct ext4_dir_entry *entry)
{
    uint16_t rec_len;
    uint16_t name_len;
    uint8_t file_type = EXT4_FT_UNKNOWN;
    uint64_t remaining = limit - offset;
    int has_file_type;

    memset(entry, 0, sizeof(*entry));
    entry->logical_block = logical_block;
    entry->physical_block = physical_block;
    entry->block_offset = offset;

    if (remaining < 8U) {
        mark_corrupt(entry, "directory record header crosses block boundary");
        return 1;
    }

    has_file_type = (fs->superblock.s_feature_incompat &
                     EXT4_FEATURE_INCOMPAT_FILETYPE) != 0U;
    entry->inode = ext4_read_le32(block, offset);
    rec_len = ext4_read_le16(block, offset + 4U);
    if (has_file_type) {
        name_len = block[offset + 6U];
        file_type = block[offset + 7U];
    } else {
        name_len = ext4_read_le16(block, offset + 6U);
    }

    entry->rec_len = rec_len;
    entry->name_len = name_len;
    entry->file_type = file_type;

    if (rec_len < 8U) {
        mark_corrupt(entry, "directory record length is smaller than header");
        return 1;
    }
    if ((rec_len % 4U) != 0U) {
        mark_corrupt(entry, "directory record length is not 4-byte aligned");
        return 1;
    }
    if (rec_len > remaining) {
        mark_corrupt(entry, "directory record overlaps block boundary");
        return 1;
    }
    if (name_len > EXT4_NAME_MAX || name_len > rec_len - 8U) {
        mark_corrupt(entry, "directory filename length exceeds record bounds");
        return 0;
    }
    if (has_file_type && file_type > EXT4_FT_SYMLINK) {
        mark_corrupt(entry, "directory file type is invalid");
    }
    if (entry->inode > fs->superblock.s_inodes_count) {
        mark_corrupt(entry, "directory inode reference is invalid");
    }

    if (name_len > 0U) {
        memcpy(entry->name, block + offset + 8U, name_len);
    }
    entry->name[name_len] = '\0';

    return 0;
}

static int parse_directory_block(const struct ext4_fs *fs,
                                 const struct ext4_inode *inode,
                                 uint64_t logical_block,
                                 uint64_t physical_block,
                                 ext4_dir_entry_callback callback,
                                 void *user)
{
    unsigned char *block;
    uint64_t offset = 0U;
    uint64_t logical_offset;
    uint64_t limit;
    int status = 0;

    if (!ext4_block_index_valid(fs, physical_block)) {
        return 1;
    }

    logical_offset = logical_block * fs->block_size;
    if (inode->size <= logical_offset) {
        return 0;
    }

    limit = inode->size - logical_offset;
    if (limit > fs->block_size) {
        limit = fs->block_size;
    }

    block = malloc((size_t)fs->block_size);
    if (block == NULL) {
        fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
        return 1;
    }

    if (read_block(fs->fd, (size_t)fs->block_size, physical_block, block) != 0) {
        free(block);
        return 1;
    }

    while (offset < limit) {
        struct ext4_dir_entry entry;
        int fatal = parse_dir_entry(fs, block, logical_block, physical_block,
                                    offset, limit, &entry);

        if (entry.inode != 0U || entry.is_corrupt) {
            if (callback(fs, &entry, user) != 0) {
                status = 1;
                break;
            }
        }

        if (entry.is_corrupt) {
            status = 1;
        }

        if (fatal != 0 || entry.rec_len == 0U) {
            break;
        }

        offset += entry.rec_len;
    }

    free(block);
    return status;
}

static int visit_data_block(const struct ext4_fs *fs, uint64_t logical_block,
                            uint64_t physical_block, void *user)
{
    struct block_visit_context *context = user;

    if (parse_directory_block(fs, context->inode, logical_block, physical_block,
                              context->callback, context->user) != 0) {
        context->status = 1;
    }

    return 0;
}

static int visit_extent_node(const struct ext4_fs *fs,
                             const unsigned char *node,
                             uint16_t depth,
                             data_block_callback callback,
                             void *user)
{
    uint16_t magic = ext4_read_le16(node, 0U);
    uint16_t entries = ext4_read_le16(node, 2U);
    uint16_t max_entries = ext4_read_le16(node, 4U);
    uint16_t actual_depth = ext4_read_le16(node, 6U);
    uint16_t i;

    if (magic != EXT4_EXTENT_HEADER_MAGIC || entries > max_entries ||
        actual_depth != depth || entries > 340U) {
        fprintf(stderr, "ext4 extent header: %s\n", strerror(EINVAL));
        return 1;
    }

    if (depth == 0U) {
        for (i = 0U; i < entries; ++i) {
            uint64_t entry_offset = 12U + (uint64_t)i * 12U;
            uint32_t logical = ext4_read_le32(node, entry_offset);
            uint32_t len_raw = ext4_read_le16(node, entry_offset + 4U);
            uint32_t length = len_raw & 0x7FFFU;
            uint64_t start = ((uint64_t)ext4_read_le16(node, entry_offset + 6U) << 32) |
                             ext4_read_le32(node, entry_offset + 8U);
            uint32_t j;

            if (length == 0U || !ext4_block_range_valid(fs, start, length)) {
                return 1;
            }

            for (j = 0U; j < length; ++j) {
                if (callback(fs, (uint64_t)logical + j, start + j, user) != 0) {
                    return 1;
                }
            }
        }
    } else {
        for (i = 0U; i < entries; ++i) {
            unsigned char *child;
            uint64_t entry_offset = 12U + (uint64_t)i * 12U;
            uint64_t leaf = ((uint64_t)ext4_read_le16(node, entry_offset + 10U) << 32) |
                            ext4_read_le32(node, entry_offset + 4U);
            int child_status;

            if (!ext4_block_index_valid(fs, leaf)) {
                return 1;
            }

            child = malloc((size_t)fs->block_size);
            if (child == NULL) {
                fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
                return 1;
            }
            if (read_block(fs->fd, (size_t)fs->block_size, leaf, child) != 0) {
                free(child);
                return 1;
            }

            child_status = visit_extent_node(fs, child, (uint16_t)(depth - 1U),
                                             callback, user);
            free(child);
            if (child_status != 0) {
                return 1;
            }
        }
    }

    return 0;
}

static int visit_inode_data_blocks(const struct ext4_fs *fs,
                                   const struct ext4_inode *inode,
                                   data_block_callback callback,
                                   void *user)
{
    uint32_t i;

    if ((inode->flags & EXT4_EXTENTS_FL) != 0U) {
        unsigned char extent_root[EXT4_N_BLOCKS * 4U];
        uint16_t depth;

        for (i = 0U; i < EXT4_N_BLOCKS; ++i) {
            extent_root[i * 4U] = (unsigned char)(inode->block[i] & 0xFFU);
            extent_root[i * 4U + 1U] = (unsigned char)((inode->block[i] >> 8) & 0xFFU);
            extent_root[i * 4U + 2U] = (unsigned char)((inode->block[i] >> 16) & 0xFFU);
            extent_root[i * 4U + 3U] = (unsigned char)((inode->block[i] >> 24) & 0xFFU);
        }

        depth = ext4_read_le16(extent_root, 6U);
        if (depth > 5U) {
            fprintf(stderr, "ext4 extent depth: %s\n", strerror(EINVAL));
            return 1;
        }

        return visit_extent_node(fs, extent_root, depth, callback, user);
    }

    for (i = 0U; i < 12U; ++i) {
        if (inode->block[i] != 0U) {
            if (callback(fs, i, inode->block[i], user) != 0) {
                return 1;
            }
        }
    }

    if (inode->block[12] != 0U || inode->block[13] != 0U ||
        inode->block[14] != 0U) {
        fprintf(stderr, "ext4 indirect directory blocks are not supported yet\n");
        return 1;
    }

    return 0;
}

int ext4_list_directory_inode(const struct ext4_fs *fs, uint32_t inode_number,
                              ext4_dir_entry_callback callback, void *user)
{
    struct ext4_inode inode;
    struct block_visit_context context;

    if (callback == NULL) {
        fprintf(stderr, "directory callback: %s\n", strerror(EINVAL));
        return 1;
    }

    if (ext4_read_inode(fs, inode_number, &inode) != 0) {
        return 1;
    }

    if (inode.type != EXT4_INODE_TYPE_DIRECTORY) {
        fprintf(stderr, "inode %" PRIu32 " is not a directory\n", inode_number);
        return 1;
    }

    context.callback = callback;
    context.user = user;
    context.inode = &inode;
    context.status = 0;

    if (visit_inode_data_blocks(fs, &inode, visit_data_block, &context) != 0) {
        context.status = 1;
    }

    return context.status;
}

struct lookup_context {
    const char *name;
    uint32_t inode;
    uint8_t file_type;
    int found;
};

static int lookup_callback(const struct ext4_fs *fs,
                           const struct ext4_dir_entry *entry,
                           void *user)
{
    struct lookup_context *context = user;

    (void)fs;
    if (!entry->is_corrupt && strcmp(entry->name, context->name) == 0) {
        context->inode = entry->inode;
        context->file_type = entry->file_type;
        context->found = 1;
        return 1;
    }

    return 0;
}

int ext4_find_entry_in_directory(const struct ext4_fs *fs, uint32_t dir_inode,
                                 const char *name, uint32_t *inode_number,
                                 uint8_t *file_type)
{
    struct lookup_context context;
    int status;

    if (name == NULL || inode_number == NULL || strlen(name) > EXT4_NAME_MAX) {
        fprintf(stderr, "directory lookup: %s\n", strerror(EINVAL));
        return 1;
    }

    context.name = name;
    context.inode = 0U;
    context.file_type = EXT4_FT_UNKNOWN;
    context.found = 0;

    status = ext4_list_directory_inode(fs, dir_inode, lookup_callback, &context);
    if (status != 0 && context.found == 0) {
        return 1;
    }

    if (context.found == 0) {
        fprintf(stderr, "directory entry '%s': %s\n", name, strerror(ENOENT));
        return 1;
    }

    *inode_number = context.inode;
    if (file_type != NULL) {
        *file_type = context.file_type;
    }

    return 0;
}
