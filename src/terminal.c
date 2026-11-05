#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

/**
 * terminal.c – Integrated pseudo-terminal (PTY) and ANSI emulator for cgui
 */

#include "terminal.h"
#include "app.h"
#include "theme.h"
#include "vk_draw.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#  include <util.h>
#else
#  include <pty.h>
#endif

#include <SDL3/SDL.h>

/* Standard ANSI 16 colors (OneDark / Alacritty inspired) */
static const uint8_t s_ansi_palette[16][4] = {
    { 30,  34,  42,  255 },  /* 0: Black */
    { 224, 108, 117, 255 },  /* 1: Red */
    { 152, 195, 121, 255 },  /* 2: Green */
    { 229, 192, 123, 255 },  /* 3: Yellow */
    { 97,  175, 239, 255 },  /* 4: Blue */
    { 198, 120, 221, 255 },  /* 5: Magenta */
    { 86,  182, 194, 255 },  /* 6: Cyan */
    { 171, 178, 191, 255 },  /* 7: White */
    { 92,  99,  112, 255 },  /* 8: Bright Black (Grey) */
    { 224, 108, 117, 255 },  /* 9: Bright Red */
    { 152, 195, 121, 255 },  /* 10: Bright Green */
    { 229, 192, 123, 255 },  /* 11: Bright Yellow */
    { 97,  175, 239, 255 },  /* 12: Bright Blue */
    { 198, 120, 221, 255 },  /* 13: Bright Magenta */
    { 86,  182, 194, 255 },  /* 14: Bright Cyan */
    { 255, 255, 255, 255 },  /* 15: Bright White */
};

static void get_256_color(int idx, uint8_t out[4])
{
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    if (idx < 16) {
        memcpy(out, s_ansi_palette[idx], 4);
        return;
    }
    if (idx < 232) {
        /* 6x6x6 color cube */
        int c = idx - 16;
        int b = c % 6; c /= 6;
        int g = c % 6; c /= 6;
        int r = c % 6;
        out[0] = r ? (uint8_t)(r * 40 + 55) : 0;
        out[1] = g ? (uint8_t)(g * 40 + 55) : 0;
        out[2] = b ? (uint8_t)(b * 40 + 55) : 0;
        out[3] = 255;
        return;
    }
    /* 24-step grayscale ramp */
    int gray = (idx - 232) * 10 + 8;
    out[0] = (uint8_t)gray;
    out[1] = (uint8_t)gray;
    out[2] = (uint8_t)gray;
    out[3] = 255;
}

static void term_cell_reset(Terminal *term, TermCell *cell)
{
    cell->c[0] = ' ';
    cell->c[1] = 0;
    memcpy(cell->fg, term->default_fg, 4);
    memcpy(cell->bg, term->default_bg, 4);
    cell->flags = 0;
}

static void term_sync_theme(Terminal *term)
{
    const Theme *th = theme_current();
    if (th) {
        memcpy(term->default_fg, th->fg, 4);
        memcpy(term->default_bg, th->bg, 4);
        memcpy(term->cursor_color, th->caret, 4);
        memcpy(term->header_bg, th->tab_strip_bg, 4);
        memcpy(term->header_border, th->tab_edge, 4);
        memcpy(term->header_fg, th->tab_text_active, 4);
    } else {
        uint8_t dfg[4] = { 220, 224, 230, 255 };
        uint8_t dbg[4] = { 24, 26, 31, 255 };
        uint8_t cur[4] = { 97, 175, 239, 255 };
        uint8_t hbg[4] = { 30, 33, 39, 255 };
        uint8_t hed[4] = { 45, 50, 60, 255 };
        memcpy(term->default_fg, dfg, 4);
        memcpy(term->default_bg, dbg, 4);
        memcpy(term->cursor_color, cur, 4);
        memcpy(term->header_bg, hbg, 4);
        memcpy(term->header_border, hed, 4);
        memcpy(term->header_fg, dfg, 4);
    }
    memcpy(term->cur_fg, term->default_fg, 4);
    memcpy(term->cur_bg, term->default_bg, 4);
    term->cur_flags = 0;
}

static void term_scroll_up(Terminal *term)
{
    if (term->rows <= 0 || term->cols <= 0) return;

    /* Push topmost row into circular scrollback */
    int target_idx = (term->scrollback_head + term->scrollback_count) % TERM_SCROLLBACK_CAP;
    if (term->scrollback_count < TERM_SCROLLBACK_CAP) {
        term->scrollback_count++;
    } else {
        term->scrollback_head = (term->scrollback_head + 1) % TERM_SCROLLBACK_CAP;
    }
    memcpy(&term->scrollback[target_idx * term->cols], &term->grid[0],
           (size_t)term->cols * sizeof(TermCell));

    /* Shift lines up by 1 */
    if (term->rows > 1) {
        memmove(&term->grid[0], &term->grid[term->cols],
                (size_t)(term->rows - 1) * term->cols * sizeof(TermCell));
    }

    /* Blank bottom row */
    TermCell *bottom_row = &term->grid[(term->rows - 1) * term->cols];
    for (int c = 0; c < term->cols; ++c) {
        term_cell_reset(term, &bottom_row[c]);
        memcpy(bottom_row[c].bg, term->cur_bg, 4);
    }
}

static void term_put_char(Terminal *term, const char *utf8_bytes, size_t nbytes)
{
    if (term->cursor_x >= term->cols) {
        term->cursor_x = 0;
        term->cursor_y++;
    }
    if (term->cursor_y >= term->rows) {
        term_scroll_up(term);
        term->cursor_y = term->rows - 1;
    }

    int idx = term->cursor_y * term->cols + term->cursor_x;
    TermCell *cell = &term->grid[idx];
    size_t copy_len = nbytes < sizeof(cell->c) - 1 ? nbytes : sizeof(cell->c) - 1;
    memcpy(cell->c, utf8_bytes, copy_len);
    cell->c[copy_len] = 0;

    memcpy(cell->fg, term->cur_fg, 4);
    memcpy(cell->bg, term->cur_bg, 4);
    cell->flags = term->cur_flags;

    term->cursor_x++;
}

static void term_handle_sgr(Terminal *term)
{
    int count = term->param_count > 0 ? term->param_count : 1;
    for (int i = 0; i < count; ++i) {
        int p = term->params[i];
        if (p == 0) {
            memcpy(term->cur_fg, term->default_fg, 4);
            memcpy(term->cur_bg, term->default_bg, 4);
            term->cur_flags = 0;
        } else if (p == 1) {
            term->cur_flags |= TERM_FLAG_BOLD;
        } else if (p == 2) {
            term->cur_flags |= TERM_FLAG_DIM;
        } else if (p == 4) {
            term->cur_flags |= TERM_FLAG_UNDERLINE;
        } else if (p == 7) {
            term->cur_flags |= TERM_FLAG_INVERSE;
        } else if (p == 22) {
            term->cur_flags &= ~(TERM_FLAG_BOLD | TERM_FLAG_DIM);
        } else if (p == 24) {
            term->cur_flags &= ~TERM_FLAG_UNDERLINE;
        } else if (p == 27) {
            term->cur_flags &= ~TERM_FLAG_INVERSE;
        } else if (p >= 30 && p <= 37) {
            memcpy(term->cur_fg, s_ansi_palette[p - 30], 4);
        } else if (p == 38) {
            /* Extended FG color */
            if (i + 2 < count && term->params[i + 1] == 5) {
                get_256_color(term->params[i + 2], term->cur_fg);
                i += 2;
            } else if (i + 4 < count && term->params[i + 1] == 2) {
                term->cur_fg[0] = (uint8_t)term->params[i + 2];
                term->cur_fg[1] = (uint8_t)term->params[i + 3];
                term->cur_fg[2] = (uint8_t)term->params[i + 4];
                term->cur_fg[3] = 255;
                i += 4;
            }
        } else if (p == 39) {
            memcpy(term->cur_fg, term->default_fg, 4);
        } else if (p >= 40 && p <= 47) {
            memcpy(term->cur_bg, s_ansi_palette[p - 40], 4);
        } else if (p == 48) {
            /* Extended BG color */
            if (i + 2 < count && term->params[i + 1] == 5) {
                get_256_color(term->params[i + 2], term->cur_bg);
                i += 2;
            } else if (i + 4 < count && term->params[i + 1] == 2) {
                term->cur_bg[0] = (uint8_t)term->params[i + 2];
                term->cur_bg[1] = (uint8_t)term->params[i + 3];
                term->cur_bg[2] = (uint8_t)term->params[i + 4];
                term->cur_bg[3] = 255;
                i += 4;
            }
        } else if (p == 49) {
            memcpy(term->cur_bg, term->default_bg, 4);
        } else if (p >= 90 && p <= 97) {
            memcpy(term->cur_fg, s_ansi_palette[p - 90 + 8], 4);
        } else if (p >= 100 && p <= 107) {
            memcpy(term->cur_bg, s_ansi_palette[p - 100 + 8], 4);
        }
    }
}

static void term_handle_csi(Terminal *term, char cmd)
{
    int p0 = term->param_count > 0 ? term->params[0] : 0;
    int p1 = term->param_count > 1 ? term->params[1] : 0;

    switch (cmd) {
    case 'H':
    case 'f': {
        int row = (p0 > 0 ? p0 - 1 : 0);
        int col = (p1 > 0 ? p1 - 1 : 0);
        if (row >= term->rows) row = term->rows - 1;
        if (col >= term->cols) col = term->cols - 1;
        term->cursor_y = row;
        term->cursor_x = col;
        break;
    }
    case 'A': /* Cursor up */
        term->cursor_y -= (p0 > 0 ? p0 : 1);
        if (term->cursor_y < 0) term->cursor_y = 0;
        break;
    case 'B': /* Cursor down */
        term->cursor_y += (p0 > 0 ? p0 : 1);
        if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;
        break;
    case 'C': /* Cursor forward */
        term->cursor_x += (p0 > 0 ? p0 : 1);
        if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
        break;
    case 'D': /* Cursor backward */
        term->cursor_x -= (p0 > 0 ? p0 : 1);
        if (term->cursor_x < 0) term->cursor_x = 0;
        break;
    case 'G': /* Cursor horizontal absolute */
        term->cursor_x = (p0 > 0 ? p0 - 1 : 0);
        if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
        break;
    case 'd': /* Line position absolute */
        term->cursor_y = (p0 > 0 ? p0 - 1 : 0);
        if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;
        break;
    case 'J': /* Erase in display */
        if (p0 == 0) {
            /* Erase below */
            int start = term->cursor_y * term->cols + term->cursor_x;
            for (int i = start; i < term->cols * term->rows; ++i)
                term_cell_reset(term, &term->grid[i]);
        } else if (p0 == 1) {
            /* Erase above */
            int end = term->cursor_y * term->cols + term->cursor_x;
            for (int i = 0; i <= end; ++i)
                term_cell_reset(term, &term->grid[i]);
        } else if (p0 == 2 || p0 == 3) {
            /* Erase all */
            for (int i = 0; i < term->cols * term->rows; ++i)
                term_cell_reset(term, &term->grid[i]);
            term->cursor_x = 0;
            term->cursor_y = 0;
            if (p0 == 3) {
                term->scrollback_count = 0;
                term->scrollback_head = 0;
                term->scroll_offset = 0;
            }
        }
        break;
    case 'K': /* Erase in line */
        if (p0 == 0) {
            /* Erase right */
            for (int c = term->cursor_x; c < term->cols; ++c)
                term_cell_reset(term, &term->grid[term->cursor_y * term->cols + c]);
        } else if (p0 == 1) {
            /* Erase left */
            for (int c = 0; c <= term->cursor_x && c < term->cols; ++c)
                term_cell_reset(term, &term->grid[term->cursor_y * term->cols + c]);
        } else if (p0 == 2) {
            /* Erase entire line */
            for (int c = 0; c < term->cols; ++c)
                term_cell_reset(term, &term->grid[term->cursor_y * term->cols + c]);
        }
        break;
    case 'm': /* SGR */
        term_handle_sgr(term);
        break;
    case 'h': /* Set mode */
        if (term->private_prefix == '?' && p0 == 25)
            term->cursor_visible = true;
        break;
    case 'l': /* Reset mode */
        if (term->private_prefix == '?' && p0 == 25)
            term->cursor_visible = false;
        break;
    default:
        break;
    }
}

static void term_parse_bytes(Terminal *term, const char *buf, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char b = (unsigned char)buf[i];

        if (term->parse_state == TERM_STATE_NORMAL) {
            if (b == 0x1b) {
                term->parse_state = TERM_STATE_ESC;
                i++;
                continue;
            } else if (b == '\r') {
                term->cursor_x = 0;
                i++;
                continue;
            } else if (b == '\n') {
                term->cursor_y++;
                if (term->cursor_y >= term->rows) {
                    term_scroll_up(term);
                    term->cursor_y = term->rows - 1;
                }
                i++;
                continue;
            } else if (b == '\b') {
                if (term->cursor_x > 0) term->cursor_x--;
                i++;
                continue;
            } else if (b == '\t') {
                int next_tab = (term->cursor_x + 8) & ~7;
                if (next_tab >= term->cols) next_tab = term->cols - 1;
                term->cursor_x = next_tab;
                i++;
                continue;
            } else if (b == 0x07) {
                /* Bell - ignore */
                i++;
                continue;
            }

            /* UTF-8 Character decode */
            size_t seq_len = 1;
            if ((b & 0x80) == 0) {
                seq_len = 1;
            } else if ((b & 0xE0) == 0xC0) {
                seq_len = 2;
            } else if ((b & 0xF0) == 0xE0) {
                seq_len = 3;
            } else if ((b & 0xF8) == 0xF0) {
                seq_len = 4;
            }
            if (i + seq_len <= len) {
                term_put_char(term, &buf[i], seq_len);
                i += seq_len;
            } else {
                /* Incomplete UTF-8 sequence at end of chunk: buffer in utf8_tail */
                size_t rem = len - i;
                if (rem < sizeof(term->utf8_tail)) {
                    memcpy(term->utf8_tail, &buf[i], rem);
                    term->utf8_tail_len = rem;
                }
                break;
            }
        } else if (term->parse_state == TERM_STATE_ESC) {
            if (b == '[') {
                term->parse_state = TERM_STATE_CSI;
                memset(term->params, 0, sizeof(term->params));
                term->param_count = 0;
                term->private_prefix = 0;
                i++;
            } else if (b == ']') {
                term->parse_state = TERM_STATE_OSC;
                i++;
            } else if (b == '(' || b == ')') {
                term->parse_state = TERM_STATE_CHARSET;
                i++;
            } else if (b == '7') {
                term->saved_cursor_x = term->cursor_x;
                term->saved_cursor_y = term->cursor_y;
                term->parse_state = TERM_STATE_NORMAL;
                i++;
            } else if (b == '8') {
                term->cursor_x = term->saved_cursor_x;
                term->cursor_y = term->saved_cursor_y;
                term->parse_state = TERM_STATE_NORMAL;
                i++;
            } else if (b == 'c') {
                terminal_clear(term);
                term->parse_state = TERM_STATE_NORMAL;
                i++;
            } else {
                term->parse_state = TERM_STATE_NORMAL;
                i++;
            }
        } else if (term->parse_state == TERM_STATE_CSI) {
            if (b == '?' || b == '>' || b == '=') {
                term->private_prefix = (char)b;
                i++;
            } else if (b >= '0' && b <= '9') {
                if (term->param_count == 0) term->param_count = 1;
                int cur_val = term->params[term->param_count - 1];
                term->params[term->param_count - 1] = cur_val * 10 + (b - '0');
                i++;
            } else if (b == ';') {
                if (term->param_count < 16) term->param_count++;
                i++;
            } else {
                /* Command terminator */
                if (term->param_count == 0 && (b == 'm' || b == 'H' || b == 'J' || b == 'K')) {
                    term->param_count = 1;
                    term->params[0] = 0;
                }
                term_handle_csi(term, (char)b);
                term->parse_state = TERM_STATE_NORMAL;
                i++;
            }
        } else if (term->parse_state == TERM_STATE_OSC) {
            /* Operating system command (skip until BEL 0x07 or ESC \) */
            if (b == 0x07 || (b == '\\' && i > 0 && (unsigned char)buf[i - 1] == 0x1b)) {
                term->parse_state = TERM_STATE_NORMAL;
            }
            i++;
        } else if (term->parse_state == TERM_STATE_CHARSET) {
            term->parse_state = TERM_STATE_NORMAL;
            i++;
        }
    }
}

static bool term_spawn_process(Terminal *term)
{
    struct winsize ws = {
        .ws_row = (unsigned short)(term->rows > 0 ? term->rows : 24),
        .ws_col = (unsigned short)(term->cols > 0 ? term->cols : 80),
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };

    pid_t pid = forkpty(&term->master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        perror("forkpty failed");
        term->alive = false;
        return false;
    }

    if (pid == 0) {
        /* Child process in child pseudo-terminal */
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        if (term->working_dir[0]) {
            chdir(term->working_dir);
        }

        const char *shell = getenv("SHELL");
        if (!shell || !shell[0]) shell = "/bin/bash";

        execlp(shell, shell, NULL);
        /* Fallback if preferred shell fails */
        execlp("/bin/sh", "/bin/sh", NULL);
        _exit(127);
    }

    term->pid = pid;
    term->alive = true;

    /* Set non-blocking on master fd so frame loop never stalls */
    int flags = fcntl(term->master_fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(term->master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    return true;
}

bool terminal_init(Terminal *term, const char *working_dir)
{
    if (!term) return false;
    memset(term, 0, sizeof(*term));

    term->cols = 80;
    term->rows = 24;
    term->cursor_visible = true;
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->scroll_offset = 0;
    term->last_blink_ms = SDL_GetTicks();
    term->blink_state = true;
    term->focused = true;

    term_sync_theme(term);

    if (working_dir && working_dir[0]) {
        snprintf(term->working_dir, sizeof(term->working_dir), "%s", working_dir);
    } else {
        if (!getcwd(term->working_dir, sizeof(term->working_dir)))
            term->working_dir[0] = 0;
    }

    const char *shell_env = getenv("SHELL");
    const char *base = shell_env ? strrchr(shell_env, '/') : NULL;
    snprintf(term->shell_name, sizeof(term->shell_name), "%s", base ? base + 1 : (shell_env ? shell_env : "bash"));
    snprintf(term->title, sizeof(term->title), "TERMINAL (%s)", term->shell_name);

    /* Allocate grid and scrollback */
    term->grid = calloc((size_t)TERM_MAX_COLS * TERM_MAX_ROWS, sizeof(TermCell));
    term->scrollback = calloc((size_t)TERM_SCROLLBACK_CAP * TERM_MAX_COLS, sizeof(TermCell));
    if (!term->grid || !term->scrollback) {
        free(term->grid);
        free(term->scrollback);
        return false;
    }

    for (int i = 0; i < TERM_MAX_COLS * TERM_MAX_ROWS; ++i)
        term_cell_reset(term, &term->grid[i]);

    return term_spawn_process(term);
}

void terminal_destroy(Terminal *term)
{
    if (!term) return;
    if (term->alive && term->pid > 0) {
        kill(term->pid, SIGHUP);
        close(term->master_fd);
        int status;
        waitpid(term->pid, &status, WNOHANG);
    }
    free(term->grid);
    free(term->scrollback);
    term->grid = NULL;
    term->scrollback = NULL;
    term->alive = false;
}

void terminal_restart(Terminal *term)
{
    if (!term) return;
    if (term->alive && term->pid > 0) {
        kill(term->pid, SIGHUP);
        close(term->master_fd);
        int status;
        waitpid(term->pid, &status, WNOHANG);
        term->alive = false;
    }

    terminal_clear(term);
    term_spawn_process(term);
}

void terminal_clear(Terminal *term)
{
    if (!term || !term->grid) return;
    for (int i = 0; i < term->cols * term->rows; ++i)
        term_cell_reset(term, &term->grid[i]);
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->scroll_offset = 0;
    term->scrollback_count = 0;
    term->scrollback_head = 0;
}

void terminal_resize_grid(Terminal *term, int new_cols, int new_rows)
{
    if (!term) return;
    if (new_cols < 10) new_cols = 10;
    if (new_rows < 3) new_rows = 3;
    if (new_cols > TERM_MAX_COLS) new_cols = TERM_MAX_COLS;
    if (new_rows > TERM_MAX_ROWS) new_rows = TERM_MAX_ROWS;

    if (new_cols == term->cols && new_rows == term->rows)
        return;

    term->cols = new_cols;
    term->rows = new_rows;

    if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
    if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;

    if (term->alive && term->master_fd >= 0) {
        struct winsize ws = {
            .ws_row = (unsigned short)term->rows,
            .ws_col = (unsigned short)term->cols,
            .ws_xpixel = 0,
            .ws_ypixel = 0,
        };
        ioctl(term->master_fd, TIOCSWINSZ, &ws);
    }
}

void terminal_update(Terminal *term)
{
    if (!term || !term->alive || term->master_fd < 0) return;

    /* Check child exit status */
    int status;
    pid_t res = waitpid(term->pid, &status, WNOHANG);
    if (res == term->pid) {
        term->alive = false;
        close(term->master_fd);
        term->master_fd = -1;
        const char *msg = "\r\n[Process terminated]\r\n";
        term_parse_bytes(term, msg, strlen(msg));
        return;
    }

    char raw_buf[4096];
    char buf[4104];
    ssize_t n = 0;
    while ((n = read(term->master_fd, raw_buf, sizeof(raw_buf))) > 0) {
        size_t total_len = 0;
        if (term->utf8_tail_len > 0) {
            memcpy(buf, term->utf8_tail, term->utf8_tail_len);
            total_len = term->utf8_tail_len;
            term->utf8_tail_len = 0;
        }
        memcpy(&buf[total_len], raw_buf, (size_t)n);
        total_len += (size_t)n;
        term_parse_bytes(term, buf, total_len);
        /* If user is actively receiving command output, snap back down */
        if (term->scroll_offset > 0)
            term->scroll_offset = 0;
    }
}

void terminal_send_input(Terminal *term, const char *data, size_t len)
{
    if (!term || !term->alive || term->master_fd < 0 || !data || len == 0) return;
    write(term->master_fd, data, len);
    term->scroll_offset = 0;
}

void terminal_draw(Terminal *term, VkContext *ctx, VkDraw *draw, Font *font,
                   float x, float y, float w, float h)
{
    if (!term || !ctx || !draw || !font || w <= 0 || h <= 0) return;

    term->x = x;
    term->y = y;
    term->w = w;
    term->h = h;

    term_sync_theme(term);

    float hdr_h = TERM_HEADER_HEIGHT;
    float body_y = y + hdr_h;
    float body_h = h - hdr_h;
    if (body_h < 10.0f) body_h = 10.0f;

    /* Measure character advance */
    float cw = font->space_advance > 0.0f ? font->space_advance : (font->pixel_height * 0.6f);
    float ch = font->line_height > 0.0f ? font->line_height : (font->pixel_height * 1.3f);
    term->char_w = cw;
    term->char_h = ch;

    /* Dynamic cols/rows calculation */
    float pad_x = 8.0f;
    float pad_y = 4.0f;
    int new_cols = (int)((w - pad_x * 2.0f) / cw);
    int new_rows = (int)((body_h - pad_y * 2.0f) / ch);
    if (new_cols > 0 && new_rows > 0 && (new_cols != term->cols || new_rows != term->rows)) {
        terminal_resize_grid(term, new_cols, new_rows);
    }

    /* 1. Header Bar */
    vk_draw_rounded_rect(draw, x, y, w, hdr_h, 0.0f, 1.0f, term->header_bg);
    vk_draw_rounded_rect(draw, x, y, w, 1.0f, 0.0f, 1.0f, term->header_border);

    /* Splitter grip handle in center */
    uint8_t grip_c[4] = { term->header_border[0] + 30, term->header_border[1] + 30, term->header_border[2] + 30, 255 };
    vk_draw_rounded_rect(draw, x + w * 0.5f - 24.0f, y + 2.0f, 48.0f, 2.0f, 1.0f, 0.0f, grip_c);

    /* Header Title */
    float hdr_baseline = y + (hdr_h + font->ascent + font->descent) * 0.5f;
    font_draw(font, ctx, x + 12.0f, hdr_baseline, term->title, term->header_fg);

    /* Header Action Buttons on right: [+] Clear [x] */
    const char *btn_restart = "[+] New";
    const char *btn_clear   = "Clear";
    const char *btn_close   = "✕";
    float bw_close   = font_measure(font, btn_close);
    float bw_clear   = font_measure(font, btn_clear);
    float bw_restart = font_measure(font, btn_restart);

    float bx_close   = x + w - bw_close - 16.0f;
    float bx_clear   = bx_close - bw_clear - 18.0f;
    float bx_restart = bx_clear - bw_restart - 18.0f;

    uint8_t btn_c[4] = { 150, 155, 165, 255 };
    font_draw(font, ctx, bx_restart, hdr_baseline, btn_restart, btn_c);
    font_draw(font, ctx, bx_clear, hdr_baseline, btn_clear, btn_c);
    font_draw(font, ctx, bx_close, hdr_baseline, btn_close, btn_c);

    /* 2. Terminal Body Background */
    vk_draw_rounded_rect(draw, x, body_y, w, body_h, 0.0f, 1.0f, term->default_bg);

    /* Blink update for cursor */
    uint64_t now_ticks = SDL_GetTicks();
    if (now_ticks - term->last_blink_ms >= 500) {
        term->blink_state = !term->blink_state;
        term->last_blink_ms = now_ticks;
    }

    /* 3. Render Characters & Cell Backgrounds */
    float baseline_offset = font->ascent;
    for (int r = 0; r < term->rows; ++r) {
        float line_y = body_y + pad_y + (float)r * ch;
        if (line_y + ch > body_y + body_h) break;

        /* Pick source row based on scroll offset */
        const TermCell *row_cells = NULL;
        if (term->scroll_offset > 0) {
            int hist_idx = term->scrollback_count - term->scroll_offset + r;
            if (hist_idx >= 0 && hist_idx < term->scrollback_count) {
                int circ_idx = (term->scrollback_head + hist_idx) % TERM_SCROLLBACK_CAP;
                row_cells = &term->scrollback[circ_idx * term->cols];
            }
        }
        if (!row_cells) {
            row_cells = &term->grid[r * term->cols];
        }

        /* Draw custom cell backgrounds first */
        for (int c = 0; c < term->cols; ++c) {
            const TermCell *cell = &row_cells[c];
            if (cell->bg[0] != term->default_bg[0] ||
                cell->bg[1] != term->default_bg[1] ||
                cell->bg[2] != term->default_bg[2]) {
                float cell_x = x + pad_x + (float)c * cw;
                vk_draw_rounded_rect(draw, cell_x, line_y, cw, ch, 0.0f, 0.0f, cell->bg);
            }
        }

        /* Batch contiguous runs of same foreground color and flags */
        int start_col = 0;
        while (start_col < term->cols) {
            const TermCell *first = &row_cells[start_col];
            if (first->c[0] == 0 || first->c[0] == ' ') {
                start_col++;
                continue;
            }

            const uint8_t *fg = first->fg;
            uint8_t flags = first->flags;
            uint8_t draw_fg[4];
            memcpy(draw_fg, fg, 4);
            if (flags & TERM_FLAG_DIM) {
                draw_fg[0] /= 2;
                draw_fg[1] /= 2;
                draw_fg[2] /= 2;
            }
            float draw_baseline = line_y + baseline_offset;

            /* If it's a non-ASCII / Nerd Font character, draw it anchored to its exact grid cell */
            if ((unsigned char)first->c[0] >= 0x80) {
                float draw_x = x + pad_x + (float)start_col * cw;
                font_draw(font, ctx, draw_x, draw_baseline, first->c, draw_fg);
                if (flags & TERM_FLAG_UNDERLINE) {
                    vk_draw_rounded_rect(draw, draw_x, line_y + ch - 1.0f,
                                         cw, 1.0f, 0.0f, 0.0f, draw_fg);
                }
                start_col++;
                continue;
            }

            /* Contiguous ASCII run */
            int end_col = start_col;
            char line_buf[512] = {0};
            size_t buf_pos = 0;

            while (end_col < term->cols) {
                const TermCell *c = &row_cells[end_col];
                if (c->c[0] == 0) break;
                if ((unsigned char)c->c[0] >= 0x80) break; /* stop before non-ASCII */
                if (c->fg[0] != fg[0] || c->fg[1] != fg[1] || c->fg[2] != fg[2] || c->flags != flags)
                    break;
                size_t clen = strlen(c->c);
                if (buf_pos + clen < sizeof(line_buf) - 1) {
                    memcpy(&line_buf[buf_pos], c->c, clen);
                    buf_pos += clen;
                    end_col++;
                } else {
                    break;
                }
            }

            if (buf_pos > 0) {
                line_buf[buf_pos] = 0;
                float draw_x = x + pad_x + (float)start_col * cw;
                font_draw(font, ctx, draw_x, draw_baseline, line_buf, draw_fg);
                if (flags & TERM_FLAG_UNDERLINE) {
                    vk_draw_rounded_rect(draw, draw_x, line_y + ch - 1.0f,
                                         (float)(end_col - start_col) * cw, 1.0f, 0.0f, 0.0f, draw_fg);
                }
            }
            start_col = end_col;
        }

        /* 4. Render Cursor */
        if (term->scroll_offset == 0 && r == term->cursor_y && term->cursor_visible) {
            float cur_x = x + pad_x + (float)term->cursor_x * cw;
            if (term->focused && term->blink_state) {
                vk_draw_rounded_rect(draw, cur_x, line_y, cw, ch, 1.0f, 0.0f, term->cursor_color);
                /* Invert char under cursor */
                if (term->cursor_x < term->cols) {
                    const TermCell *cur_cell = &term->grid[r * term->cols + term->cursor_x];
                    if (cur_cell->c[0] && cur_cell->c[0] != ' ') {
                        uint8_t inv_fg[4] = { 0, 0, 0, 255 };
                        font_draw(font, ctx, cur_x, line_y + baseline_offset, cur_cell->c, inv_fg);
                    }
                }
            } else if (!term->focused) {
                /* Hollow border cursor when unfocused */
                vk_draw_rounded_rect(draw, cur_x, line_y, cw, ch, 1.0f, 1.0f, term->cursor_color);
            }
        }
    }

    /* 5. Scrollbar Track if viewing history */
    if (term->scrollback_count > 0) {
        float bar_w = 4.0f;
        float bar_x = x + w - bar_w - 2.0f;
        float track_h = body_h - 4.0f;
        float thumb_h = track_h * ((float)term->rows / (float)(term->scrollback_count + term->rows));
        if (thumb_h < 16.0f) thumb_h = 16.0f;
        float progress = (float)(term->scrollback_count - term->scroll_offset) / (float)term->scrollback_count;
        float thumb_y = body_y + 2.0f + (track_h - thumb_h) * progress;
        uint8_t thumb_c[4] = { 100, 105, 115, 180 };
        vk_draw_rounded_rect(draw, bar_x, thumb_y, bar_w, thumb_h, 2.0f, 0.0f, thumb_c);
    }
}

bool terminal_handle_event(Terminal *term, App *app, const SDL_Event *ev)
{
    if (!term || !app) return false;

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float mx = (float)ev->button.x;
        float my = (float)ev->button.y;

        /* Click inside header */
        if (mx >= term->x && mx <= term->x + term->w &&
            my >= term->y && my <= term->y + TERM_HEADER_HEIGHT) {
            float bw_close = 20.0f;
            float bw_clear = 40.0f;
            float bw_restart = 50.0f;
            float bx_close = term->x + term->w - bw_close - 16.0f;
            float bx_clear = bx_close - bw_clear - 18.0f;
            float bx_restart = bx_clear - bw_restart - 18.0f;

            if (mx >= bx_close) {
                app->terminal_open = false;
                return true;
            } else if (mx >= bx_clear && mx < bx_close) {
                terminal_clear(term);
                return true;
            } else if (mx >= bx_restart && mx < bx_clear) {
                terminal_restart(term);
                return true;
            }

            /* Click on splitter top edge starts dragging */
            if (my <= term->y + 6.0f) {
                app->terminal_resizing = true;
                return true;
            }
            term->focused = true;
            return true;
        }

        /* Click inside terminal body */
        if (mx >= term->x && mx <= term->x + term->w &&
            my >= term->y + TERM_HEADER_HEIGHT && my <= term->y + term->h) {
            term->focused = true;
            return true;
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (app->terminal_resizing) {
            app->terminal_resizing = false;
            return true;
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        if (app->terminal_resizing) {
            float window_h = (float)app->vk.swapchain_extent.height;
            float status_h = 26.0f;
            float new_h = window_h - status_h - (float)ev->motion.y;
            if (new_h < TERM_MIN_HEIGHT) new_h = TERM_MIN_HEIGHT;
            if (new_h > window_h * 0.75f) new_h = window_h * 0.75f;
            app->terminal_height = new_h;
            return true;
        }
    }

    /* Mouse Wheel: Scrollback history */
    if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (mx >= term->x && mx <= term->x + term->w &&
            my >= term->y && my <= term->y + term->h) {
            int delta = (int)(ev->wheel.y * 3.0f);
            term->scroll_offset += delta;
            if (term->scroll_offset < 0) term->scroll_offset = 0;
            if (term->scroll_offset > term->scrollback_count)
                term->scroll_offset = term->scrollback_count;
            return true;
        }
    }

    if (!term->focused) return false;

    /* Text input */
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        SDL_Keymod mod = SDL_GetModState();
        bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
        bool gui = (mod & SDL_KMOD_GUI) != 0;
        if (!ctrl && !gui) {
            terminal_send_input(term, ev->text.text, strlen(ev->text.text));
            return true;
        }
    }

    /* Keyboard navigation & control keys */
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = ev->key.key;
        SDL_Keymod mod = ev->key.mod;
        bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
        bool shift = (mod & SDL_KMOD_SHIFT) != 0;
        bool alt = (mod & SDL_KMOD_ALT) != 0;

        /* Allow Ctrl+` to toggle terminal closed from inside */
        if ((ctrl || (mod & SDL_KMOD_GUI)) && (k == SDLK_GRAVE || k == '`')) {
            terminal_toggle(app);
            return true;
        }

        /* Clipboard Paste: Ctrl+Shift+V */
        if (ctrl && shift && (k == SDLK_V || k == 'v')) {
            char *clip = SDL_GetClipboardText();
            if (clip) {
                terminal_send_input(term, clip, strlen(clip));
                SDL_free(clip);
            }
            return true;
        }

        /* Control combinations */
        if (ctrl && !shift && !alt) {
            char c = 0;
            if (k >= SDLK_A && k <= SDLK_Z) c = (char)(k - SDLK_A + 1);
            else if (k >= 'a' && k <= 'z') c = (char)(k - 'a' + 1);
            if (c > 0) {
                terminal_send_input(term, &c, 1);
                return true;
            }
        }

        /* Common terminal keys */
        switch (k) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            terminal_send_input(term, "\r", 1);
            return true;
        case SDLK_BACKSPACE:
            terminal_send_input(term, "\x7f", 1);
            return true;
        case SDLK_TAB:
            terminal_send_input(term, "\t", 1);
            return true;
        case SDLK_ESCAPE:
            terminal_send_input(term, "\x1b", 1);
            return true;
        case SDLK_UP:
            terminal_send_input(term, "\x1b[A", 3);
            return true;
        case SDLK_DOWN:
            terminal_send_input(term, "\x1b[B", 3);
            return true;
        case SDLK_RIGHT:
            terminal_send_input(term, "\x1b[C", 3);
            return true;
        case SDLK_LEFT:
            terminal_send_input(term, "\x1b[D", 3);
            return true;
        case SDLK_HOME:
            terminal_send_input(term, "\x1b[H", 3);
            return true;
        case SDLK_END:
            terminal_send_input(term, "\x1b[F", 3);
            return true;
        case SDLK_PAGEUP:
            terminal_send_input(term, "\x1b[5~", 4);
            return true;
        case SDLK_PAGEDOWN:
            terminal_send_input(term, "\x1b[6~", 4);
            return true;
        case SDLK_DELETE:
            terminal_send_input(term, "\x1b[3~", 4);
            return true;
        default:
            break;
        }
    }

    return false;
}
