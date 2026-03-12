/*
 * input.c
 * Keyboard input handling for the teded terminal text editor.
 *
 * Responsibilities:
 *   - input_read_key        : read a single keypress from stdin, translating
 *                             escape sequences into our EditorKey enum values
 *   - input_move_cursor     : move the cursor in response to arrow/page keys
 *   - input_process_keypress: high-level dispatch – decide what to do for
 *                             each incoming key
 */

#include "editor.h"

/* -------------------------------------------------------------------------
 * input_read_key
 *
 * Block until a byte arrives on stdin, then return the appropriate key
 * code.  Escape sequences (e.g. arrow keys) arrive as multi-byte strings
 * beginning with '\x1b' (ESC); we read ahead to decode them.
 *
 * Returns:
 *   - A printable ASCII value (32–126) for normal characters
 *   - A CTRL_KEY value (1–26) for control combinations
 *   - One of the EditorKey enum values for special keys
 * ---------------------------------------------------------------------- */
int input_read_key(void)
{
    int   nread;
    char  c;

    /* Keep trying until we get a byte (EAGAIN can occur on some platforms) */
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1) die("read");
    }

    /* Not an escape sequence – return immediately */
    if (c != '\x1b') return (unsigned char)c;

    /* ---------- Escape sequence decode ---------- */
    char seq[3];

    /*
     * Try to read two more bytes after the ESC.
     * If they don't arrive quickly we treat the bare ESC as its own key.
     * (We simply return KEY_ARROW_UP for any unrecognised sequence rather
     * than defining a separate ESC key, keeping the mapping simple.)
     */
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
        /* CSI sequence: ESC [ <letter>  or  ESC [ <digit> ~ */
        if (seq[1] >= '0' && seq[1] <= '9') {
            /* Extended sequence – read the terminating '~' */
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return KEY_HOME;
                    case '3': return KEY_DEL;
                    case '4': return KEY_END;
                    case '5': return KEY_PAGE_UP;
                    case '6': return KEY_PAGE_DOWN;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                }
            }
        } else {
            /* Single-letter CSI sequences */
            switch (seq[1]) {
                case 'A': return KEY_ARROW_UP;
                case 'B': return KEY_ARROW_DOWN;
                case 'C': return KEY_ARROW_RIGHT;
                case 'D': return KEY_ARROW_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
    } else if (seq[0] == 'O') {
        /* SS3 sequences (some terminals use these for Home/End) */
        switch (seq[1]) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    }

    /* Unknown escape sequence – ignore */
    return '\x1b';
}

/* -------------------------------------------------------------------------
 * input_move_cursor
 *
 * Adjust E.cx / E.cy based on a directional key.
 *
 * Rules enforced:
 *   - The cursor never moves above row 0 or left of column 0.
 *   - Moving right at the end of a line wraps to the start of the next.
 *   - Moving left at the start of a line wraps to the end of the previous.
 *   - Moving down/up on Page Down/Up moves a full screen at a time.
 *   - After vertical movement the cursor is snapped to the line length.
 * ---------------------------------------------------------------------- */
void input_move_cursor(int key)
{
    /* Current row (NULL if cursor is past the last row) */
    EditorRow *row = (E.cy < E.numrows) ? &E.rows[E.cy] : NULL;

    switch (key) {
        case KEY_ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                /* Wrap to end of the previous line */
                E.cy--;
                E.cx = E.rows[E.cy].size;
            }
            break;

        case KEY_ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                /* Wrap to start of the next line */
                E.cy++;
                E.cx = 0;
            }
            break;

        case KEY_ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;

        case KEY_ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;

        case KEY_PAGE_UP:
            /* Jump cursor to the top of the visible screen */
            E.cy = E.rowoff;
            {
                int times = E.screenrows;
                while (times--) input_move_cursor(KEY_ARROW_UP);
            }
            break;

        case KEY_PAGE_DOWN:
            /* Jump cursor to the bottom of the visible screen */
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows) E.cy = E.numrows;
            {
                int times = E.screenrows;
                while (times--) input_move_cursor(KEY_ARROW_DOWN);
            }
            break;

        case KEY_HOME:
            E.cx = 0;
            break;

        case KEY_END:
            if (E.cy < E.numrows)
                E.cx = E.rows[E.cy].size;
            break;
    }

    /*
     * Snap the cursor to the end of the new row.
     * This prevents E.cx from being beyond the line length after moving
     * vertically from a longer line to a shorter one.
     */
    row = (E.cy < E.numrows) ? &E.rows[E.cy] : NULL;
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) E.cx = rowlen;
}

/* -------------------------------------------------------------------------
 * input_process_keypress
 *
 * Read one key and act on it.  This is the main dispatch table for the
 * editor.  It is called once per iteration of the main event loop.
 * ---------------------------------------------------------------------- */
void input_process_keypress(void)
{
    int c = input_read_key();

    switch (c) {

        /* ---- File operations ---- */

        case CTRL_KEY('s'):          /* Ctrl+S: save */
            file_save();
            break;

        case CTRL_KEY('q'):          /* Ctrl+Q: quit */
            if (E.dirty) {
                /*
                 * Warn the user if there are unsaved changes.
                 * A second Ctrl+Q (handled by re-entry) would call exit()
                 * because we reset dirty here.  For simplicity we just show
                 * a message and require another Ctrl+Q.
                 */
                editor_set_status_message(
                    "Unsaved changes! Press Ctrl+Q again to quit.");
                E.dirty = 0;  /* Reset so the next Ctrl+Q exits cleanly */
                return;
            }
            /* Clear the screen and exit */
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H",  3);
            exit(EXIT_SUCCESS);
            break;

        /* ---- Whitespace / structural editing ---- */

        case '\r':                   /* Enter key */
            buffer_insert_newline();
            break;

        case KEY_BACKSPACE:          /* Backspace (127 on most terminals) */
        case CTRL_KEY('h'):          /* Ctrl+H: legacy backspace          */
        case KEY_DEL:                /* Delete key: delete char to right   */
            if (c == KEY_DEL) {
                /* Move right then backspace to simulate forward delete */
                input_move_cursor(KEY_ARROW_RIGHT);
            }
            buffer_delete_char();
            break;

        /* ---- Cursor movement ---- */

        case KEY_ARROW_UP:
        case KEY_ARROW_DOWN:
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN:
        case KEY_HOME:
        case KEY_END:
            input_move_cursor(c);
            break;

        /* ---- Ignored control characters ---- */

        case CTRL_KEY('l'):          /* Traditional "refresh" – we always refresh */
        case '\x1b':                 /* Bare ESC – do nothing */
            break;

        /* ---- Printable characters ---- */

        default:
            /* Insert any printable character (and tabs) into the buffer */
            if (!iscntrl(c) || c == '\t')
                buffer_insert_char(c);
            break;
    }
}
