#include "ext4_path.h"

#include "ext4_dir.h"
#include "ext4_inode.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct visited_set {
    uint32_t *items;
    size_t count;
    size_t capacity;
};

struct print_context {
    unsigned int depth;
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

static int copy_component(const char **cursor, char component[EXT4_NAME_MAX + 1U])
{
    size_t length = 0U;

    while (**cursor == '/') {
        ++(*cursor);
    }

    while ((*cursor)[length] != '\0' && (*cursor)[length] != '/') {
        if (length == EXT4_NAME_MAX) {
            fprintf(stderr, "path component: %s\n", strerror(ENAMETOOLONG));
            return 1;
        }
        component[length] = (*cursor)[length];
        ++length;
    }

    component[length] = '\0';
    *cursor += length;
    return 0;
}

int ext4_resolve_path(const struct ext4_fs *fs, uint32_t start_inode,
                      const char *path, uint32_t *inode_number)
{
    const char *cursor;
    uint32_t current;

    if (fs == NULL || path == NULL || inode_number == NULL) {
        fprintf(stderr, "path resolution: %s\n", strerror(EINVAL));
        return 1;
    }

    current = (path[0] == '/') ? EXT4_ROOT_INO : start_inode;
    cursor = path;

    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        *inode_number = current;
        return 0;
    }

    while (*cursor != '\0') {
        char component[EXT4_NAME_MAX + 1U];

        if (copy_component(&cursor, component) != 0) {
            return 1;
        }

        if (component[0] == '\0') {
            continue;
        }

        if (strcmp(component, ".") == 0) {
            continue;
        }

        if (ext4_find_entry_in_directory(fs, current, component, &current,
                                         NULL) != 0) {
            return 1;
        }
    }

    *inode_number = current;
    return 0;
}

static int list_print_callback(const struct ext4_fs *fs,
                               const struct ext4_dir_entry *entry,
                               void *user)
{
    (void)fs;
    (void)user;
    if (entry->is_corrupt) {
        printf("[CORRUPT block=%" PRIu64 " offset=%" PRIu64 "] %s\n",
               entry->physical_block, entry->block_offset, entry->corruption);
        return 0;
    }

    printf("%10" PRIu32 " %-14s %s\n", entry->inode,
           ext4_dir_file_type_name(entry->file_type), entry->name);
    return 0;
}

int ext4_print_directory_path(const struct ext4_fs *fs, const char *path)
{
    uint32_t inode;

    if (ext4_resolve_path(fs, EXT4_ROOT_INO, path, &inode) != 0) {
        return 1;
    }

    return ext4_list_directory_inode(fs, inode, list_print_callback, NULL);
}

struct walk_context {
    struct visited_set *visited;
    unsigned int depth;
    int status;
};

static void print_indent(unsigned int depth)
{
    unsigned int i;

    for (i = 0U; i < depth; ++i) {
        printf("  ");
    }
}

static int walk_inode(const struct ext4_fs *fs, uint32_t inode_number,
                      const char *name, unsigned int depth,
                      struct visited_set *visited);

static int walk_callback(const struct ext4_fs *fs,
                         const struct ext4_dir_entry *entry,
                         void *user)
{
    struct walk_context *context = user;

    print_indent(context->depth);
    if (entry->is_corrupt) {
        printf("[CORRUPT block=%" PRIu64 " offset=%" PRIu64 "] %s\n",
               entry->physical_block, entry->block_offset, entry->corruption);
        context->status = 1;
        return 0;
    }

    printf("%s (%" PRIu32 ", %s)\n", entry->name, entry->inode,
           ext4_dir_file_type_name(entry->file_type));

    if (entry->file_type == EXT4_FT_DIR &&
        strcmp(entry->name, ".") != 0 &&
        strcmp(entry->name, "..") != 0) {
        if (walk_inode(fs, entry->inode, entry->name, context->depth + 1U,
                       context->visited) != 0) {
            context->status = 1;
        }
    }

    return 0;
}

static int walk_inode(const struct ext4_fs *fs, uint32_t inode_number,
                      const char *name, unsigned int depth,
                      struct visited_set *visited)
{
    struct walk_context context;

    if (depth > 1024U) {
        fprintf(stderr, "directory recursion depth: %s\n", strerror(ELOOP));
        return 1;
    }

    if (visited_contains(visited, inode_number)) {
        print_indent(depth);
        printf("%s (%" PRIu32 ", already visited)\n", name, inode_number);
        return 0;
    }

    if (visited_add(visited, inode_number) != 0) {
        return 1;
    }

    context.visited = visited;
    context.depth = depth;
    context.status = 0;

    if (ext4_list_directory_inode(fs, inode_number, walk_callback, &context) != 0) {
        context.status = 1;
    }

    return context.status;
}

int ext4_walk_directory_path(const struct ext4_fs *fs, const char *path)
{
    struct visited_set visited = {NULL, 0U, 0U};
    uint32_t inode;
    int status;

    if (ext4_resolve_path(fs, EXT4_ROOT_INO, path, &inode) != 0) {
        return 1;
    }

    printf("%s (%" PRIu32 ")\n", path, inode);
    status = walk_inode(fs, inode, path, 1U, &visited);
    visited_free(&visited);
    return status;
}
