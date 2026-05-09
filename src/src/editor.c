#include "editor.h"
#include "errors.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int parse_mode(const char *mode_text, mode_t *mode)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(mode_text, &end, 8);
    if (errno != 0 || end == mode_text || *end != '\0' || value < 0 || value > 07777) {
        fprintf(stderr, "Invalid mode: %s\n", mode_text);
        return 1;
    }

    *mode = (mode_t)value;
    return 0;
}

static int grow_buffer(char **buffer, size_t *capacity)
{
    char *resized;
    size_t new_capacity;

    if (*capacity > SIZE_MAX / 2) {
        fprintf(stderr, "File is too large to read safely\n");
        return 1;
    }

    new_capacity = (*capacity == 0) ? 4096 : *capacity * 2;
    resized = realloc(*buffer, new_capacity);
    if (resized == NULL) {
        errno = ENOMEM;
        fs_report_errno("realloc");
        return 1;
    }

    *buffer = resized;
    *capacity = new_capacity;
    return 0;
}

static int write_all(int fd, const char *buffer, size_t length,
                     const char *target)
{
    size_t written = 0;

    while (written < length) {
        ssize_t result = fs_write(fd, buffer + written, length - written, target);

        if (result < 0) {
            return 1;
        }

        if (result == 0) {
            fprintf(stderr, "%s: write returned zero bytes\n", target);
            return 1;
        }

        written += (size_t)result;
    }

    return 0;
}

static int write_content_to_file(const char *file, const char *content,
                                 int append)
{
    int flags = O_WRONLY | O_CREAT;
    int fd;
    int status = 0;

    flags |= append ? O_APPEND : O_TRUNC;

    fd = fs_open(file, flags, 0666);
    if (fd < 0) {
        return 1;
    }

    if (write_all(fd, content, strlen(content), file) != 0) {
        status = 1;
    }

    if (fs_close(fd, file) != 0) {
        status = 1;
    }

    if (status == 0) {
        printf("%s: %s\n", append ? "Appended" : "Written", file);
    }

    return status;
}

int editor_stat(const char *file)
{
    struct stat st;

    if (fs_stat(file, &st) != 0) {
        return 1;
    }

    utils_print_file_metadata(file, &st, "stat");
    return 0;
}

int editor_read_file(const char *file)
{
    int fd;
    char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    int status = 0;

    fd = fs_open(file, O_RDONLY, 0);
    if (fd < 0) {
        return 1;
    }

    for (;;) {
        ssize_t result;

        if (size == capacity && grow_buffer(&buffer, &capacity) != 0) {
            status = 1;
            break;
        }

        result = fs_read(fd, buffer + size, capacity - size, file);
        if (result < 0) {
            status = 1;
            break;
        }

        if (result == 0) {
            break;
        }

        size += (size_t)result;
    }

    if (status == 0 && size > 0) {
        status = write_all(STDOUT_FILENO, buffer, size, "stdout");
    }

    free(buffer);

    if (fs_close(fd, file) != 0) {
        status = 1;
    }

    return status;
}

int editor_write_file(const char *file, const char *content)
{
    return write_content_to_file(file, content, 0);
}

int editor_append_file(const char *file, const char *content)
{
    return write_content_to_file(file, content, 1);
}

int editor_create(const char *file)
{
    int fd = fs_open(file, O_WRONLY | O_CREAT | O_EXCL, 0666);

    if (fd < 0) {
        return 1;
    }

    if (fs_close(fd, file) != 0) {
        return 1;
    }

    printf("Created: %s\n", file);
    return 0;
}

int editor_delete(const char *file)
{
    if (fs_unlink(file) != 0) {
        return 1;
    }

    printf("Deleted: %s\n", file);
    return 0;
}

int editor_rename_file(const char *old_path, const char *new_path)
{
    if (fs_rename(old_path, new_path) != 0) {
        return 1;
    }

    printf("Renamed: %s -> %s\n", old_path, new_path);
    return 0;
}

int editor_chmod(const char *mode_text, const char *file)
{
    mode_t mode;

    if (parse_mode(mode_text, &mode) != 0) {
        return 1;
    }

    if (fs_chmod(file, mode) != 0) {
        return 1;
    }

    printf("Changed permissions: %s -> %04lo\n", file, (unsigned long)mode);
    return 0;
}

int editor_link(const char *src, const char *dst)
{
    if (fs_link(src, dst) != 0) {
        return 1;
    }

    printf("Linked: %s -> %s\n", dst, src);
    return 0;
}

int editor_symlink(const char *src, const char *dst)
{
    if (fs_symlink(src, dst) != 0) {
        return 1;
    }

    printf("Symlinked: %s -> %s\n", dst, src);
    return 0;
}
