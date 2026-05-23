# EXT4 Inspector

A low-level filesystem analysis and recovery toolkit written in C.

EXT4 Inspector provides direct access to EXT4 filesystem structures, enabling inspection of superblocks, block groups, inodes, directories, allocation chains, and backup metadata without relying on high-level filesystem APIs.

The project focuses on Linux system programming, filesystem internals, raw block access, corruption diagnostics, and safe metadata recovery.

---

# Tech Stack

`C11` `Linux` `POSIX API` `EXT4` `GCC` `Makefile`

---

# Key Features

## Filesystem Analysis

- EXT4 superblock parsing
- Block group descriptor inspection
- Inode loading and validation
- Block allocation chain traversal
- Path resolution

## Directory Operations

- Directory listing
- Recursive filesystem traversal
- Filename search
- Inode search

## Corruption Diagnostics

- Shared block detection
- Corruption scanning
- Metadata validation
- Chain consistency checks

## Recovery Tools

- Backup superblock discovery
- Backup comparison
- Read-only recovery previews
- Verified superblock restoration

## General Filesystem Utilities

- File metadata inspection
- Disk usage calculation
- Directory tree generation
- File reading and writing
- Link management
- Permission management

---

# Architecture

```text
CLI Commands
      ↓
Filesystem Analyzer
      ↓
EXT4 Metadata Layer
      ↓
Raw Block I/O
      ↓
Filesystem Image / Block Device
````

## EXT4 Metadata Layer

```text
ext4_io.c        Raw checked I/O
ext4_super.c     Superblock parsing
ext4_group.c     Group descriptor parsing
ext4_inode.c     Inode loading
ext4_block.c     Block traversal
ext4_chain.c     Block ownership analysis
ext4_dir.c       Directory parsing
ext4_path.c      Path resolution
ext4_search.c    Metadata search
ext4_backup.c    Backup discovery
ext4_recovery.c  Recovery operations
```

---

# Build

Build debug version:

```bash
make
```

or

```bash
make debug
```

Build release version:

```bash
make release
```

Run tests:

```bash
make test
```

Clean build artifacts:

```bash
make clean
```

---

# EXT4 Analysis Commands

## Read Superblock

Reads and validates the primary EXT4 superblock.

### Command

```bash
build/debug/fs_tool superblock image.ext4
```

### Example Output

```text
Filesystem Information

Magic Number: 0xEF53
Block Size: 4096
Inode Count: 65536
Block Count: 262144
Free Blocks: 184320
Free Inodes: 63421
Filesystem State: Clean
```

---

## List Directory Contents

Reads directory entries directly from the EXT4 image.

### Command

```bash
build/debug/fs_tool ext4-ls image.ext4 /
```

### Example Output

```text
drwxr-xr-x  home
drwxr-xr-x  etc
drwxr-xr-x  var
-rw-r--r--  README.txt
-rwxr-xr-x  script.sh
```

---

## Recursive Filesystem Walk

Traverses directory structures directly from EXT4 metadata.

### Command

```bash
build/debug/fs_tool ext4-walk image.ext4 /
```

### Example Output

```text
/
├── home
│   ├── user
│   └── documents
├── etc
├── var
└── README.txt
```

---

## Find File By Name

Searches the filesystem for matching filenames.

### Command

```bash
build/debug/fs_tool ext4-find-name image.ext4 / report.pdf
```

### Example Output

```text
Match Found

Path: /home/user/documents/report.pdf
Inode: 4521
```

---

## Find Inode

Locates files by inode number.

### Command

```bash
build/debug/fs_tool ext4-find-inode image.ext4 / 4521
```

### Example Output

```text
Inode Found

Path: /home/user/documents/report.pdf
Type: Regular File
Size: 24576 bytes
```

---

## Analyze Block Ownership Chain

Displays inode block allocation information.

### Command

```bash
build/debug/fs_tool ext4-chain image.ext4 /home/user/report.pdf
```

### Example Output

```text
Inode: 4521

Direct Blocks:
  1042
  1043
  1044

Indirect Blocks:
  3271

Total Blocks: 4
Corruption Detected: 0
```

---

## Corruption Scan

Performs metadata consistency checks.

### Command

```bash
build/debug/fs_tool ext4-corruption-scan image.ext4
```

### Example Output

```text
Filesystem Scan Complete

Directories Checked: 254
Files Checked: 1328

Corrupted Structures: 0
Shared Blocks: 0

Status: CLEAN
```

---

## Discover Backup Superblocks

Locates backup EXT4 superblocks.

### Command

```bash
build/debug/fs_tool ext4-backups image.ext4
```

### Example Output

```text
Backup Superblocks

Group 1    Block 32768
Group 3    Block 98304
Group 5    Block 163840

3 backup copies found
```

---

## Recovery Preview

Compares primary and backup metadata without modifying the filesystem.

### Command

```bash
build/debug/fs_tool ext4-restore-super image.ext4 0
```

### Example Output

```text
Recovery Preview

Selected Backup: Group 1
Backup State: Valid

Primary Superblock:
  Free Blocks: 184000

Backup Superblock:
  Free Blocks: 184320

No data written (preview mode)
```

---

# Filesystem Utilities

## Analyze Path

### Command

```bash
build/debug/fs_tool analyze .
```

### Example Output

```text
Path: .
Type: Directory
Size: 4096 bytes
Permissions: rwxr-xr-x
```

---

## Directory Tree

### Command

```bash
build/debug/fs_tool tree .
```

### Example Output

```text
.
├── src
├── build
├── README
└── Makefile
```

---

## Disk Usage

### Command

```bash
build/debug/fs_tool du .
```

### Example Output

```text
Files: 45
Directories: 12
Total Size: 3.4 MB
```

---

## File Metadata

### Command

```bash
build/debug/fs_tool stat README
```

### Example Output

```text
Type: Regular File
Size: 12345 bytes
Permissions: rw-r--r--
Inode: 31824
Links: 1
```

---

# Technical Highlights

* Direct EXT4 metadata parsing
* Raw block-level I/O
* Safe offset validation
* POSIX system call usage
* Recovery-oriented tooling
* Modular architecture (20+ source files)
* Strict C11 compilation
* Warning-free builds (`-Wall -Wextra -pedantic -Werror`)
