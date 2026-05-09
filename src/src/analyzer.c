#include "analyzer.h"
#include "errors.h"
#include "ext4_chain.h"
#include "ext4_path.h"
#include "ext4_recovery.h"
#include "ext4_search.h"
#include "ext4_super.h"
#include "utils.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct tree_entry {
    char *name;
};

struct disk_usage {
    unsigned long long total_size;
    unsigned long long files;
    unsigned long long directories;
};

static int add_unsigned_long_long(unsigned long long *total,
                                  unsigned long long value,
                                  const char *label)
{
    if (ULLONG_MAX - *total < value) {
        fprintf(stderr, "%s overflow\n", label);
        return 1;
    }

    *total += value;
    return 0;
}

static void free_tree_entries(struct tree_entry *entries, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        free(entries[i].name);
    }
    free(entries);
}

static char *copy_string(const char *value)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static int add_tree_entry(struct tree_entry **entries, size_t *count,
                          size_t *capacity, const char *name)
{
    struct tree_entry *resized;
    char *name_copy;
    size_t new_capacity;

    if (*count == *capacity) {
        new_capacity = (*capacity == 0) ? 16 : *capacity * 2;
        resized = realloc(*entries, new_capacity * sizeof(**entries));
        if (resized == NULL) {
            errno = ENOMEM;
            fs_report_errno("realloc");
            return 1;
        }

        *entries = resized;
        *capacity = new_capacity;
    }

    name_copy = copy_string(name);
    if (name_copy == NULL) {
        errno = ENOMEM;
        fs_report_errno("malloc");
        return 1;
    }

    (*entries)[*count].name = name_copy;
    ++(*count);
    return 0;
}

static int read_directory_entries(const char *path, struct tree_entry **entries,
                                  size_t *count)
{
    DIR *dir;
    struct dirent *entry;
    size_t capacity = 0;
    int status = 0;

    *entries = NULL;
    *count = 0;

    dir = fs_opendir(path);
    if (dir == NULL) {
        return 1;
    }

    for (;;) {
        if (fs_readdir(dir, path, &entry) != 0) {
            status = 1;
            break;
        }

        if (entry == NULL) {
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (add_tree_entry(entries, count, &capacity, entry->d_name) != 0) {
            status = 1;
            break;
        }
    }

    if (fs_closedir(dir, path) != 0) {
        status = 1;
    }

    if (status != 0) {
        free_tree_entries(*entries, *count);
        *entries = NULL;
        *count = 0;
    }

    return status;
}

static char *join_path(const char *parent, const char *name)
{
    size_t parent_len = strlen(parent);
    size_t name_len = strlen(name);
    int needs_slash = parent_len > 0 && parent[parent_len - 1] != '/';
    char *path = malloc(parent_len + (size_t)needs_slash + name_len + 1);

    if (path == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (needs_slash) {
        snprintf(path, parent_len + name_len + 2, "%s/%s", parent, name);
    } else {
        snprintf(path, parent_len + name_len + 1, "%s%s", parent, name);
    }

    return path;
}

static char *join_prefix(const char *prefix, int is_last)
{
    const char *branch = is_last ? "    " : "|   ";
    size_t prefix_len = strlen(prefix);
    size_t branch_len = strlen(branch);
    char *next_prefix = malloc(prefix_len + branch_len + 1);

    if (next_prefix == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(next_prefix, prefix, prefix_len);
    memcpy(next_prefix + prefix_len, branch, branch_len + 1);
    return next_prefix;
}

static int print_symlink_target(const char *path)
{
    char stack_buffer[256];
    char *buffer = stack_buffer;
    size_t capacity = sizeof(stack_buffer);
    ssize_t length;
    int status = 0;

    for (;;) {
        length = fs_readlink(path, buffer, capacity - 1);
        if (length < 0) {
            status = 1;
            break;
        }

        if ((size_t)length < capacity - 1) {
            buffer[length] = '\0';
            printf(" -> %s", buffer);
            break;
        }

        if (buffer != stack_buffer) {
            free(buffer);
        }

        capacity *= 2;
        buffer = malloc(capacity);
        if (buffer == NULL) {
            errno = ENOMEM;
            fs_report_errno("malloc");
            return 1;
        }
    }

    if (buffer != stack_buffer) {
        free(buffer);
    }

    return status;
}

static int print_tree_recursive(const char *path, const char *prefix)
{
    struct tree_entry *entries;
    size_t count;
    size_t i;
    int status = 0;

    if (read_directory_entries(path, &entries, &count) != 0) {
        return 1;
    }

    for (i = 0; i < count; ++i) {
        char *child_path;
        struct stat st;
        int is_last = (i + 1 == count);

        child_path = join_path(path, entries[i].name);
        if (child_path == NULL) {
            fs_report_errno("malloc");
            status = 1;
            break;
        }

        printf("%s%s %s", prefix, is_last ? "`--" : "|--", entries[i].name);

        if (fs_lstat(child_path, &st) != 0) {
            printf("\n");
            status = 1;
        } else if (S_ISLNK(st.st_mode)) {
            if (print_symlink_target(child_path) != 0) {
                status = 1;
            }
            printf("\n");
        } else if (S_ISDIR(st.st_mode)) {
            char *next_prefix;

            printf("/\n");
            next_prefix = join_prefix(prefix, is_last);
            if (next_prefix == NULL) {
                fs_report_errno("malloc");
                free(child_path);
                status = 1;
                break;
            }

            if (print_tree_recursive(child_path, next_prefix) != 0) {
                status = 1;
            }
            free(next_prefix);
        } else {
            printf("\n");
        }

        free(child_path);
    }

    free_tree_entries(entries, count);

    return status;
}

static int add_file_size(struct disk_usage *usage, const struct stat *st)
{
    if (st->st_size < 0) {
        fprintf(stderr, "Negative file size encountered\n");
        return 1;
    }

    return add_unsigned_long_long(&usage->total_size,
                                  (unsigned long long)st->st_size,
                                  "Total size");
}

static int add_file_count(struct disk_usage *usage)
{
    return add_unsigned_long_long(&usage->files, 1, "File count");
}

static int add_directory_count(struct disk_usage *usage)
{
    return add_unsigned_long_long(&usage->directories, 1, "Directory count");
}

static int calculate_disk_usage_recursive(const char *path,
                                          struct disk_usage *usage)
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    int status = 0;

    if (fs_lstat(path, &st) != 0) {
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        if (add_file_count(usage) != 0 || add_file_size(usage, &st) != 0) {
            return 1;
        }
        return 0;
    }

    if (add_directory_count(usage) != 0) {
        return 1;
    }

    dir = fs_opendir(path);
    if (dir == NULL) {
        return 1;
    }

    for (;;) {
        char *child_path;

        if (fs_readdir(dir, path, &entry) != 0) {
            status = 1;
            break;
        }

        if (entry == NULL) {
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        child_path = join_path(path, entry->d_name);
        if (child_path == NULL) {
            fs_report_errno("malloc");
            status = 1;
            break;
        }

        if (calculate_disk_usage_recursive(child_path, usage) != 0) {
            status = 1;
        }

        free(child_path);
    }

    if (fs_closedir(dir, path) != 0) {
        status = 1;
    }

    return status;
}

int analyzer_analyze(const char *path)
{
    struct stat st;

    if (fs_lstat(path, &st) != 0) {
        return 1;
    }

    utils_print_file_metadata(path, &st, "lstat");
    return 0;
}

int analyzer_tree(const char *path)
{
    struct stat st;

    if (fs_lstat(path, &st) != 0) {
        return 1;
    }

    printf("%s%s\n", path, S_ISDIR(st.st_mode) ? "/" : "");
    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }

    return print_tree_recursive(path, "");
}

int analyzer_du(const char *path)
{
    struct disk_usage usage = {0, 0, 0};
    int status;

    status = calculate_disk_usage_recursive(path, &usage);

    printf("Path: %s\n", path);
    printf("Total file size: %llu bytes\n", usage.total_size);
    printf("Files: %llu\n", usage.files);
    printf("Directories: %llu\n", usage.directories);

    return status;
}

int analyzer_superblock(const char *path)
{
    return ext4_print_superblock_info(path);
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        fprintf(stderr, "invalid integer '%s'\n", text);
        return 1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

static int with_ext4_fs(const char *image,
                        int (*operation)(const struct ext4_fs *fs,
                                         const char *path,
                                         const char *arg),
                        const char *path, const char *arg)
{
    struct ext4_fs fs;
    int status;

    if (ext4_fs_open(image, 1, &fs) != 0) {
        return 1;
    }

    status = operation(&fs, path, arg);
    if (ext4_fs_close(&fs, image) != 0) {
        status = 1;
    }

    return status;
}

static int op_ls(const struct ext4_fs *fs, const char *path, const char *arg)
{
    (void)arg;
    return ext4_print_directory_path(fs, path);
}

static int op_walk(const struct ext4_fs *fs, const char *path, const char *arg)
{
    (void)arg;
    return ext4_walk_directory_path(fs, path);
}

static int op_find_name(const struct ext4_fs *fs, const char *path,
                        const char *arg)
{
    return ext4_search_filename(fs, path, arg);
}

static int op_find_inode(const struct ext4_fs *fs, const char *path,
                         const char *arg)
{
    uint32_t inode_number;

    if (parse_u32(arg, &inode_number) != 0) {
        return 1;
    }

    return ext4_search_inode_number(fs, path, inode_number);
}

static int op_find_corrupt(const struct ext4_fs *fs, const char *path,
                           const char *arg)
{
    (void)arg;
    return ext4_search_corrupted_entries(fs, path);
}

int analyzer_ext4_ls(const char *image, const char *path)
{
    return with_ext4_fs(image, op_ls, path, NULL);
}

int analyzer_ext4_walk(const char *image, const char *path)
{
    return with_ext4_fs(image, op_walk, path, NULL);
}

int analyzer_ext4_find_name(const char *image, const char *path,
                            const char *name)
{
    return with_ext4_fs(image, op_find_name, path, name);
}

int analyzer_ext4_find_inode(const char *image, const char *path,
                             const char *inode_text)
{
    return with_ext4_fs(image, op_find_inode, path, inode_text);
}

int analyzer_ext4_find_corrupt(const char *image, const char *path)
{
    return with_ext4_fs(image, op_find_corrupt, path, NULL);
}

static int op_chain(const struct ext4_fs *fs, const char *path, const char *arg)
{
    (void)arg;
    return ext4_print_chain_for_path(fs, path);
}

int analyzer_ext4_chain(const char *image, const char *path)
{
    return with_ext4_fs(image, op_chain, path, NULL);
}

int analyzer_ext4_shared_blocks(const char *image)
{
    struct ext4_fs fs;
    int status;

    if (ext4_fs_open(image, 1, &fs) != 0) {
        return 1;
    }

    status = ext4_print_shared_blocks(&fs);
    if (ext4_fs_close(&fs, image) != 0) {
        status = 1;
    }

    return status;
}

int analyzer_ext4_corruption_scan(const char *image)
{
    struct ext4_fs fs;
    int status;

    if (ext4_fs_open(image, 1, &fs) != 0) {
        return 1;
    }

    status = ext4_print_corruption_scan(&fs);
    if (ext4_fs_close(&fs, image) != 0) {
        status = 1;
    }

    return status;
}

int analyzer_ext4_backups(const char *image)
{
    return ext4_recovery_print_backups(image);
}

int analyzer_ext4_compare_super(const char *image)
{
    return ext4_recovery_compare_superblocks(image);
}

int analyzer_ext4_restore_super(const char *image, const char *index_text,
                                int write_enabled)
{
    return ext4_recovery_restore_superblock(image, index_text, write_enabled);
}
