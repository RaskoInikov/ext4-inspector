#ifndef ANALYZER_H
#define ANALYZER_H

int analyzer_analyze(const char *path);
int analyzer_tree(const char *path);
int analyzer_du(const char *path);
int analyzer_superblock(const char *path);
int analyzer_ext4_ls(const char *image, const char *path);
int analyzer_ext4_walk(const char *image, const char *path);
int analyzer_ext4_find_name(const char *image, const char *path,
                            const char *name);
int analyzer_ext4_find_inode(const char *image, const char *path,
                             const char *inode_text);
int analyzer_ext4_find_corrupt(const char *image, const char *path);
int analyzer_ext4_chain(const char *image, const char *path);
int analyzer_ext4_shared_blocks(const char *image);
int analyzer_ext4_corruption_scan(const char *image);
int analyzer_ext4_backups(const char *image);
int analyzer_ext4_compare_super(const char *image);
int analyzer_ext4_restore_super(const char *image, const char *index_text,
                                int write_enabled);

#endif
