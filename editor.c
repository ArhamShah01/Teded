/*
 * editor.c
 * Entry point and global state for the teded terminal text editor.
 *
 * Responsibilities:
 *   - Define the global EditorConfig instance (E)
 *   - Initialise editor state
 *   - Run the main event loop
 *   - Provide the die() helper used by all modules
 */

#include <stdarg.h>
#include <time.h>
#include "editor.h"

/* -------------------------------------------------------------------------
 * Global editor state (declared extern in editor.h)
 * ---------------------------------------------------------------------- */
EditorConfig E;

/* -------------------------------------------------------------------------
 * die – print an error message, restore the terminal, and exit.
 *
 * Called whenever an unrecoverable error occurs (e.g. a system-call
 * failure).  We disable raw mode first so the terminal is left in a
 * usable state for the shell.
 * ---------------------------------------------------------------------- */
void die(const char *msg)
{
    /* Clear the screen before printing the error */
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",  3);

    screen_disable_raw_mode();
    perror(msg);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------
 * editor_set_status_message – printf-style helper that writes a temporary
 * message into E.status_msg.  The message is displayed in the status bar
 * until the next keypress.
 * ---------------------------------------------------------------------- */
void editor_set_status_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
}

/* -------------------------------------------------------------------------
 * editor_init – zero-initialise the EditorConfig and query the terminal
 * for its current dimensions.
 * ---------------------------------------------------------------------- */
void editor_init(void)
{
    E.cx       = 0;
    E.cy       = 0;
    E.rx       = 0;
    E.rowoff   = 0;
    E.coloff   = 0;
    E.numrows  = 0;
    E.rows     = NULL;
    E.dirty    = 0;
    E.filename = NULL;
    E.status_msg[0] = '\0';

    /* Ask the OS for the terminal window size */
    if (screen_get_window_size(&E.screenrows, &E.screencols) == -1)
        die("screen_get_window_size");

    /* Reserve rows at the bottom for the status bar and message line */
    E.screenrows -= STATUS_BAR_ROWS;
}

/* -------------------------------------------------------------------------
 * main – program entry point.
 *
 * 1. Enable raw mode (must happen before any output).
 * 2. Initialise editor state.
 * 3. Optionally load a file given on the command line.
 * 4. Show a welcome / help message in the status bar.
 * 5. Run the main event loop: refresh screen → read keypress → repeat.
 * ---------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    screen_enable_raw_mode();
    editor_init();

    if (argc >= 2) {
        /* A filename was provided – open (or create) it */
        file_open(argv[1]);
    }

    editor_set_status_message(
        "Ctrl+S Save | Ctrl+Q Quit | Arrow keys move cursor");

    /* ---- Main event loop ---- */
    while (1) {
        screen_refresh();          /* Redraw the entire screen          */
        input_process_keypress();  /* Block until a key, then handle it */
    }

    return 0; /* Unreachable – Ctrl+Q calls exit() */
}
