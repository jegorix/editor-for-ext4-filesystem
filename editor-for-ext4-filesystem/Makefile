CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
LDLIBS ?= -lncurses

BUILD_DIR := build
BIN := $(BUILD_DIR)/ext4tool

SRC := \
	src/main.c \
	src/util.c \
	src/ext4_io.c \
	src/ext4_super.c \
	src/ext4_inode.c \
	src/ext4_dir.c \
	src/metadata_editor.c \
	src/dashboard.c

OBJ := $(SRC:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR)
