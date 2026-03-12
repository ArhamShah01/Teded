/*
 * screen.c
 * Terminal control and screen rendering for the teded editor.
 *
 * Responsibilities:
 *   - Enable / disable raw terminal mode
 *   - Query the terminal window size
 *   - Scroll the viewport to keep the cursor visible
 *   - Render the text rows, status bar, and status message
 *   - Assemble and flush the complete frame in one write() call
 *
 * All rendering is done via an append-buffer approach: we build up the
 * entire frame as a heap-allocated string, then write it to stdout in a
 * single call.  This avoids flickering caused by many small writes.
 *
 * ANSI escape codes used:
 *   \x1b[2J      – erase entire display
 *   \x1b[H       – move cursor to home (top-left)
 *   \x1b[?25l    – hide cursor
 *   \x1b[?25h    – show cursor
 *   \x1b[K       – erase from cursor to end of line
 *   \x1b[7m      – reverse video (for status bar highlight)
 *   \x1b[m       – reset attributes
 *   \x1b[%d;%dH  – move cursor to row, col (1-based)
 */

#include <stdarg.h>
#include "editor.h"

/* -------------------------------------------------------------------------
 * screen_disable_raw_mode
 *
 * Restore the terminal attributes that were saved before we entered raw
 * mode.  Registered with atexit() so it runs on any normal exit path.
 * ---------------------------------------------------------------------- */
void screen_disable_raw_mode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/* -------------------------------------------------------------------------
 * screen_enable_raw_mode
 *
 * Put the terminal into "raw" mode so we receive every keypress
 * immediately without the OS doing any line-editing or special-character
 * processing.
 *
 * Key flags we turn OFF:
 *   IXON   – disable Ctrl+S / Ctrl+Q flow control in the driver
 *   ICRNL  – stop translating CR to NL (we handle \r ourselves)
 *   ECHO   – don't echo typed characters
 *   ICANON – disable canonical (line-buffered) mode
 *   ISIG   – disable Ctrl+C / Ctrl+Z signal generation
 *   IEXTEN – disable Ctrl+V literal-next processing
 *   OPOST  – disable output post-processing (e.g. \n → \r\n translation)
 *
 * VMIN=0, VTIME=1 configures read() to return after 1/10 s even if no
 * bytes arrived, which lets us handle resize events without blocking
 * indefinitely.
 * ---------------------------------------------------------------------- */
void screen_enable_raw_mode(void)
{
    /* Save original settings so we can restore them later */
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(screen_disable_raw_mode);

    struct termios raw = E.orig_termios;

    /* Input flags */
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output flags */
    raw.c_oflag &= ~(unsigned)(OPOST);
    /* Control flags */
    raw.c_cflag |= (unsigned)(CS8);
    /* Local flags */
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);

    /* read() returns as soon as 1 byte is available, or after 100 ms */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/* -------------------------------------------------------------------------
 * screen_get_cursor_position
 *
 * Fallback method to query the cursor position using the ANSI "Device
 * Status Report" escape sequence.  The terminal responds with
 * "\x1b[<row>;<col>R".  We parse that to get the terminal dimensions.
 *
 * This is used as a fallback when ioctl(TIOCGWINSZ) fails.
 * ---------------------------------------------------------------------- */
static int screen_get_cursor_position(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    /* Ask the terminal for the cursor position */
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ <rows> ; <cols> R */
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Validate the response format */
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

/* -------------------------------------------------------------------------
 * screen_get_window_size
 *
 * Fill *rows and *cols with the current terminal dimensions.
 * Tries ioctl first; falls back to cursor-position query.
 * ---------------------------------------------------------------------- */
int screen_get_window_size(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* Fallback: move cursor to bottom-right then query its position */
        return screen_get_cursor_position(rows, cols);
    }

    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}

/* -------------------------------------------------------------------------
 * screen_append
 *
 * Append 'slen' bytes from 's' to the dynamic append buffer (*ab).
 * The buffer is grown with realloc() as needed.
 * ---------------------------------------------------------------------- */
void screen_append(char **ab, int *ab_len, int *ab_cap,
                   const char *s, int slen)
{
    if (*ab_len + slen >= *ab_cap) {
        /* Double capacity (or fit the new data, whichever is larger) */
        int new_cap = (*ab_cap) * 2;
        if (new_cap < *ab_len + slen + 1) new_cap = *ab_len + slen + 1;
        *ab = realloc(*ab, new_cap);
        if (!*ab) die("realloc");
        *ab_cap = new_cap;
    }
    memcpy(*ab + *ab_len, s, slen);
    *ab_len += slen;
}

/* Convenience macro for appending a string literal */
#define AB_APPEND(ab, ab_len, ab_cap, s) \
    screen_append((ab), (ab_len), (ab_cap), (s), (int)strlen(s))

/* -------------------------------------------------------------------------
 * screen_scroll
 *
 * Adjust the vertical (rowoff) and horizontal (coloff) scroll offsets so
 * that the cursor remains within the visible viewport.
 *
 * We also compute E.rx (render-column) here because tab expansion means
 * the cursor column in the render string differs from E.cx.
 * ---------------------------------------------------------------------- */
void screen_scroll(void)
{
    /* Compute render column for the current row */
    E.rx = 0;
    if (E.cy < E.numrows)
        E.rx = buffer_row_cx_to_rx(&E.rows[E.cy], E.cx);

    /* Vertical scroll */
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1;

    /* Horizontal scroll */
    if (E.rx < E.coloff)
        E.coloff = E.rx;
    if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;
}

/* -------------------------------------------------------------------------
 * screen_draw_rows
 *
 * Render all text rows into the append buffer.
 *
 * For rows beyond the file content we draw a '~' character (à la vim).
 * In the middle of an empty file we display a welcome banner.
 * ---------------------------------------------------------------------- */
void screen_draw_rows(char **ab, int *ab_len, int *ab_cap)
{
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;   /* Map screen row → buffer row */

        if (filerow >= E.numrows) {
            /* ---- Row is past end of file ---- */
            if (E.numrows == 0 && y == E.screenrows / 3) {
                /* Show a welcome banner when the buffer is empty */
                char welcome[80];
                int wlen = snprintf(welcome, sizeof(welcome),
                    "teded -- version %s", TEDED_VERSION);
                if (wlen > E.screencols) wlen = E.screencols;

                /* Centre the banner horizontally */
                int padding = (E.screencols - wlen) / 2;
                if (padding) {
                    AB_APPEND(ab, ab_len, ab_cap, "~");
                    padding--;
                }
                while (padding--) AB_APPEND(ab, ab_len, ab_cap, " ");
                screen_append(ab, ab_len, ab_cap, welcome, wlen);
            } else {
                AB_APPEND(ab, ab_len, ab_cap, "~");
            }
        } else {
            /* ---- Render a line of file content ---- */
            EditorRow *row = &E.rows[filerow];

            /* Apply horizontal scroll: skip the first coloff render chars */
            int len = row->rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;

            if (len > 0)
                screen_append(ab, ab_len, ab_cap,
                               row->render + E.coloff, len);
        }

        /* Erase from cursor to end of line (clears any leftover chars) */
        AB_APPEND(ab, ab_len, ab_cap, "\x1b[K");
        /* Move to the next terminal line */
        AB_APPEND(ab, ab_len, ab_cap, "\r\n");
    }
}

/* -------------------------------------------------------------------------
 * screen_draw_status_bar
 *
 * Draw a highlighted (reverse-video) status bar at the bottom showing:
 *   left  – filename (or "[No Name]") and dirty indicator
 *   right – current line / total lines
 * ---------------------------------------------------------------------- */
void screen_draw_status_bar(char **ab, int *ab_len, int *ab_cap)
{
    /* Switch to reverse video */
    AB_APPEND(ab, ab_len, ab_cap, "\x1b[7m");

    char left[128], right[128];

    int llen = snprintf(left, sizeof(left),
        " File: %.30s%s | Lines: %d | Ctrl+S Save | Ctrl+Q Quit",
        E.filename ? E.filename : "[No Name]",
        E.dirty    ? " [modified]" : "",
        E.numrows);

    int rlen = snprintf(right, sizeof(right),
        "Ln %d/%d ", E.cy + 1, E.numrows);

    /* Clamp left string to screen width */
    if (llen > E.screencols) llen = E.screencols;
    screen_append(ab, ab_len, ab_cap, left, llen);

    /* Pad between left and right sections */
    while (llen < E.screencols) {
        if (E.screencols - llen == rlen) {
            /* Exactly enough space: write the right section */
            screen_append(ab, ab_len, ab_cap, right, rlen);
            break;
        }
        AB_APPEND(ab, ab_len, ab_cap, " ");
        llen++;
    }

    /* Reset attributes and move to the next line */
    AB_APPEND(ab, ab_len, ab_cap, "\x1b[m");
    AB_APPEND(ab, ab_len, ab_cap, "\r\n");
}

/* -------------------------------------------------------------------------
 * screen_draw_status_message
 *
 * Draw the one-line status message (set via editor_set_status_message).
 * The message is displayed below the status bar.
 * ---------------------------------------------------------------------- */
void screen_draw_status_message(char **ab, int *ab_len, int *ab_cap)
{
    /* Erase the message line */
    AB_APPEND(ab, ab_len, ab_cap, "\x1b[K");

    int msglen = (int)strlen(E.status_msg);
    if (msglen > E.screencols) msglen = E.screencols;

    if (msglen)
        screen_append(ab, ab_len, ab_cap, E.status_msg, msglen);
}

/* -------------------------------------------------------------------------
 * screen_refresh
 *
 * Build and flush the complete frame:
 *   1. Scroll so cursor is in the viewport.
 *   2. Hide the cursor (prevents flicker).
 *   3. Move to home position.
 *   4. Draw all text rows.
 *   5. Draw the status bar.
 *   6. Draw the status message.
 *   7. Position the terminal cursor at E.cy/E.cx.
 *   8. Show the cursor again.
 *   9. Flush everything in one write().
 * ---------------------------------------------------------------------- */
void screen_refresh(void)
{
    screen_scroll();

    /* Initialise the append buffer */
    int  ab_cap = 4096;
    int  ab_len = 0;
    char *ab    = malloc(ab_cap);
    if (!ab) die("malloc");

    /* Hide cursor while we redraw to avoid flicker */
    AB_APPEND(&ab, &ab_len, &ab_cap, "\x1b[?25l");
    /* Move cursor to top-left */
    AB_APPEND(&ab, &ab_len, &ab_cap, "\x1b[H");

    screen_draw_rows(&ab, &ab_len, &ab_cap);
    screen_draw_status_bar(&ab, &ab_len, &ab_cap);
    screen_draw_status_message(&ab, &ab_len, &ab_cap);

    /* Position the cursor: terminal uses 1-based row/col */
    char pos[32];
    int plen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH",
                        (E.cy - E.rowoff) + 1,
                        (E.rx - E.coloff) + 1);
    screen_append(&ab, &ab_len, &ab_cap, pos, plen);

    /* Show cursor again */
    AB_APPEND(&ab, &ab_len, &ab_cap, "\x1b[?25h");

    /* Flush the entire frame in one syscall */
    write(STDOUT_FILENO, ab, ab_len);
    free(ab);
}
