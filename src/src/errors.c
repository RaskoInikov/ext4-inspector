#include "errors.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

int fs_report_errno(const char *context)
{
    int saved_errno = errno;

    fprintf(stderr, "%s: %s\n", context, strerror(saved_errno));
    return 1;
}

int fs_stat(const char *path, struct stat *st)
{
    if (stat(path, st) != 0) {
        return fs_report_errno(path);
    }

    return 0;
}

int fs_lstat(const char *path, struct stat *st)
{
    if (lstat(path, st) != 0) {
        return fs_report_errno(path);
    }

    return 0;
}

DIR *fs_opendir(const char *path)
{
    DIR *dir = opendir(path);

    if (dir == NULL) {
        fs_report_errno(path);
    }

    return dir;
}

int fs_readdir(DIR *dir, const char *path, struct dirent **entry)
{
    errno = 0;
    *entry = readdir(dir);

    if (*entry == NULL && errno != 0) {
        return fs_report_errno(path);
    }

    return 0;
}

int fs_closedir(DIR *dir, const char *path)
{
    if (closedir(dir) != 0) {
        return fs_report_errno(path);
    }

    return 0;
}

ssize_t fs_readlink(const char *path, char *buffer, size_t size)
{
    ssize_t result = readlink(path, buffer, size);

    if (result < 0) {
        fs_report_errno(path);
    }

    return result;
}

int fs_open(const char *path, int flags, mode_t mode)
{
    int fd = open(path, flags, mode);

    if (fd < 0) {
        fs_report_errno(path);
    }

    return fd;
}

ssize_t fs_read(int fd, void *buffer, size_t count, const char *context)
{
    ssize_t result;

    do {
        result = read(fd, buffer, count);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        fs_report_errno(context);
    }

    return result;
}

ssize_t fs_write(int fd, const void *buffer, size_t count, const char *context)
{
    ssize_t result;

    do {
        result = write(fd, buffer, count);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        fs_report_errno(context);
    }

    return result;
}

ssize_t fs_pread(int fd, void *buffer, size_t count, off_t offset,
                 const char *context)
{
    ssize_t result;

    do {
        result = pread(fd, buffer, count, offset);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        fs_report_errno(context);
    }

    return result;
}

ssize_t fs_pwrite(int fd, const void *buffer, size_t count, off_t offset,
                  const char *context)
{
    ssize_t result;

    do {
        result = pwrite(fd, buffer, count, offset);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        fs_report_errno(context);
    }

    return result;
}

int fs_close(int fd, const char *context)
{
    if (close(fd) != 0) {
        return fs_report_errno(context);
    }

    return 0;
}

int fs_unlink(const char *path)
{
    if (unlink(path) != 0) {
        return fs_report_errno(path);
    }

    return 0;
}

int fs_rename(const char *old_path, const char *new_path)
{
    if (rename(old_path, new_path) != 0) {
        int saved_errno = errno;

        fprintf(stderr, "rename %s -> %s: %s\n",
                old_path, new_path, strerror(saved_errno));
        return 1;
    }

    return 0;
}

int fs_chmod(const char *path, mode_t mode)
{
    if (chmod(path, mode) != 0) {
        return fs_report_errno(path);
    }

    return 0;
}

int fs_link(const char *src, const char *dst)
{
    if (link(src, dst) != 0) {
        int saved_errno = errno;

        fprintf(stderr, "link %s -> %s: %s\n", src, dst, strerror(saved_errno));
        return 1;
    }

    return 0;
}

int fs_symlink(const char *src, const char *dst)
{
    if (symlink(src, dst) != 0) {
        int saved_errno = errno;

        fprintf(stderr, "symlink %s -> %s: %s\n", src, dst, strerror(saved_errno));
        return 1;
    }

    return 0;
}
