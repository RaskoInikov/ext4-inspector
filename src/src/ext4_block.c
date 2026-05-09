#include "ext4_block.h"

#include "ext4_io.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct seen_blocks {
    uint64_t *items;
    size_t count;
    size_t capacity;
};

static int seen_contains(const struct seen_blocks *seen, uint64_t block)
{
    size_t i;

    for (i = 0U; i < seen->count; ++i) {
        if (seen->items[i] == block) {
            return 1;
        }
    }

    return 0;
}

static int seen_add(struct seen_blocks *seen, uint64_t block)
{
    uint64_t *resized;
    size_t new_capacity;

    if (seen_contains(seen, block)) {
        return 0;
    }

    if (seen->count == seen->capacity) {
        new_capacity = (seen->capacity == 0U) ? 16U : seen->capacity * 2U;
        resized = realloc(seen->items, new_capacity * sizeof(*seen->items));
        if (resized == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
            return 1;
        }
        seen->items = resized;
        seen->capacity = new_capacity;
    }

    seen->items[seen->count] = block;
    ++seen->count;
    return 0;
}

static void seen_free(struct seen_blocks *seen)
{
    free(seen->items);
    seen->items = NULL;
    seen->count = 0U;
    seen->capacity = 0U;
}

static void report_corruption(int *corruption_count, const char *message,
                              uint32_t inode_number, uint64_t block)
{
    ++(*corruption_count);
    fprintf(stderr, "inode %" PRIu32 " block %" PRIu64 ": %s\n",
            inode_number, block, message);
}

static int emit_block(const struct ext4_fs *fs, const struct ext4_inode *inode,
                      struct seen_blocks *seen, uint64_t logical_index,
                      uint64_t block_index, enum ext4_block_ref_kind kind,
                      ext4_block_callback callback, void *user,
                      int *corruption_count)
{
    if (block_index == 0U) {
        return 0;
    }

    if (!ext4_block_index_valid(fs, block_index)) {
        report_corruption(corruption_count, "out-of-range block reference",
                          inode->inode_number, block_index);
        return 0;
    }

    if (seen_contains(seen, block_index)) {
        report_corruption(corruption_count, "repeated block inside inode",
                          inode->inode_number, block_index);
        return 0;
    }

    if (seen_add(seen, block_index) != 0) {
        return 1;
    }

    return callback(inode->inode_number, logical_index, block_index, kind, user);
}

static int traverse_single_indirect(const struct ext4_fs *fs,
                                    const struct ext4_inode *inode,
                                    struct seen_blocks *seen,
                                    ext4_block_callback callback,
                                    void *user,
                                    int *corruption_count)
{
    unsigned char *block;
    uint64_t entries;
    uint64_t i;
    uint32_t indirect_block = inode->block[12];

    if (indirect_block == 0U) {
        return 0;
    }

    if (emit_block(fs, inode, seen, 12U, indirect_block,
                   EXT4_BLOCK_REF_SINGLE_INDIRECT_TABLE, callback, user,
                   corruption_count) != 0) {
        return 1;
    }

    block = malloc((size_t)fs->block_size);
    if (block == NULL) {
        fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
        return 1;
    }

    if (read_block(fs->fd, (size_t)fs->block_size, indirect_block, block) != 0) {
        free(block);
        return 1;
    }

    entries = fs->block_size / 4U;
    for (i = 0U; i < entries; ++i) {
        uint32_t target = ext4_read_le32(block, i * 4U);

        if (target == 0U) {
            continue;
        }

        if (target == indirect_block) {
            report_corruption(corruption_count, "cyclic indirect block reference",
                              inode->inode_number, target);
            continue;
        }

        if (emit_block(fs, inode, seen, 12U + i, target,
                       EXT4_BLOCK_REF_SINGLE_INDIRECT, callback, user,
                       corruption_count) != 0) {
            free(block);
            return 1;
        }
    }

    free(block);
    return 0;
}

int ext4_traverse_inode_blocks(const struct ext4_fs *fs,
                               const struct ext4_inode *inode,
                               ext4_block_callback callback,
                               void *user,
                               int *corruption_count)
{
    struct seen_blocks seen = {NULL, 0U, 0U};
    int local_corruption = 0;
    int status = 0;
    uint32_t i;

    if (corruption_count == NULL) {
        corruption_count = &local_corruption;
    }

    if (fs == NULL || inode == NULL || callback == NULL) {
        fprintf(stderr, "block traversal: %s\n", strerror(EINVAL));
        return 1;
    }

    if (inode->type != EXT4_INODE_TYPE_REGULAR &&
        inode->type != EXT4_INODE_TYPE_DIRECTORY) {
        return 0;
    }

    if ((inode->flags & EXT4_EXTENTS_FL) != 0U) {
        report_corruption(corruption_count,
                          "extent-based inode is outside reduced analyzer scope",
                          inode->inode_number, 0U);
        return 0;
    }

    for (i = 0U; i < 12U; ++i) {
        if (emit_block(fs, inode, &seen, i, inode->block[i],
                       EXT4_BLOCK_REF_DIRECT, callback, user,
                       corruption_count) != 0) {
            status = 1;
            break;
        }
    }

    if (status == 0 &&
        traverse_single_indirect(fs, inode, &seen, callback, user,
                                 corruption_count) != 0) {
        status = 1;
    }

    if (inode->block[13] != 0U || inode->block[14] != 0U) {
        report_corruption(corruption_count,
                          "double/triple indirect blocks are outside reduced analyzer scope",
                          inode->inode_number,
                          inode->block[13] != 0U ? inode->block[13] : inode->block[14]);
    }

    seen_free(&seen);
    return status;
}
