# ext4tool

`ext4tool` is a coursework project for Operating Systems and System Programming: an interactive analyzer and safe metadata editor for ext4 filesystem images.

The program works with an ext4 image file, displays key metadata through an `ncurses` interface, supports English and Russian UI, and allows editing a limited set of metadata only in explicit write mode.

## Project structure

- `src/` - source code
- `include/` - header files
- `Makefile` - build script
- `test-ext4.img` - sample ext4 image for local testing

## Features

The main dashboard includes the following screens:

- `Superblock View` - inspect main superblock fields
- `Backup Superblocks` - list candidate backup superblocks
- `Group Descriptor` - inspect a selected block group descriptor
- `Inode View` - inspect inode metadata
- `Directory Browser` - list directory entries by absolute path or `inode:N`
- `Resolve Path` - resolve an absolute path to an inode number
- `Resolve Inode` - resolve an inode number to a path
- `Search By Name` - find the first matching entry by name
- `Filesystem Statistics` - show aggregated filesystem statistics
- `Root Directory Statistics` - show simple root directory statistics
- `Language` - switch the interface language

## Metadata editing

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

For each write operation the tool:

- creates a backup copy of the image
- applies the change
- rereads metadata and verifies the updated value

## Safety restrictions

- Default mode is `--readonly`
- If unsupported ext4 `incompat` features are detected, the tool forces readonly mode
- Editing is blocked while readonly mode is active
- The program is intended for ext4 image files, not mounted live partitions

Backup files are created next to the source image in the form `<image>.<timestamp>.bak`.

## Requirements

- C compiler with C11 support, for example `gcc`
- `make`
- `ncurses` development library

## Build

```bash
make
```

Resulting binary:

```bash
build/ext4tool
```

## Run

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

Help:

```bash
./build/ext4tool --help
```

## Command line options

```text
--image <path>       path to an ext4 image file
--readonly           readonly mode (default)
--write              enable write mode
--lang <en|ru>       interface language
-h, --help           show help
```

## Interface controls

- `Up` / `Down` - move through menu items
- `Enter` - open the selected screen or confirm input
- `Esc` - close a dialog or go back
- `q` - exit the current window or the main screen

## Notes

- Path lookup accepts only absolute paths such as `/`, `/home`, `/lost+found`
- Directory browser accepts either `/path` or `inode:N`, for example `inode:2`
- Name search returns the first match found during traversal from the root directory
