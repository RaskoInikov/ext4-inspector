# File System Analyzer and Editor

`fs_tool` is a CLI-based C file system analyzer and editor.

## Project Structure

```text
.
├── Makefile
├── README
├── src/
│   ├── main.c
│   ├── analyzer.c
│   ├── analyzer.h
│   ├── editor.c
│   ├── editor.h
│   ├── errors.c
│   ├── errors.h
│   ├── ext4_backup.c
│   ├── ext4_backup.h
│   ├── ext4_block.c
│   ├── ext4_block.h
│   ├── ext4_chain.c
│   ├── ext4_chain.h
│   ├── ext4_dir.c
│   ├── ext4_dir.h
│   ├── ext4_group.c
│   ├── ext4_group.h
│   ├── ext4_inode.c
│   ├── ext4_inode.h
│   ├── ext4_io.c
│   ├── ext4_io.h
│   ├── ext4_path.c
│   ├── ext4_path.h
│   ├── ext4_recovery.c
│   ├── ext4_recovery.h
│   ├── ext4_search.c
│   ├── ext4_search.h
│   ├── ext4_super.c
│   ├── ext4_super.h
│   ├── utils.c
│   └── utils.h
└── build/
    ├── debug/
    └── release/
```

## Build

Build the debug binary:

```sh
make
```

or:

```sh
make debug
```

Build the release binary:

```sh
make release
```

Remove generated binaries:

```sh
make clean
```

The project is compiled as C11 with GCC-compatible strict warning flags:

```text
-std=c11 -Wall -Wextra -pedantic -Werror
```

System calls are routed through `errors.c`, which checks return values and prints failures with `strerror(errno)`.

Low-level filesystem image or block-device access is provided by `ext4_io.c`. It opens images/devices with `open()` and reads or writes whole blocks with `pread()` and `pwrite()` using overflow-checked block offsets.

```c
int ext4_io_open(const char *path, int writable);
int read_block(int fd, size_t block_size, uint64_t block_index, void *buffer);
int write_block(int fd, size_t block_size, uint64_t block_index, const void *buffer);
```

Ext4 superblock parsing reads the superblock at byte offset 1024, decodes documented little-endian fields, validates magic number `0xEF53`, and prints the result:

```sh
build/debug/fs_tool superblock image.ext4
```

The reusable ext4 metadata layer is split across:

```text
ext4_io.c      raw checked offset I/O
ext4_super.c   superblock parsing and filesystem context
ext4_group.c   block group descriptor parsing
ext4_inode.c   inode loading and validation
ext4_block.c   reduced direct/single-indirect block traversal
ext4_chain.c   inode chain analysis and block ownership maps
ext4_dir.c     raw ext4 directory entry parsing
ext4_path.c    path resolution and recursive traversal
ext4_search.c  recursive filename, inode, and corruption search
ext4_backup.c  sparse backup superblock discovery and comparison
ext4_recovery.c readonly recovery previews and guarded restore
```

Core APIs:

```c
int ext4_read_superblock(int fd, struct ext4_superblock *superblock);
int ext4_read_group_desc(const struct ext4_fs *fs, uint32_t group_index,
                         struct ext4_group_desc *desc);
int ext4_get_inode_table(const struct ext4_fs *fs, uint32_t group_index,
                         uint64_t *block_index);
int ext4_read_inode(const struct ext4_fs *fs, uint32_t inode_number,
                    struct ext4_inode *inode);
int ext4_list_directory_inode(const struct ext4_fs *fs, uint32_t inode_number,
                              ext4_dir_entry_callback callback, void *user);
int ext4_resolve_path(const struct ext4_fs *fs, uint32_t start_inode,
                      const char *path, uint32_t *inode_number);
int ext4_traverse_inode_blocks(const struct ext4_fs *fs,
                               const struct ext4_inode *inode,
                               ext4_block_callback callback,
                               void *user,
                               int *corruption_count);
int ext4_build_chain_for_inode(const struct ext4_fs *fs, uint32_t inode_number,
                               struct ext4_inode_chain *chain);
int ext4_backup_discover(const char *path, int writable,
                         struct ext4_backup_report *report);
```

Run metadata parser tests:

```sh
make test
```

Raw ext4 directory commands read filesystem image data directly; they do not use POSIX directory APIs:

```sh
build/debug/fs_tool ext4-ls image.ext4 /
build/debug/fs_tool ext4-walk image.ext4 /
build/debug/fs_tool ext4-find-name image.ext4 / target-name
build/debug/fs_tool ext4-find-inode image.ext4 / 12
build/debug/fs_tool ext4-find-corrupt image.ext4 /
build/debug/fs_tool ext4-chain image.ext4 /path/to/file
build/debug/fs_tool ext4-shared-blocks image.ext4
build/debug/fs_tool ext4-corruption-scan image.ext4
build/debug/fs_tool ext4-backups image.ext4
build/debug/fs_tool ext4-compare-super image.ext4
build/debug/fs_tool ext4-restore-super image.ext4 0
build/debug/fs_tool ext4-restore-super image.ext4 0 --write
```

`ext4-restore-super` is readonly by default. With `--write`, it requires typing `RESTORE`, writes only the primary superblock from the selected validated backup, then rereads and verifies key fields.

## Usage

Show help:

```sh
build/debug/fs_tool -h
build/debug/fs_tool --help
```

Show version:

```sh
build/debug/fs_tool -v
build/debug/fs_tool --version
```

Analyze a path:

```sh
build/debug/fs_tool analyze .
```

`analyze` uses `lstat()` so symbolic links are reported as links.

Print a directory tree:

```sh
build/debug/fs_tool tree .
```

`tree` performs a depth-first traversal with `opendir()`, `readdir()`, and `closedir()`. It skips `.` and `..`, prints symbolic link targets, and does not recurse into symbolic links.

Calculate recursive disk usage:

```sh
build/debug/fs_tool du .
```

`du` streams directory entries recursively, sums non-directory file sizes, counts files and directories, and treats symbolic links as files instead of recursing into them.

Print file metadata:

```sh
build/debug/fs_tool stat README
```

`stat` uses `stat()` and follows symbolic links. Metadata output includes file type, size, rwx permissions, inode, link count, and access/modify/change timestamps.

Read, overwrite, or append file content:

```sh
build/debug/fs_tool read sample.txt
build/debug/fs_tool write sample.txt "new content"
build/debug/fs_tool append sample.txt " more content"
```

`read` uses a dynamically resized buffer. `write` and `append` use POSIX `write()` and handle partial writes.

Create or delete a file:

```sh
build/debug/fs_tool create sample.txt
build/debug/fs_tool delete sample.txt
```

Rename a file or directory:

```sh
build/debug/fs_tool rename old-name.txt new-name.txt
```

Change permissions:

```sh
build/debug/fs_tool chmod 0644 README
```

Create links:

```sh
build/debug/fs_tool link README README.hardlink
build/debug/fs_tool symlink README README.symlink
```
