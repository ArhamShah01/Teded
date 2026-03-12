/*
 * buffer.c
 * Dynamic text-buffer management for the teded editor.
 *
 * Each line of text is stored as an EditorRow.  This module owns:
 *   - Rendering rows (tab expansion)
 *   - Inserting / deleting rows
 *   - Inserting / deleting characters within a row
 *   - Higher-level operations: insert char at cursor, delete char at cursor,
 *     and insert a newline (splitting the current row).
 */

#include "editor.h"

/* -------------------------------------------------------------------------
 * buffer_update_render
 *
 * Build the 'render' string for a row by copying its 'chars' string and
 * expanding every tab character into TAB_SIZE spaces.  The render string
 * is what gets drawn to the terminal.
 * ---------------------------------------------------------------------- */
void buffer_update_render(EditorRow *row)
{
    int tabs = 0;
    int j;

    /* Count tabs so we can allocate the right amount of memory */
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    /* Each tab expands to at most TAB_SIZE characters */
    row->render = malloc(row->size + tabs * (TAB_SIZE - 1) + 1);
    if (!row->render) die("malloc");

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            /* Expand tab: advance to the next tab-stop */
            row->render[idx++] = ' ';
            while (idx % TAB_SIZE != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

/* -------------------------------------------------------------------------
 * buffer_row_cx_to_rx
 *
 * Convert a buffer column index (cx) to a render column index (rx).
 * The two differ when there are tabs before the cursor position.
 * ---------------------------------------------------------------------- */
int buffer_row_cx_to_rx(EditorRow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
        rx++;
    }
    return rx;
}

/* -------------------------------------------------------------------------
 * buffer_insert_row
 *
 * Insert a new EditorRow at position 'at' (0-based) in E.rows[].
 * The row is initialised with the given string 's' of length 'len'.
 * ---------------------------------------------------------------------- */
void buffer_insert_row(int at, const char *s, size_t len)
{
    if (at < 0 || at > E.numrows) return;

    /* Grow the rows array by one slot */
    E.rows = realloc(E.rows, sizeof(EditorRow) * (E.numrows + 1));
    if (!E.rows) die("realloc");

    /* Shift rows below 'at' down by one position */
    memmove(&E.rows[at + 1], &E.rows[at],
            sizeof(EditorRow) * (E.numrows - at));

    /* Initialise the new row */
    E.rows[at].size  = (int)len;
    E.rows[at].chars = malloc(len + 1);
    if (!E.rows[at].chars) die("malloc");
    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';

    E.rows[at].render = NULL;
    E.rows[at].rsize  = 0;
    buffer_update_render(&E.rows[at]);

    E.numrows++;
    E.dirty++;
}

/* -------------------------------------------------------------------------
 * buffer_free_row – release memory owned by a single EditorRow.
 * ---------------------------------------------------------------------- */
void buffer_free_row(EditorRow *row)
{
    free(row->render);
    free(row->chars);
}

/* -------------------------------------------------------------------------
 * buffer_delete_row – remove the row at index 'at' and free its memory.
 * ---------------------------------------------------------------------- */
void buffer_delete_row(int at)
{
    if (at < 0 || at >= E.numrows) return;

    buffer_free_row(&E.rows[at]);

    /* Shift rows above 'at' up by one position */
    memmove(&E.rows[at], &E.rows[at + 1],
            sizeof(EditorRow) * (E.numrows - at - 1));

    E.numrows--;
    E.dirty++;
}

/* -------------------------------------------------------------------------
 * buffer_row_insert_char
 *
 * Insert character 'c' at column 'at' within a row.
 * 'at' may equal row->size (append at end).
 * ---------------------------------------------------------------------- */
void buffer_row_insert_char(EditorRow *row, int at, int c)
{
    if (at < 0 || at > row->size) at = row->size;

    /* Grow chars by one byte (+1 for the new char, +1 for NUL) */
    row->chars = realloc(row->chars, row->size + 2);
    if (!row->chars) die("realloc");

    /* Shift characters after 'at' right */
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = (char)c;
    row->size++;

    buffer_update_render(row);
    E.dirty++;
}

/* -------------------------------------------------------------------------
 * buffer_row_delete_char
 *
 * Delete the character at column 'at' within a row.
 * ---------------------------------------------------------------------- */
void buffer_row_delete_char(EditorRow *row, int at)
{
    if (at < 0 || at >= row->size) return;

    /* Shift characters after 'at' left (NUL included) */
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;

    buffer_update_render(row);
    E.dirty++;
}

/* -------------------------------------------------------------------------
 * buffer_row_append_string
 *
 * Append a string 's' of length 'len' to the end of a row.
 * Used when joining two rows (backspace at start of a row).
 * ---------------------------------------------------------------------- */
void buffer_row_append_string(EditorRow *row, const char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + (int)len + 1);
    if (!row->chars) die("realloc");

    memcpy(&row->chars[row->size], s, len);
    row->size += (int)len;
    row->chars[row->size] = '\0';

    buffer_update_render(row);
    E.dirty++;
}

/* -------------------------------------------------------------------------
 * buffer_insert_char
 *
 * High-level: insert character 'c' at the current cursor position (E.cx,
 * E.cy).  Creates the first row if the buffer is empty.
 * ---------------------------------------------------------------------- */
void buffer_insert_char(int c)
{
    /* If cursor is past the last row, append a blank row first */
    if (E.cy == E.numrows)
        buffer_insert_row(E.numrows, "", 0);

    buffer_row_insert_char(&E.rows[E.cy], E.cx, c);
    E.cx++;  /* Advance cursor past the newly inserted character */
}

/* -------------------------------------------------------------------------
 * buffer_delete_char
 *
 * High-level: delete the character immediately to the left of the cursor.
 * If the cursor is at the start of a row, merge that row with the one above.
 * ---------------------------------------------------------------------- */
void buffer_delete_char(void)
{
    /* Nothing to delete at the very beginning of the file */
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    if (E.cx > 0) {
        /* Normal case: delete the character to the left */
        buffer_row_delete_char(&E.rows[E.cy], E.cx - 1);
        E.cx--;
    } else {
        /* Cursor is at column 0: merge this row with the row above */
        E.cx = E.rows[E.cy - 1].size;  /* New cursor is at end of prev row */
        buffer_row_append_string(&E.rows[E.cy - 1],
                                 E.rows[E.cy].chars,
                                 E.rows[E.cy].size);
        buffer_delete_row(E.cy);
        E.cy--;
    }
}

/* -------------------------------------------------------------------------
 * buffer_insert_newline
 *
 * High-level: split the current row at E.cx, creating a new row below
 * with the text to the right of the cursor.
 * ---------------------------------------------------------------------- */
void buffer_insert_newline(void)
{
    if (E.cx == 0) {
        /* Cursor is at the beginning of the row: insert blank row above */
        buffer_insert_row(E.cy, "", 0);
    } else {
        /* Split the current row at cx */
        EditorRow *row = &E.rows[E.cy];
        /* New row gets everything from cx onwards */
        buffer_insert_row(E.cy + 1,
                          &row->chars[E.cx],
                          row->size - E.cx);
        /* Truncate the current row at cx */
        row = &E.rows[E.cy]; /* Re-fetch: realloc may have moved memory */
        row->size = E.cx;
        row->chars[row->size] = '\0';
        buffer_update_render(row);
    }

    /* Move cursor to the start of the new row */
    E.cy++;
    E.cx = 0;
}
