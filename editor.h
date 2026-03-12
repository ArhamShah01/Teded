/*
 * editor.h
 * Central header for the teded terminal text editor.
 * Defines all shared data structures, constants, and function prototypes.
 */

#ifndef EDITOR_H
#define EDITOR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/ioctl.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define TEDED_VERSION   "1.0.0"
#define TAB_SIZE        4           /* Number of spaces a tab expands to   */
#define STATUS_BAR_ROWS 2           /* Rows reserved at the bottom          */

/* Control key macro: strips upper bits, leaving Ctrl+<key> value */
#define CTRL_KEY(k) ((k) & 0x1f)

/* Special key codes (values above normal ASCII range) */
enum EditorKey {
    KEY_BACKSPACE  = 127,
    KEY_ARROW_UP   = 1000,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_DEL
};

/* -------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------- */

/*
 * EditorRow: represents one line of text in the buffer.
 *   chars    – raw characters as stored in the file
 *   render   – characters prepared for display (tabs expanded, etc.)
 *   size     – number of bytes in chars (not counting NUL)
 *   rsize    – number of bytes in render (not counting NUL)
 */
typedef struct {
    char  *chars;
    char  *render;
    int    size;
    int    rsize;
} EditorRow;

/*
 * EditorConfig: global editor state.
 * One instance (E) lives in editor.c and is extern'd everywhere.
 */
typedef struct {
    /* Cursor position in the buffer (col/row indices into rows[]) */
    int cx, cy;

    /* Horizontal render offset (for tab-expanded cursor column) */
    int rx;

    /* Top-left of the visible viewport (scroll offsets) */
    int rowoff;
    int coloff;

    /* Terminal dimensions */
    int screenrows;
    int screencols;

    /* File content */
    EditorRow *rows;
    int        numrows;

    /* File metadata */
    char *filename;
    int   dirty;        /* Non-zero when unsaved changes exist */

    /* Original terminal settings (restored on exit) */
    struct termios orig_termios;

    /* One-shot status message shown in the status bar */
    char status_msg[256];
} EditorConfig;

/* The single global editor state – defined in editor.c */
extern EditorConfig E;

/* -------------------------------------------------------------------------
 * editor.c – core initialisation and entry point
 * ---------------------------------------------------------------------- */
void editor_init(void);
void editor_set_status_message(const char *fmt, ...);
void die(const char *msg);

/* -------------------------------------------------------------------------
 * buffer.c – dynamic row operations
 * ---------------------------------------------------------------------- */
void buffer_update_render(EditorRow *row);
void buffer_insert_row(int at, const char *s, size_t len);
void buffer_free_row(EditorRow *row);
void buffer_delete_row(int at);
void buffer_row_insert_char(EditorRow *row, int at, int c);
void buffer_row_delete_char(EditorRow *row, int at);
void buffer_row_append_string(EditorRow *row, const char *s, size_t len);
int  buffer_row_cx_to_rx(EditorRow *row, int cx);
void buffer_insert_char(int c);
void buffer_delete_char(void);
void buffer_insert_newline(void);

/* -------------------------------------------------------------------------
 * file.c – file I/O
 * ---------------------------------------------------------------------- */
void file_open(const char *filename);
void file_save(void);
char *file_rows_to_string(int *buflen);

/* -------------------------------------------------------------------------
 * input.c – keyboard reading and dispatch
 * ---------------------------------------------------------------------- */
int  input_read_key(void);
void input_process_keypress(void);
void input_move_cursor(int key);

/* -------------------------------------------------------------------------
 * screen.c – terminal rendering
 * ---------------------------------------------------------------------- */
void screen_enable_raw_mode(void);
void screen_disable_raw_mode(void);
int  screen_get_window_size(int *rows, int *cols);
void screen_scroll(void);
void screen_draw_rows(char **ab, int *ab_len, int *ab_cap);
void screen_draw_status_bar(char **ab, int *ab_len, int *ab_cap);
void screen_draw_status_message(char **ab, int *ab_len, int *ab_cap);
void screen_refresh(void);
void screen_append(char **ab, int *ab_len, int *ab_cap,
                   const char *s, int slen);

#endif /* EDITOR_H */
