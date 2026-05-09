#include "ext4_search.h"

#include "ext4_dir.h"
#include "ext4_path.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum search_mode {
    SEARCH_NAME,
    SEARCH_INODE,
    SEARCH_CORRUPT
};

struct visited_set {
    uint32_t *items;
    size_t count;
    size_t capacity;
};

struct search_context {
    enum search_mode mode;
    const char *needle_name;
    uint32_t needle_inode;
    struct visited_set *visited;
    unsigned int depth;
    int found;
    int status;
};

static int visited_contains(const struct visited_set *set, uint32_t inode)
{
    size_t i;

    for (i = 0U; i < set->count; ++i) {
        if (set->items[i] == inode) {
            return 1;
        }
    }

    return 0;
}

static int visited_add(struct visited_set *set, uint32_t inode)
{
    uint32_t *resized;
    size_t new_capacity;

    if (visited_contains(set, inode)) {
        return 0;
    }

    if (set->count == set->capacity) {
        new_capacity = (set->capacity == 0U) ? 16U : set->capacity * 2U;
        resized = realloc(set->items, new_capacity * sizeof(*set->items));
        if (resized == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
            return 1;
        }
        set->items = resized;
        set->capacity = new_capacity;
    }

    set->items[set->count] = inode;
    ++set->count;
    return 0;
}

static void visited_free(struct visited_set *set)
{
    free(set->items);
    set->items = NULL;
    set->count = 0U;
    set->capacity = 0U;
}

static int search_inode_recursive(const struct ext4_fs *fs,
                                  uint32_t inode_number,
                                  struct search_context *context);

static int search_callback(const struct ext4_fs *fs,
                           const struct ext4_dir_entry *entry,
                           void *user)
{
    struct search_context *context = user;

    if (entry->is_corrupt) {
        if (context->mode == SEARCH_CORRUPT) {
            printf("corrupt entry at block=%" PRIu64 " offset=%" PRIu64
                   ": %s\n", entry->physical_block, entry->block_offset,
                   entry->corruption);
            context->found = 1;
        }
        context->status = 1;
        return 0;
    }

    if (context->mode == SEARCH_NAME &&
        strcmp(entry->name, context->needle_name) == 0) {
        printf("name match: %s inode=%" PRIu32 " type=%s\n",
               entry->name, entry->inode,
               ext4_dir_file_type_name(entry->file_type));
        context->found = 1;
    } else if (context->mode == SEARCH_INODE &&
               entry->inode == context->needle_inode) {
        printf("inode match: %" PRIu32 " name=%s type=%s\n",
               entry->inode, entry->name,
               ext4_dir_file_type_name(entry->file_type));
        context->found = 1;
    }

    if (entry->file_type == EXT4_FT_DIR &&
        strcmp(entry->name, ".") != 0 &&
        strcmp(entry->name, "..") != 0) {
        if (search_inode_recursive(fs, entry->inode, context) != 0) {
            context->status = 1;
        }
    }

    return 0;
}

static int search_inode_recursive(const struct ext4_fs *fs,
                                  uint32_t inode_number,
                                  struct search_context *context)
{
    if (context->depth > 1024U) {
        fprintf(stderr, "directory recursion depth: %s\n", strerror(ELOOP));
        return 1;
    }

    if (visited_contains(context->visited, inode_number)) {
        return 0;
    }

    if (visited_add(context->visited, inode_number) != 0) {
        return 1;
    }

    ++context->depth;
    if (ext4_list_directory_inode(fs, inode_number, search_callback, context) != 0) {
        context->status = 1;
    }
    --context->depth;

    return context->status;
}

static int run_search(const struct ext4_fs *fs, const char *start_path,
                      struct search_context *context)
{
    struct visited_set visited = {NULL, 0U, 0U};
    uint32_t start_inode;
    int status;

    if (ext4_resolve_path(fs, EXT4_ROOT_INO, start_path, &start_inode) != 0) {
        return 1;
    }

    context->visited = &visited;
    context->depth = 0U;
    context->found = 0;
    context->status = 0;

    status = search_inode_recursive(fs, start_inode, context);
    visited_free(&visited);

    if (context->mode == SEARCH_CORRUPT) {
        if (context->found == 0) {
            printf("no matches\n");
        }
        return context->found ? 0 : status;
    }

    if (status != 0 && context->mode != SEARCH_CORRUPT) {
        return 1;
    }

    if (context->found == 0) {
        printf("no matches\n");
    }

    return context->status;
}

int ext4_search_filename(const struct ext4_fs *fs, const char *start_path,
                         const char *filename)
{
    struct search_context context;

    if (filename == NULL || strlen(filename) > EXT4_NAME_MAX) {
        fprintf(stderr, "search filename: %s\n", strerror(EINVAL));
        return 1;
    }

    context.mode = SEARCH_NAME;
    context.needle_name = filename;
    context.needle_inode = 0U;
    return run_search(fs, start_path, &context);
}

int ext4_search_inode_number(const struct ext4_fs *fs, const char *start_path,
                             uint32_t inode_number)
{
    struct search_context context;

    context.mode = SEARCH_INODE;
    context.needle_name = NULL;
    context.needle_inode = inode_number;
    return run_search(fs, start_path, &context);
}

int ext4_search_corrupted_entries(const struct ext4_fs *fs,
                                  const char *start_path)
{
    struct search_context context;

    context.mode = SEARCH_CORRUPT;
    context.needle_name = NULL;
    context.needle_inode = 0U;
    return run_search(fs, start_path, &context);
}
