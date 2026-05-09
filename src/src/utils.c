#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FS_TOOL_VERSION "0.1.0"

static const char *file_type(mode_t mode)
{
    if (S_ISREG(mode)) {
        return "regular file";
    }
    if (S_ISDIR(mode)) {
        return "directory";
    }
    if (S_ISLNK(mode)) {
        return "symbolic link";
    }
    if (S_ISCHR(mode)) {
        return "character device";
    }
    if (S_ISBLK(mode)) {
        return "block device";
    }
    if (S_ISFIFO(mode)) {
        return "fifo";
    }
    if (S_ISSOCK(mode)) {
        return "socket";
    }

    return "unknown";
}

static char file_type_char(mode_t mode)
{
    if (S_ISREG(mode)) {
        return '-';
    }
    if (S_ISDIR(mode)) {
        return 'd';
    }
    if (S_ISLNK(mode)) {
        return 'l';
    }
    if (S_ISCHR(mode)) {
        return 'c';
    }
    if (S_ISBLK(mode)) {
        return 'b';
    }
    if (S_ISFIFO(mode)) {
        return 'p';
    }
    if (S_ISSOCK(mode)) {
        return 's';
    }

    return '?';
}

static void format_permissions(mode_t mode, char permissions[11])
{
    permissions[0] = file_type_char(mode);
    permissions[1] = (mode & S_IRUSR) ? 'r' : '-';
    permissions[2] = (mode & S_IWUSR) ? 'w' : '-';
    permissions[3] = (mode & S_IXUSR) ? 'x' : '-';
    permissions[4] = (mode & S_IRGRP) ? 'r' : '-';
    permissions[5] = (mode & S_IWGRP) ? 'w' : '-';
    permissions[6] = (mode & S_IXGRP) ? 'x' : '-';
    permissions[7] = (mode & S_IROTH) ? 'r' : '-';
    permissions[8] = (mode & S_IWOTH) ? 'w' : '-';
    permissions[9] = (mode & S_IXOTH) ? 'x' : '-';
    permissions[10] = '\0';

    if (mode & S_ISUID) {
        permissions[3] = (mode & S_IXUSR) ? 's' : 'S';
    }
    if (mode & S_ISGID) {
        permissions[6] = (mode & S_IXGRP) ? 's' : 'S';
    }
    if (mode & S_ISVTX) {
        permissions[9] = (mode & S_IXOTH) ? 't' : 'T';
    }
}

static void format_time(time_t value, char *buffer, size_t buffer_size)
{
    struct tm tm_value;

    errno = 0;
    if (localtime_r(&value, &tm_value) == NULL) {
        snprintf(buffer, buffer_size, "unavailable: %s", strerror(errno));
        return;
    }

    if (strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S %z", &tm_value) == 0) {
        snprintf(buffer, buffer_size, "unavailable");
    }
}

void utils_print_file_metadata(const char *path, const struct stat *st,
                               const char *source)
{
    char permissions[11];
    char access_time[64];
    char modify_time[64];
    char change_time[64];

    format_permissions(st->st_mode, permissions);
    format_time(st->st_atime, access_time, sizeof(access_time));
    format_time(st->st_mtime, modify_time, sizeof(modify_time));
    format_time(st->st_ctime, change_time, sizeof(change_time));

    printf("Path: %s\n", path);
    printf("Metadata source: %s\n", source);
    printf("File type: %s\n", file_type(st->st_mode));
    printf("Size: %lld bytes\n", (long long)st->st_size);
    printf("Permissions: %s (%04lo)\n", permissions, (unsigned long)(st->st_mode & 07777));
    printf("Inode: %llu\n", (unsigned long long)st->st_ino);
    printf("Links: %llu\n", (unsigned long long)st->st_nlink);
    printf("Accessed: %s\n", access_time);
    printf("Modified: %s\n", modify_time);
    printf("Changed: %s\n", change_time);
}

void utils_print_version(void)
{
    printf("fs_tool version %s\n", FS_TOOL_VERSION);
}

void utils_print_usage(const char *program_name)
{
    printf("Usage: %s [options] <command> [arguments]\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help       Show this help message\n");
    printf("  -v, --version    Show version information\n");
    printf("\n");
    printf("Commands:\n");
    printf("  analyze <path>        Analyze a file system path\n");
    printf("  tree <path>           Print a directory tree\n");
    printf("  du <path>             Calculate recursive disk usage\n");
    printf("  superblock <image>    Print ext4 superblock information\n");
    printf("  ext4-ls <img> <path>  List ext4 directory without POSIX dir APIs\n");
    printf("  ext4-walk <img> <p>   Recursively traverse ext4 directory\n");
    printf("  ext4-find-name <img> <p> <name>   Search ext4 filenames\n");
    printf("  ext4-find-inode <img> <p> <ino>   Search ext4 inode references\n");
    printf("  ext4-find-corrupt <img> <p>       Search corrupted ext4 entries\n");
    printf("  ext4-chain <img> <path>           Analyze inode block chain\n");
    printf("  ext4-shared-blocks <img>          Detect shared ext4 data blocks\n");
    printf("  ext4-corruption-scan <img>        Scan ext4 chains for corruption\n");
    printf("  ext4-backups <img>                Discover ext4 backup superblocks\n");
    printf("  ext4-compare-super <img>          Compare primary and backup superblocks\n");
    printf("  ext4-restore-super <img> <idx> [--write]  Preview or restore primary superblock\n");
    printf("  stat <file>           Print file metadata\n");
    printf("  read <file>           Print file content\n");
    printf("  write <file> <text>   Overwrite file content\n");
    printf("  append <file> <text>  Append to file content\n");
    printf("  create <file>         Create an empty file\n");
    printf("  delete <file>         Delete a file\n");
    printf("  rename <old> <new>    Rename a file or directory\n");
    printf("  chmod <mode> <file>   Change file permissions\n");
    printf("  link <src> <dst>      Create a hard link\n");
    printf("  symlink <src> <dst>   Create a symbolic link\n");
}
