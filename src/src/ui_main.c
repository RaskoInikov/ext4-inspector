#include "ui_main.h"

#include "ui_input.h"
#include "ui_panel.h"

#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t ui_resize_requested = 0;

static void ui_handle_winch(int signal_number)
{
    (void)signal_number;

    ui_resize_requested = 1;
}

static void ui_shutdown(void)
{
    endwin();
}

static int ui_initialize_ncurses(void)
{
    if (initscr() == NULL) {
        return 1;
    }

    atexit(ui_shutdown);

    setlocale(LC_ALL, "");

    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    curs_set(0);

    if (has_colors()) {
        start_color();
    }

    return 0;
}

int ui_main_run(void)
{
    struct ui_layout layout;

    int key;
    int should_exit;

    should_exit = 0;

    if (ui_initialize_ncurses() != 0) {
        return 1;
    }

    signal(SIGWINCH, ui_handle_winch);

    if (ui_panel_initialize(&layout) != 0) {
        endwin();

        fprintf(stderr,
                "Failed to initialize UI layout\n");

        return 1;
    }

    while (!should_exit) {
        if (ui_resize_requested) {
            ui_resize_requested = 0;

            if (ui_panel_resize(&layout) != 0) {
                clear();

                mvprintw(0,
                         0,
                         "Terminal too small");

                refresh();
            }
        }

        ui_panel_draw(&layout);

        key = getch();

        if (key == KEY_RESIZE) {
            if (ui_panel_resize(&layout) != 0) {
                clear();

                mvprintw(0,
                         0,
                         "Terminal too small");

                refresh();
            }

            continue;
        }

        should_exit =
            ui_input_handle(&layout, key);
    }

    ui_panel_destroy(&layout);

    return 0;
}