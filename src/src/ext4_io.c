#include "ext4_io.h"

#include "errors.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int validate_block_request(size_t block_size, uint64_t block_index,
                                  const void *buffer, off_t *offset)
{
    uint64_t max_index;

    if (buffer == NULL) {
        fprintf(stderr, "block buffer: %s\n", strerror(EINVAL));
        return 1;
    }

    if (block_size == 0) {
        fprintf(stderr, "block size: %s\n", strerror(EINVAL));
        return 1;
    }

    if (block_size > (size_t)SSIZE_MAX) {
        fprintf(stderr, "block size: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    max_index = (uint64_t)((LLONG_MAX - (long long)(block_size - 1)) /
                           (long long)block_size);
    if (block_index > max_index) {
        fprintf(stderr, "block offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    *offset = (off_t)(block_index * (uint64_t)block_size);
    if ((uint64_t)(*offset) / (uint64_t)block_size != block_index) {
        fprintf(stderr, "block offset: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    return 0;
}

static int validate_offset_request(uint64_t offset, size_t size,
                                   const void *buffer, off_t *pos)
{
    if (buffer == NULL) {
        fprintf(stderr, "I/O buffer: %s\n", strerror(EINVAL));
        return 1;
    }

    if (size > (size_t)SSIZE_MAX) {
        fprintf(stderr, "I/O size: %s\n", strerror(EOVERFLOW));
        return 1;
    }

    if (offset > (uint64_t)LLONG_MAX ||
        (uint64_t)size > (uint64_t)LLONG_MAX - offset) {
        fprintf(stderr, "I/O offset %" PRIu64 ": %s\n",
                offset, strerror(EOVERFLOW));
        return 1;
    }

    *pos = (off_t)offset;
    return 0;
}

int ext4_io_open(const char *path, int writable)
{
    int flags = writable ? O_RDWR : O_RDONLY;

    return fs_open(path, flags, 0);
}

int ext4_io_read_exact_at(int fd, uint64_t offset, void *buffer, size_t size,
                          const char *context)
{
    off_t pos;
    size_t total = 0;

    if (validate_offset_request(offset, size, buffer, &pos) != 0) {
        return 1;
    }

    while (total < size) {
        ssize_t bytes_read = fs_pread(fd, (char *)buffer + total,
                                      size - total, pos + (off_t)total,
                                      context);

        if (bytes_read < 0) {
            return 1;
        }

        if (bytes_read == 0) {
            // fprintf(stderr, "%s: unexpected end of file\n", context);
            return 1;
        }

        total += (size_t)bytes_read;
    }

    return 0;
}

int ext4_io_write_exact_at(int fd, uint64_t offset, const void *buffer,
                           size_t size, const char *context)
{
    off_t pos;
    size_t total = 0;

    if (validate_offset_request(offset, size, buffer, &pos) != 0) {
        return 1;
    }

    while (total < size) {
        ssize_t bytes_written = fs_pwrite(fd, (const char *)buffer + total,
                                          size - total, pos + (off_t)total,
                                          context);

        if (bytes_written < 0) {
            return 1;
        }

        if (bytes_written == 0) {
            fprintf(stderr, "%s: wrote zero bytes\n", context);
            return 1;
        }

        total += (size_t)bytes_written;
    }

    return 0;
}

int read_block(int fd, size_t block_size, uint64_t block_index, void *buffer)
{
    off_t offset;

    if (validate_block_request(block_size, block_index, buffer, &offset) != 0) {
        return 1;
    }

    return ext4_io_read_exact_at(fd, (uint64_t)offset, buffer, block_size,
                                 "pread block");
}

int write_block(int fd, size_t block_size, uint64_t block_index,
                const void *buffer)
{
    off_t offset;

    if (validate_block_request(block_size, block_index, buffer, &offset) != 0) {
        return 1;
    }

    return ext4_io_write_exact_at(fd, (uint64_t)offset, buffer, block_size,
                                  "pwrite block");
}
