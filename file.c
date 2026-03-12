/*
 * file.c
 * File I/O for the teded terminal text editor.
 *
 * We define _POSIX_C_SOURCE to expose POSIX extensions (strdup, getline)
 * without leaving strict C99 mode for the rest of the project.
 */
#define _POSIX_C_SOURCE 200809L

/*
 * file.c
 * File I/O for the teded terminal text editor.
 *
 * Responsibilities:
 *   - file_open  : read a file from disk into the row buffer (or create it)
 *   - file_save  : serialise the row buffer back to disk
 *   - file_rows_to_string : helper that joins all rows into one flat string
 */

#include "editor.h"

/* -------------------------------------------------------------------------
 * file_rows_to_string
 *
 * Flatten E.rows[] into a single heap-allocated string, joining rows with
 * newline characters.  The caller receives the total byte count via *buflen
 * and is responsible for free()ing the returned pointer.
 * ---------------------------------------------------------------------- */
char *file_rows_to_string(int *buflen)
{
    int total_len = 0;
    int j;

    /* Calculate total length: each row contributes its size + 1 for '\n' */
    for (j = 0; j < E.numrows; j++)
        total_len += E.rows[j].size + 1;

    *buflen = total_len;

    char *buf = malloc(total_len + 1);
    if (!buf) die("malloc");

    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.rows[j].chars, E.rows[j].size);
        p += E.rows[j].size;
        *p = '\n';
        p++;
    }
    /* NUL-terminate for safety, though we use the length for I/O */
    *p = '\0';

    return buf;
}

/* -------------------------------------------------------------------------
 * file_open
 *
 * Open the file at 'filename' and load its contents into the row buffer.
 * If the file does not exist we still store the filename so that file_save()
 * can create it; the buffer simply starts empty.
 * ---------------------------------------------------------------------- */
void file_open(const char *filename)
{
    /* Keep a copy of the filename in the editor state */
    free(E.filename);
    E.filename = strdup(filename);
    if (!E.filename) die("strdup");

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* File does not exist – start with an empty buffer */
        editor_set_status_message("New file: %s", filename);
        return;
    }

    char  *line    = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    /*
     * Read the file line by line using getline().
     * getline() allocates (or grows) the buffer pointed to by 'line'
     * and returns the number of bytes read including the newline, or -1
     * at EOF / error.
     */
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        /* Strip trailing newline and carriage-return characters */
        while (linelen > 0 &&
               (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;

        buffer_insert_row(E.numrows, line, (size_t)linelen);
    }

    free(line);
    fclose(fp);

    /* A freshly loaded file has no unsaved changes */
    E.dirty = 0;
}

/* -------------------------------------------------------------------------
 * file_save
 *
 * Write the current buffer contents to disk.
 * Uses O_TRUNC so the file is replaced rather than partially overwritten.
 * Reports success / failure in the status bar.
 * ---------------------------------------------------------------------- */
void file_save(void)
{
    /* Cannot save without a filename */
    if (E.filename == NULL) {
        editor_set_status_message("No filename set – cannot save.");
        return;
    }

    int len;
    char *buf = file_rows_to_string(&len);

    /*
     * Open the file for writing.
     *   O_RDWR  – read/write access (needed to truncate)
     *   O_CREAT – create if it does not exist
     *   O_TRUNC – truncate to zero length before writing
     *   0644    – owner rw, group r, other r
     */
    int fd = open(E.filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        editor_set_status_message("Save failed: %s", E.filename);
        free(buf);
        return;
    }

    /* Write the full buffer in one call */
    if (write(fd, buf, len) != len) {
        editor_set_status_message("Save error: write failed for %s",
                                  E.filename);
        close(fd);
        free(buf);
        return;
    }

    close(fd);
    free(buf);

    E.dirty = 0;
    editor_set_status_message("Saved %d bytes to %s", len, E.filename);
}
