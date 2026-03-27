```
            ████████╗███████╗██████╗ ███████╗██████╗ 
            ╚══██╔══╝██╔════╝██╔══██╗██╔════╝██╔══██╗
               ██║   █████╗  ██║  ██║█████╗  ██║  ██║
               ██║   ██╔══╝  ██║  ██║██╔══╝  ██║  ██║
               ██║   ███████╗██████╔╝███████╗██████╔╝
               ╚═╝   ╚══════╝╚═════╝ ╚══════╝╚═════╝ 
```
A terminal text editor written in C99 for Linux. Works in raw terminal mode and renders output using ANSI escape sequences. No external libraries. No ncurses.

## Features

- Raw terminal mode via `termios` for immediate per-keystroke input
- Full multi-line editing: insert, delete, split and join lines
- Cursor movement via arrow keys, Page Up/Down, Home, End
- Vertical and horizontal scrolling
- Tab expansion (default width: 4 spaces)
- Open existing files or create new ones from the command line
- Save with `Ctrl+S`, quit with `Ctrl+Q`
- Warns before quitting with unsaved changes
- Status bar showing filename, line count, modified indicator, and key hints
- Entire frame written in a single `write()` call to prevent flicker
- Compiles clean under `-Wall -Wextra -Wpedantic -std=c99`

## Building

Requirements: GCC, GNU Make, Linux.

```bash
git clone https://github.com/<your-username>/teded.git
cd teded
make
```

Other targets:

```bash
make clean     # remove object files and binary
make run       # build and launch with an empty buffer
make test      # build and open the Makefile as a test file
```

## Usage

```bash
./teded <filename>
```

If the file does not exist it will be created on first save.

### Key Bindings

| Key | Action |
|---|---|
| Arrow keys | Move cursor |
| Page Up / Down | Scroll one screen |
| Home / End | Start / end of line |
| Backspace | Delete character to the left |
| Delete | Delete character to the right |
| Enter | Insert newline |
| Ctrl+S | Save |
| Ctrl+Q | Quit (press twice if there are unsaved changes) |

## Project Structure

```
teded/
├── editor.h      shared header: structs, constants, prototypes
├── editor.c      entry point, global state, init
├── buffer.c      row and character insert/delete, tab rendering
├── file.c        file read via getline, file write via open/write
├── input.c       escape sequence decoding, cursor movement, key dispatch
├── screen.c      raw mode, scrolling, frame rendering, status bar
└── Makefile
```

## How It Works

The buffer is a heap-allocated array of `EditorRow` structs. Each row stores two strings: `chars` for the raw content and `render` for the tab-expanded version used for display. Both are resized with `realloc()` on each edit.

The full screen is assembled into a single heap buffer each frame and flushed with one `write()` call. Arrow keys and other special keys arrive as multi-byte escape sequences and are decoded in `input_read_key()`.

File reading uses `getline()`. Saving uses `open()` with `O_TRUNC` and `write()` directly, with no stdio buffering in the write path.

## Limitations

- No syntax highlighting
- No search or replace
- No undo/redo
- Single buffer only
- No line-wrap mode

## License

MIT
