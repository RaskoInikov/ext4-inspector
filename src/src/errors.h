#ifndef ERRORS_H
#define ERRORS_H

#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int fs_report_errno(const char *context);
int fs_stat(const char *path, struct stat *st);
int fs_lstat(const char *path, struct stat *st);
DIR *fs_opendir(const char *path);
int fs_readdir(DIR *dir, const char *path, struct dirent **entry);
int fs_closedir(DIR *dir, const char *path);
ssize_t fs_readlink(const char *path, char *buffer, size_t size);
int fs_open(const char *path, int flags, mode_t mode);
ssize_t fs_read(int fd, void *buffer, size_t count, const char *context);
ssize_t fs_write(int fd, const void *buffer, size_t count, const char *context);
ssize_t fs_pread(int fd, void *buffer, size_t count, off_t offset,
                 const char *context);
ssize_t fs_pwrite(int fd, const void *buffer, size_t count, off_t offset,
                  const char *context);
int fs_close(int fd, const char *context);
int fs_unlink(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_chmod(const char *path, mode_t mode);
int fs_link(const char *src, const char *dst);
int fs_symlink(const char *src, const char *dst);

#endif
