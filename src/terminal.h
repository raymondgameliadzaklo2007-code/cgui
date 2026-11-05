/**
 * terminal.h – Integrated pseudo-terminal (PTY) and ANSI emulator for cgui
 *
 * Spawns the user's interactive shell ($SHELL or /bin/bash) in a pseudo-terminal,
 * handles non-blocking VT100 / xterm ANSI escape sequences, 256-color & truecolor
 * formatting, a scrollback history buffer, and renders via Vulkan and FreeType.
 */

#ifndef CGUI_TERMINAL_H
#define CGUI_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define TERM_MAX_COLS 320
#define TERM_MAX_ROWS 160
#define TERM_SCROLLBACK_CAP 2048
#define TERM_DEFAULT_HEIGHT 240.0f
#define TERM_MIN_HEIGHT 90.0f
#define TERM_HEADER_HEIGHT 26.0f

#define TERM_FLAG_BOLD      (1 << 0)
#define TERM_FLAG_DIM       (1 << 1)
#define TERM_FLAG_UNDERLINE (1 << 2)
#define TERM_FLAG_INVERSE   (1 << 3)

typedef struct TermCell {
    char    c[8];       /* UTF-8 encoded character or ASCII, null terminated */
    uint8_t fg[4];      /* RGBA */
    uint8_t bg[4];      /* RGBA */
    uint8_t flags;      /* TERM_FLAG_* */
} TermCell;

typedef enum TermParseState {
    TERM_STATE_NORMAL = 0,
    TERM_STATE_ESC,
    TERM_STATE_CSI,
    TERM_STATE_OSC,
    TERM_STATE_CHARSET
} TermParseState;

typedef struct Terminal {
    int       master_fd;
    pid_t     pid;
    bool      alive;
    bool      focused;

    int       cols;
    int       rows;
    int       cursor_x;
    int       cursor_y;
    int       saved_cursor_x;
    int       saved_cursor_y;
    bool      cursor_visible;
    uint64_t  last_blink_ms;
    bool      blink_state;

    TermCell *grid;             /* cols * rows */
    TermCell *scrollback;       /* TERM_SCROLLBACK_CAP * cols */
    int       scrollback_count; /* number of lines in scrollback */
    int       scrollback_head;  /* circular buffer head index */
    int       scroll_offset;    /* 0 = active screen, >0 = viewing history */

    /* ANSI parser state */
    TermParseState parse_state;
    int       params[16];
    int       param_count;
    char      private_prefix;
    uint8_t   cur_fg[4];
    uint8_t   cur_bg[4];
    uint8_t   cur_flags;

    /* Theme colors */
    uint8_t   default_fg[4];
    uint8_t   default_bg[4];
    uint8_t   cursor_color[4];
    uint8_t   header_bg[4];
    uint8_t   header_border[4];
    uint8_t   header_fg[4];

    char      working_dir[1024];
    char      shell_name[64];
    char      title[128];

    /* Geometry in screen coords */
    float     x, y, w, h;
    float     char_w;
    float     char_h;
} Terminal;

struct App;
struct VkContext;
struct VkDraw;
struct Font;
union SDL_Event;

/** Lifecycle */
bool terminal_init(Terminal *term, const char *working_dir);
void terminal_destroy(Terminal *term);
void terminal_restart(Terminal *term);

/** Non-blocking poll: drains output from PTY into cell grid */
void terminal_update(Terminal *term);

/** Resize PTY and buffer dimensions */
void terminal_resize_grid(Terminal *term, int new_cols, int new_rows);

/** Input */
void terminal_send_input(Terminal *term, const char *data, size_t len);
void terminal_clear(Terminal *term);

/** Drawing and interaction */
void terminal_draw(Terminal *term, struct VkContext *ctx, struct VkDraw *draw, struct Font *font,
                   float x, float y, float w, float h);
bool terminal_handle_event(Terminal *term, struct App *app, const union SDL_Event *ev);

#endif /* CGUI_TERMINAL_H */
