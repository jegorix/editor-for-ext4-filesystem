# ext4tool

`ext4tool` is a coursework project for Operating Systems and System Programming. It is an interactive analyzer and safe metadata editor for ext4 filesystem images, implemented in C with an `ncurses` dashboard.

The tool is designed for offline work with ext4 image files. It can inspect core filesystem structures, resolve paths and inode references, browse directories, display summary statistics, and apply a limited set of metadata updates in explicit write mode.

## Overview

The project focuses on two goals:

- inspection of important ext4 metadata in a readable terminal UI
- controlled metadata editing with basic safety checks

The program opens an image, validates the primary superblock, falls back to a backup superblock if needed, checks feature compatibility, and then starts the interactive dashboard.

If the image uses unsupported ext4 `incompat` features, the program forces readonly mode even if it was started with `--write`.

## Implemented functionality

The dashboard includes the following screens:

- `Superblock View` - inspect primary superblock fields such as block size, inode size, feature flags, volume label, counters, and check interval
- `Backup Superblocks` - list candidate backup superblock locations derived from ext4 layout rules
- `Group Descriptor` - inspect one block group descriptor, including inode table location and free block/inode counters
- `Inode View` - inspect a selected inode: mode, UID/GID, size, timestamps, links count, flags, block pointers, and extent usage flag
- `Directory Browser` - list directory entries for an absolute path or for a direct inode target like `inode:2`
- `Resolve Path` - resolve an absolute path to the corresponding inode number
- `Resolve Inode` - search the directory tree and resolve an inode number back to a path
- `Search By Name` - find the first matching entry name during traversal from the root directory
- `Edit Superblock` - modify selected superblock metadata in write mode
- `Edit Inode` - modify selected inode metadata in write mode
- `Filesystem Statistics` - aggregate block group counters and display capacity and usage summary
- `Root Directory Statistics` - show counts of files, directories, symlinks, hidden names, and average name length in the root directory
- `Language` - switch between English and Russian UI
- `Help` - show a short guide to navigation and features

## Supported metadata edits

Editing is available only when the program is started with `--write`.

Supported superblock fields:

- volume label
- mount count
- max mount count
- check interval

Supported inode fields:

- mode
- UID
- GID
- atime
- ctime
- mtime
- flags

## Safety model

The editor follows a simple safety workflow for each write operation:

1. create a backup copy of the source image
2. apply the requested metadata update
3. reread the modified structure
4. verify that the updated field contains the expected value

Additional safety restrictions:

- default mode is `--readonly`
- editing is blocked while readonly mode is active
- unsupported ext4 `incompat` features force readonly mode
- the tool is intended for image files, not mounted live partitions

Backup files are created next to the source image in the form `<image>.<timestamp>.bak`.

## Project structure

```text
editor-for-ext4-filesystem/
├── include/              public headers
├── src/                  implementation
├── Makefile              build script
├── README.md             project documentation
└── test-ext4.img         sample ext4 image
```

## Source layout

Main modules:

- `src/main.c` - command line parsing, image opening, superblock validation, feature checks, dashboard startup
- `src/ext4_io.c` - low-level image access, bounds checking, backup creation
- `src/ext4_super.c` - superblock parsing, backup superblock search, group descriptor access, feature compatibility checks
- `src/ext4_inode.c` - inode offset calculation, inode reading, limited inode field writing
- `src/ext4_dir.c` - directory entry parsing, absolute path lookup, inode-to-path and name-based search
- `src/metadata_editor.c` - write workflow, input validation, post-write verification
- `src/dashboard.c` - interactive `ncurses` interface and all screens
- `src/util.c` - helper functions for parsing, formatting, and small utilities

Headers in `include/` expose the corresponding structures and functions used across modules.

## Requirements

- C compiler with C11 support, for example `gcc`
- `make`
- `ncurses` development library

Typical package names:

- Debian/Ubuntu: `libncurses-dev`
- Fedora: `ncurses-devel`
- Arch Linux: `ncurses`

## Build

Build the project with:

```bash
make
```

The resulting binary is:

```bash
build/ext4tool
```

Clean build artifacts with:

```bash
make clean
```

## Command line usage

```text
--image <path>       path to an ext4 image file
--readonly           readonly mode (default)
--write              enable write mode
--lang <en|ru>       interface language
-h, --help           show help
```

Examples:

Readonly mode:

```bash
./build/ext4tool --image ./test-ext4.img --readonly
```

Write mode:

```bash
./build/ext4tool --image ./test-ext4.img --write
```

Russian interface:

```bash
./build/ext4tool --image ./test-ext4.img --lang ru
```

Show help:

```bash
./build/ext4tool --help
```

## Interface controls

- `Up` / `Down` - move through menu items
- `Enter` - open the selected screen or confirm input
- `Esc` - close a dialog or go back
- `q` - exit the current window or the main screen

## Working with paths and directories

- path lookup accepts only absolute paths such as `/`, `/home`, `/lost+found`
- directory browser accepts either `/path` or `inode:N`, for example `inode:2`
- name search returns the first match found during traversal from the root directory
- inode-to-path resolution is implemented by directory tree traversal starting at the root inode

## Data exposed by the tool

The program currently reads and displays:

- primary superblock data
- candidate backup superblock offsets
- selected group descriptor data
- inode metadata
- directory entries
- aggregate filesystem usage counters
- simple root directory statistics

## Limitations

This is a coursework tool, not a full ext4 implementation. Current limitations include:

- only a limited subset of metadata fields can be edited
- directory name search returns the first match, not all matches
- the tool is intended for ext4 images and does not aim to support mounted devices
- some ext4 feature combinations may trigger forced readonly mode
- backup images are created as full image copies, which can be expensive for large files

## Sample workflow

1. build the project with `make`
2. open an image in readonly mode
3. inspect the superblock, group descriptors, and inode data
4. browse a directory or resolve a path to an inode
5. if metadata editing is needed, restart with `--write`
6. apply one metadata change and verify the generated backup path

## License

The repository root may contain a separate license file for the overall coursework repository. This directory itself does not define an additional project-specific license inside `editor-for-ext4-filesystem/`.
