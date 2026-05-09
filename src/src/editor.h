#ifndef EDITOR_H
#define EDITOR_H

int editor_stat(const char *file);
int editor_read_file(const char *file);
int editor_write_file(const char *file, const char *content);
int editor_append_file(const char *file, const char *content);
int editor_create(const char *file);
int editor_delete(const char *file);
int editor_rename_file(const char *old_path, const char *new_path);
int editor_chmod(const char *mode, const char *file);
int editor_link(const char *src, const char *dst);
int editor_symlink(const char *src, const char *dst);

#endif
