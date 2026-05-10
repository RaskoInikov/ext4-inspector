#include "analyzer.h"
#include "editor.h"
#include "utils.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "ui_main.h"
#include "utils.h"

static int require_arguments(const char *program_name, const char *command,
                             int actual, int expected)
{
    if (actual == expected) {
        return 0;
    }

    fprintf(stderr, "Invalid arguments for command '%s'\n", command);
    utils_print_usage(program_name);
    return 1;
}

static int run_command(const char *program_name, int argc, char *argv[])
{
    const char *command = argv[0];

    if (strcmp(command, "analyze") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_analyze(argv[1]);
    }

    if (strcmp(command, "tree") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_tree(argv[1]);
    }

    if (strcmp(command, "du") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_du(argv[1]);
    }

    if (strcmp(command, "superblock") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_superblock(argv[1]);
    }

    if (strcmp(command, "ext4-ls") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return analyzer_ext4_ls(argv[1], argv[2]);
    }

    if (strcmp(command, "ext4-walk") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return analyzer_ext4_walk(argv[1], argv[2]);
    }

    if (strcmp(command, "ext4-find-name") == 0) {
        if (require_arguments(program_name, command, argc - 1, 3) != 0) {
            return 1;
        }
        return analyzer_ext4_find_name(argv[1], argv[2], argv[3]);
    }

    if (strcmp(command, "ext4-find-inode") == 0) {
        if (require_arguments(program_name, command, argc - 1, 3) != 0) {
            return 1;
        }
        return analyzer_ext4_find_inode(argv[1], argv[2], argv[3]);
    }

    if (strcmp(command, "ext4-find-corrupt") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return analyzer_ext4_find_corrupt(argv[1], argv[2]);
    }

    if (strcmp(command, "ext4-chain") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return analyzer_ext4_chain(argv[1], argv[2]);
    }

    if (strcmp(command, "ext4-shared-blocks") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_ext4_shared_blocks(argv[1]);
    }

    if (strcmp(command, "ext4-corruption-scan") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_ext4_corruption_scan(argv[1]);
    }

    if (strcmp(command, "ext4-backups") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_ext4_backups(argv[1]);
    }

    if (strcmp(command, "ext4-compare-super") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return analyzer_ext4_compare_super(argv[1]);
    }

    if (strcmp(command, "ext4-restore-super") == 0) {
        int write_enabled = 0;

        if (argc != 3 && argc != 4) {
            fprintf(stderr, "Invalid arguments for command '%s'\n", command);
            utils_print_usage(program_name);
            return 1;
        }

        if (argc == 4) {
            if (strcmp(argv[3], "--write") != 0) {
                fprintf(stderr, "Unknown restore option: %s\n", argv[3]);
                utils_print_usage(program_name);
                return 1;
            }
            write_enabled = 1;
        }

        return analyzer_ext4_restore_super(argv[1], argv[2], write_enabled);
    }

    if (strcmp(command, "ui") == 0) {
        if (require_arguments(program_name,
                              command,
                              argc - 1,
                              0) != 0) {
            return 1;
        }

        return ui_main_run();
    }

    if (strcmp(command, "stat") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return editor_stat(argv[1]);
    }

    if (strcmp(command, "read") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return editor_read_file(argv[1]);
    }

    if (strcmp(command, "write") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return editor_write_file(argv[1], argv[2]);
    }

    if (strcmp(command, "append") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return editor_append_file(argv[1], argv[2]);
    }

    if (strcmp(command, "create") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return editor_create(argv[1]);
    }

    if (strcmp(command, "delete") == 0) {
        if (require_arguments(program_name, command, argc - 1, 1) != 0) {
            return 1;
        }
        return editor_delete(argv[1]);
    }

    if (strcmp(command, "rename") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return editor_rename_file(argv[1], argv[2]);
    }

    if (strcmp(command, "chmod") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return editor_chmod(argv[1], argv[2]);
    }

    if (strcmp(command, "link") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return editor_link(argv[1], argv[2]);
    }

    if (strcmp(command, "symlink") == 0) {
        if (require_arguments(program_name, command, argc - 1, 2) != 0) {
            return 1;
        }
        return editor_symlink(argv[1], argv[2]);
    }

    fprintf(stderr, "Unknown command: %s\n", command);
    utils_print_usage(program_name);
    return 1;
}

int main(int argc, char *argv[])
{
    int opt;
    static const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };

    if (argc == 1) {
        utils_print_usage(argv[0]);
        return 0;
    }

    while ((opt = getopt_long(argc, argv, "+hv", long_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            utils_print_usage(argv[0]);
            return 0;
        case 'v':
            utils_print_version();
            return 0;
        default:
            utils_print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        utils_print_usage(argv[0]);
        return 0;
    }

    return run_command(argv[0], argc - optind, &argv[optind]);
}
