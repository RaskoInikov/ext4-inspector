CC := gcc
PROGRAM := fs_tool

SRC_DIR := src
DEBUG_DIR := build/debug
RELEASE_DIR := build/release

SOURCES := $(SRC_DIR)/main.c \
           $(SRC_DIR)/analyzer.c \
           $(SRC_DIR)/editor.c \
           $(SRC_DIR)/errors.c \
           $(SRC_DIR)/ext4_backup.c \
           $(SRC_DIR)/ext4_block.c \
           $(SRC_DIR)/ext4_chain.c \
           $(SRC_DIR)/ext4_dir.c \
           $(SRC_DIR)/ext4_group.c \
           $(SRC_DIR)/ext4_inode.c \
           $(SRC_DIR)/ext4_io.c \
           $(SRC_DIR)/ext4_path.c \
           $(SRC_DIR)/ext4_recovery.c \
           $(SRC_DIR)/ext4_search.c \
           $(SRC_DIR)/ext4_super.c \
           $(SRC_DIR)/ui.c \
           $(SRC_DIR)/utils.c

COMMON_CFLAGS := -std=c11 -Wall -Wextra -pedantic -Werror -D_GNU_SOURCE
DEBUG_CFLAGS  := $(COMMON_CFLAGS) -g -O0
RELEASE_CFLAGS := $(COMMON_CFLAGS) -O2 -DNDEBUG

# ncurses link flags – use ncursesw for wide-char support if available,
# fall back to ncurses.
NCURSES_LDFLAGS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncurses)

.PHONY: all debug release test clean

all: debug

debug: $(DEBUG_DIR)/$(PROGRAM)

release: $(RELEASE_DIR)/$(PROGRAM)

test: $(DEBUG_DIR)/ext4_metadata_tests
	$(DEBUG_DIR)/ext4_metadata_tests

$(DEBUG_DIR)/$(PROGRAM): $(SOURCES)
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(DEBUG_CFLAGS) -o $@ $(SOURCES) $(NCURSES_LDFLAGS)

$(RELEASE_DIR)/$(PROGRAM): $(SOURCES)
	@mkdir -p $(RELEASE_DIR)
	$(CC) $(RELEASE_CFLAGS) -o $@ $(SOURCES) $(NCURSES_LDFLAGS)

$(DEBUG_DIR)/ext4_metadata_tests: tests/ext4_metadata_tests.c \
                                  $(SRC_DIR)/errors.c \
                                  $(SRC_DIR)/ext4_backup.c \
                                  $(SRC_DIR)/ext4_block.c \
                                  $(SRC_DIR)/ext4_chain.c \
                                  $(SRC_DIR)/ext4_dir.c \
                                  $(SRC_DIR)/ext4_group.c \
                                  $(SRC_DIR)/ext4_inode.c \
                                  $(SRC_DIR)/ext4_io.c \
                                  $(SRC_DIR)/ext4_path.c \
                                  $(SRC_DIR)/ext4_recovery.c \
                                  $(SRC_DIR)/ext4_search.c \
                                  $(SRC_DIR)/ext4_super.c
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(DEBUG_CFLAGS) -I$(SRC_DIR) -o $@ $^

clean:
	rm -f $(DEBUG_DIR)/$(PROGRAM) $(RELEASE_DIR)/$(PROGRAM) \
	      $(DEBUG_DIR)/ext4_metadata_tests
