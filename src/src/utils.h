#ifndef UTILS_H
#define UTILS_H

#include <sys/stat.h>

void utils_print_file_metadata(const char *path, const struct stat *st,
                               const char *source);
void utils_print_version(void);
void utils_print_usage(const char *program_name);

#endif
