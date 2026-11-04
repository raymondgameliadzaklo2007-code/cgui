/**
 * lua_bridge.c – C-Lua runtime bridge for cgui
 */

#include "lua_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <SDL3/SDL.h>
#include "app.h"

static lua_State *g_L = NULL;
static App *g_app = NULL;

DynamicCommand g_dynamic_commands[MAX_DYNAMIC_COMMANDS];
int g_dynamic_command_count = 0;

typedef struct KeyBinding {
    SDL_Keycode key;
    SDL_Keymod  mod;
    char command_id[64];
    int  lua_fn_ref;
} KeyBinding;

#define MAX_KEYBINDINGS 64
static KeyBinding g_keybindings[MAX_KEYBINDINGS];
static int g_keybinding_count = 0;

void lua_bridge_set_app(App *app)
{
    g_app = app;
}

static int error_handler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg) {
        luaL_traceback(L, L, msg, 1);
    } else if (!lua_isnoneornil(L, 1)) {
        if (!luaL_callmeta(L, 1, "__tostring"))
            lua_pushliteral(L, "(non-string error message)");
    }
    return 1;
}

bool lua_bridge_pcall(lua_State *L, int nargs, int nresults)
{
    if (!L) return false;

    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, error_handler);
    lua_insert(L, base);

    int status = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);

    if (status != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "[cgui-lua error] %s\n", err ? err : "unknown error");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool lua_bridge_dostring(const char *code)
{
    if (!g_L || !code) return false;
    if (luaL_loadstring(g_L, code) != LUA_OK) {
        fprintf(stderr, "[cgui-lua load error] %s\n", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return false;
    }
    return lua_bridge_pcall(g_L, 0, 0);
}

bool lua_bridge_dofile(const char *path)
{
    if (!g_L || !path) return false;
    if (luaL_loadfile(g_L, path) != LUA_OK) {
        fprintf(stderr, "[cgui-lua load file error] %s\n", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return false;
    }
    return lua_bridge_pcall(g_L, 0, 0);
}

static bool get_table_hex(lua_State *L, int tidx, const char *key, uint8_t out[4])
{
    int abs_idx = (tidx > 0) ? tidx : (lua_gettop(L) + tidx + 1);
    lua_getfield(L, abs_idx, key);
    bool ok = false;
    if (lua_isstring(L, -1)) {
        ok = theme_parse_hex(lua_tostring(L, -1), out);
    }
    lua_pop(L, 1);
    return ok;
}

bool lua_bridge_parse_base30(lua_State *L, int index, Theme *th)
{
    if (!L || !th) return false;
    int abs_idx = (index > 0) ? index : (lua_gettop(L) + index + 1);
    if (!lua_istable(L, abs_idx)) return false;

    int t = abs_idx;
    bool pushed_base30 = false;
    lua_getfield(L, abs_idx, "base_30");
    if (lua_istable(L, -1)) {
        t = lua_gettop(L);
        pushed_base30 = true;
    } else {
        lua_pop(L, 1);
    }

    /* Check type ("dark" or "light") */
    lua_getfield(L, abs_idx, "type");
    if (lua_isstring(L, -1)) {
        const char *tp = lua_tostring(L, -1);
        th->is_dark = (strcmp(tp, "light") != 0);
    } else {
        th->is_dark = true;
    }
    lua_pop(L, 1);

    uint8_t white[4] = { 171, 178, 191, 255 };
    uint8_t darker_black[4] = { 30, 34, 42, 255 };
    uint8_t black[4] = { 36, 41, 51, 255 };
    uint8_t line[4] = { 49, 53, 63, 255 };
    uint8_t one_bg[4] = { 40, 44, 52, 255 };
    uint8_t one_bg2[4] = { 53, 59, 69, 255 };
    uint8_t one_bg3[4] = { 62, 68, 81, 255 };
    uint8_t grey_fg[4] = { 92, 99, 112, 255 };
    uint8_t red[4] = { 224, 108, 117, 255 };
    uint8_t green[4] = { 152, 195, 121, 255 };
    uint8_t nord_blue[4] = { 97, 175, 239, 255 };
    uint8_t blue[4] = { 97, 175, 239, 255 };
    uint8_t yellow[4] = { 229, 192, 123, 255 };
    uint8_t purple[4] = { 198, 120, 221, 255 };
    uint8_t teal[4] = { 86, 182, 194, 255 };
    uint8_t orange[4] = { 209, 154, 102, 255 };
    uint8_t statusline_bg[4] = { 34, 38, 46, 255 };

    get_table_hex(L, t, "white", white);
    get_table_hex(L, t, "darker_black", darker_black);
    get_table_hex(L, t, "black", black);
    get_table_hex(L, t, "line", line);
    get_table_hex(L, t, "one_bg", one_bg);
    get_table_hex(L, t, "one_bg2", one_bg2);
    get_table_hex(L, t, "one_bg3", one_bg3);
    get_table_hex(L, t, "grey_fg", grey_fg);
    get_table_hex(L, t, "red", red);
    get_table_hex(L, t, "green", green);
    get_table_hex(L, t, "nord_blue", nord_blue);
    get_table_hex(L, t, "blue", blue);
    get_table_hex(L, t, "yellow", yellow);
    get_table_hex(L, t, "purple", purple);
    get_table_hex(L, t, "teal", teal);
    get_table_hex(L, t, "orange", orange);
    get_table_hex(L, t, "statusline_bg", statusline_bg);

    if (pushed_base30) {
        lua_pop(L, 1);
    }

    /* Populate Theme struct */
    memcpy(th->bg, black, 4);
    memcpy(th->fg, white, 4);
    memcpy(th->caret, nord_blue, 4);
    memcpy(th->selection, one_bg3, 4);
    th->selection[3] = 200;
    memcpy(th->line_highlight, line, 4);
    memcpy(th->gutter_bg, darker_black, 4);
    memcpy(th->gutter_fg, grey_fg, 4);
    memcpy(th->bracket_match, nord_blue, 4);
    th->bracket_match[3] = 90;

    /* Syntax mapping */
    memcpy(th->syntax[HL_DEFAULT],  white, 4);
    memcpy(th->syntax[HL_KEYWORD],  purple, 4);
    memcpy(th->syntax[HL_TYPE],     yellow, 4);
    memcpy(th->syntax[HL_FUNCTION], blue, 4);
    memcpy(th->syntax[HL_STRING],   green, 4);
    memcpy(th->syntax[HL_NUMBER],   orange, 4);
    memcpy(th->syntax[HL_COMMENT],  grey_fg, 4);
    memcpy(th->syntax[HL_PREPROC],  red, 4);
    memcpy(th->syntax[HL_OPERATOR], teal, 4);
    memcpy(th->syntax[HL_CONSTANT], orange, 4);
    memcpy(th->syntax[HL_PROPERTY], red, 4);

    /* Tab bar */
    memcpy(th->tab_strip_bg, darker_black, 4);
    memcpy(th->tab_active_bg, black, 4);
    memcpy(th->tab_inactive_bg, darker_black, 4);
    memcpy(th->tab_edge, line, 4);
    memcpy(th->tab_text_active, white, 4);
    memcpy(th->tab_text_inactive, grey_fg, 4);

    /* Status bar */
    memcpy(th->status_bg, statusline_bg, 4);
    memcpy(th->status_edge, line, 4);
    memcpy(th->status_fg, white, 4);

    /* Sidebar */
    memcpy(th->sidebar_bg, darker_black, 4);
    memcpy(th->sidebar_edge, line, 4);
    memcpy(th->sidebar_text, white, 4);
    memcpy(th->sidebar_heading, grey_fg, 4);
    memcpy(th->sidebar_muted, grey_fg, 4);
    memcpy(th->sidebar_hover, one_bg, 4);
    memcpy(th->sidebar_selected, one_bg2, 4);

    /* Overlays */
    memcpy(th->overlay_bg, darker_black, 4);
    memcpy(th->overlay_edge, line, 4);
    memcpy(th->overlay_sel, one_bg2, 4);

    return true;
}

static bool resolve_theme_path(const char *name, char *out_path, size_t out_cap)
{
    if (!name || !out_path || out_cap == 0) return false;

    const char *home = getenv("HOME");
    if (home) {
        snprintf(out_path, out_cap, "%s/.config/cgui/themes/%s.lua", home, name);
        struct stat st;
        if (stat(out_path, &st) == 0) return true;
    }

#ifdef CGUI_DEFAULT_THEMES_PATH
    snprintf(out_path, out_cap, "%s/%s.lua", CGUI_DEFAULT_THEMES_PATH, name);
    {
        struct stat st;
        if (stat(out_path, &st) == 0) return true;
    }
#endif

    const char *candidates[] = {
        "subprojects/base46/lua/base46/themes",
        "assets/themes",
        "/usr/share/cgui/themes",
        "/usr/local/share/cgui/themes",
        NULL
    };

    for (int i = 0; candidates[i]; ++i) {
        snprintf(out_path, out_cap, "%s/%s.lua", candidates[i], name);
        struct stat st;
        if (stat(out_path, &st) == 0) return true;
    }

    return false;
}

bool lua_bridge_set_theme(const char *name)
{
    if (!name) return false;

    /* Check built-in themes first */
    if (strcmp(name, "onedark") == 0) {
        theme_set_builtin_onedark(&g_theme);
        return true;
    } else if (strcmp(name, "light") == 0) {
        theme_set_builtin_light(&g_theme);
        return true;
    }

    if (!g_L) {
        fprintf(stderr, "[cgui-theme] Lua VM not initialized\n");
        return false;
    }

    char path[1024];
    if (!resolve_theme_path(name, path, sizeof(path))) {
        fprintf(stderr, "[cgui-theme] Theme '%s' not found on disk\n", name);
        return false;
    }

    int top = lua_gettop(g_L);
    if (luaL_loadfile(g_L, path) != LUA_OK) {
        fprintf(stderr, "[cgui-theme load error] %s\n", lua_tostring(g_L, -1));
        lua_settop(g_L, top);
        return false;
    }

    if (!lua_bridge_pcall(g_L, 0, 1)) {
        lua_settop(g_L, top);
        return false;
    }

    Theme loaded;
    memset(&loaded, 0, sizeof(loaded));
    strncpy(loaded.name, name, sizeof(loaded.name) - 1);

    bool ok = lua_bridge_parse_base30(g_L, -1, &loaded);
    lua_settop(g_L, top);

    if (ok) {
        g_theme = loaded;
        return true;
    }

    fprintf(stderr, "[cgui-theme] Failed to parse theme table: %s\n", name);
    return false;
}

static int lua_cgui_theme_set(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (g_app) {
        cgui_apply_theme_all(g_app, name);
        lua_pushboolean(L, true);
        return 1;
    }
    bool ok = lua_bridge_set_theme(name);
    lua_pushboolean(L, ok);
    return 1;
}

static int lua_cgui_theme_get(lua_State *L)
{
    lua_pushstring(L, g_theme.name);
    return 1;
}

static int lua_cgui_theme_toggle(lua_State *L)
{
    if (g_theme.is_dark) {
        if (g_app) cgui_apply_theme_all(g_app, "light");
        else lua_bridge_set_theme("light");
    } else {
        if (g_app) cgui_apply_theme_all(g_app, "onedark");
        else lua_bridge_set_theme("onedark");
    }
    lua_pushstring(L, g_theme.name);
    return 1;
}

static int lua_cgui_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    fprintf(stderr, "[cgui-log] %s\n", msg);
    return 0;
}

static void scan_dir_for_themes(const char *dirpath, char names[][64], int *count, int max_names)
{
    if (!dirpath || *count >= max_names) return;
    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *count < max_names) {
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcmp(ent->d_name + len - 4, ".lua") == 0) {
            char name[64];
            size_t n = len - 4;
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, ent->d_name, n);
            name[n] = '\0';

            /* Avoid duplicates */
            bool exists = false;
            for (int i = 0; i < *count; ++i) {
                if (strcmp(names[i], name) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                strncpy(names[*count], name, sizeof(names[0]) - 1);
                (*count)++;
            }
        }
    }
    closedir(d);
}

int lua_bridge_list_themes(char names[][64], int max_names)
{
    int count = 0;

    /* Always include built-in */
    if (count < max_names) {
        strncpy(names[count++], "onedark", sizeof(names[0]) - 1);
    }
    if (count < max_names) {
        strncpy(names[count++], "light", sizeof(names[0]) - 1);
    }

    const char *home = getenv("HOME");
    if (home) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/.config/cgui/themes", home);
        scan_dir_for_themes(buf, names, &count, max_names);
    }

#ifdef CGUI_DEFAULT_THEMES_PATH
    scan_dir_for_themes(CGUI_DEFAULT_THEMES_PATH, names, &count, max_names);
#endif

    scan_dir_for_themes("subprojects/base46/lua/base46/themes", names, &count, max_names);
    scan_dir_for_themes("assets/themes", names, &count, max_names);

    return count;
}

static int lua_cgui_theme_list(lua_State *L)
{
    char names[128][64];
    int count = lua_bridge_list_themes(names, 128);

    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        lua_pushstring(L, names[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* ---------------------------------------------------------------- *
 * Shortcut Parsing, Commands & Keymap Registry                     *
 * ---------------------------------------------------------------- */

static bool parse_shortcut(const char *str, SDL_Keycode *out_key, SDL_Keymod *out_mod)
{
    if (!str || !out_key || !out_mod) return false;
    *out_key = SDLK_UNKNOWN;
    *out_mod = SDL_KMOD_NONE;

    char buf[128];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *saveptr = NULL;
    char *token = strtok_r(buf, "+-", &saveptr);
    while (token) {
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') { *end = 0; end--; }

        if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0) {
            *out_mod |= SDL_KMOD_CTRL;
        } else if (strcasecmp(token, "Shift") == 0) {
            *out_mod |= SDL_KMOD_SHIFT;
        } else if (strcasecmp(token, "Alt") == 0) {
            *out_mod |= SDL_KMOD_ALT;
        } else if (strcasecmp(token, "GUI") == 0 || strcasecmp(token, "Super") == 0 || strcasecmp(token, "Cmd") == 0) {
            *out_mod |= SDL_KMOD_GUI;
        } else {
            if (strlen(token) == 1) {
                char c = token[0];
                if (c >= 'a' && c <= 'z') *out_key = (SDL_Keycode)('a' + (c - 'a'));
                else if (c >= 'A' && c <= 'Z') *out_key = (SDL_Keycode)('a' + (c - 'A'));
                else if (c >= '0' && c <= '9') *out_key = (SDL_Keycode)('0' + (c - '0'));
                else if (c == '=') *out_key = SDLK_EQUALS;
                else if (c == '-') *out_key = SDLK_MINUS;
                else if (c == '[') *out_key = SDLK_LEFTBRACKET;
                else if (c == ']') *out_key = SDLK_RIGHTBRACKET;
            } else if (strcasecmp(token, "F1") == 0) *out_key = SDLK_F1;
            else if (strcasecmp(token, "F2") == 0) *out_key = SDLK_F2;
            else if (strcasecmp(token, "F3") == 0) *out_key = SDLK_F3;
            else if (strcasecmp(token, "F4") == 0) *out_key = SDLK_F4;
            else if (strcasecmp(token, "F5") == 0) *out_key = SDLK_F5;
            else if (strcasecmp(token, "F6") == 0) *out_key = SDLK_F6;
            else if (strcasecmp(token, "F7") == 0) *out_key = SDLK_F7;
            else if (strcasecmp(token, "F8") == 0) *out_key = SDLK_F8;
            else if (strcasecmp(token, "F9") == 0) *out_key = SDLK_F9;
            else if (strcasecmp(token, "F10") == 0) *out_key = SDLK_F10;
            else if (strcasecmp(token, "F11") == 0) *out_key = SDLK_F11;
            else if (strcasecmp(token, "F12") == 0) *out_key = SDLK_F12;
            else if (strcasecmp(token, "Tab") == 0) *out_key = SDLK_TAB;
            else if (strcasecmp(token, "Return") == 0 || strcasecmp(token, "Enter") == 0) *out_key = SDLK_RETURN;
            else if (strcasecmp(token, "Space") == 0) *out_key = SDLK_SPACE;
            else if (strcasecmp(token, "Escape") == 0 || strcasecmp(token, "Esc") == 0) *out_key = SDLK_ESCAPE;
            else if (strcasecmp(token, "Backspace") == 0) *out_key = SDLK_BACKSPACE;
            else if (strcasecmp(token, "Delete") == 0 || strcasecmp(token, "Del") == 0) *out_key = SDLK_DELETE;
        }
        token = strtok_r(NULL, "+-", &saveptr);
    }
    return (*out_key != SDLK_UNKNOWN);
}

static int lua_cgui_commands_register(lua_State *L)
{
    const char *id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const char *label = lua_isstring(L, 3) ? lua_tostring(L, 3) : id;
    const char *shortcut = lua_isstring(L, 4) ? lua_tostring(L, 4) : "";

    for (int i = 0; i < g_dynamic_command_count; ++i) {
        if (strcmp(g_dynamic_commands[i].id, id) == 0) {
            if (g_dynamic_commands[i].lua_fn_ref != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, g_dynamic_commands[i].lua_fn_ref);
            }
            lua_pushvalue(L, 2);
            g_dynamic_commands[i].lua_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            strncpy(g_dynamic_commands[i].label, label, sizeof(g_dynamic_commands[i].label) - 1);
            strncpy(g_dynamic_commands[i].shortcut, shortcut, sizeof(g_dynamic_commands[i].shortcut) - 1);
            return 0;
        }
    }

    if (g_dynamic_command_count >= MAX_DYNAMIC_COMMANDS) {
        return luaL_error(L, "Maximum dynamic commands (%d) reached", MAX_DYNAMIC_COMMANDS);
    }

    DynamicCommand *cmd = &g_dynamic_commands[g_dynamic_command_count++];
    strncpy(cmd->id, id, sizeof(cmd->id) - 1);
    strncpy(cmd->label, label, sizeof(cmd->label) - 1);
    strncpy(cmd->shortcut, shortcut, sizeof(cmd->shortcut) - 1);

    lua_pushvalue(L, 2);
    cmd->lua_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (shortcut[0]) {
        SDL_Keycode k;
        SDL_Keymod m;
        if (parse_shortcut(shortcut, &k, &m) && g_keybinding_count < MAX_KEYBINDINGS) {
            KeyBinding *kb = &g_keybindings[g_keybinding_count++];
            kb->key = k;
            kb->mod = m;
            strncpy(kb->command_id, id, sizeof(kb->command_id) - 1);
            kb->lua_fn_ref = LUA_NOREF;
        }
    }

    return 0;
}

static int lua_cgui_commands_execute(lua_State *L)
{
    const char *id = luaL_checkstring(L, 1);
    bool ok = lua_bridge_execute_command_id(g_app, id);
    lua_pushboolean(L, ok);
    return 1;
}

static int lua_cgui_keymap_bind(lua_State *L)
{
    const char *shortcut = luaL_checkstring(L, 1);
    SDL_Keycode k;
    SDL_Keymod m;
    if (!parse_shortcut(shortcut, &k, &m)) {
        return luaL_error(L, "Invalid shortcut format: %s", shortcut);
    }

    if (g_keybinding_count >= MAX_KEYBINDINGS) {
        return luaL_error(L, "Maximum keybindings (%d) reached", MAX_KEYBINDINGS);
    }

    KeyBinding *kb = &g_keybindings[g_keybinding_count++];
    kb->key = k;
    kb->mod = m;
    kb->lua_fn_ref = LUA_NOREF;
    kb->command_id[0] = 0;

    if (lua_isstring(L, 2)) {
        strncpy(kb->command_id, lua_tostring(L, 2), sizeof(kb->command_id) - 1);
    } else if (lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        kb->lua_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        return luaL_error(L, "Argument 2 to bind must be command string or function");
    }

    return 0;
}

bool lua_bridge_execute_command(App *app, int index)
{
    (void)app;
    if (!g_L || index < 0 || index >= g_dynamic_command_count) return false;
    DynamicCommand *cmd = &g_dynamic_commands[index];
    if (cmd->lua_fn_ref == LUA_NOREF) return false;

    lua_rawgeti(g_L, LUA_REGISTRYINDEX, cmd->lua_fn_ref);
    return lua_bridge_pcall(g_L, 0, 0);
}

bool lua_bridge_execute_command_id(App *app, const char *id)
{
    if (!id) return false;

    if (strcmp(id, "theme:toggle") == 0 || strcmp(id, "theme:toggle_dark_light") == 0) {
        if (g_theme.is_dark) {
            cgui_apply_theme_all(app, "light");
        } else {
            cgui_apply_theme_all(app, "onedark");
        }
        return true;
    }
    if (strcmp(id, "theme:picker") == 0 || strcmp(id, "theme:select") == 0) {
        if (app) theme_picker_open(app);
        return true;
    }
    if (strcmp(id, "palette:open") == 0) {
        if (app) palette_show(app);
        return true;
    }
    if (strcmp(id, "file:new") == 0) {
        if (app) tab_add(app, NULL, "");
        return true;
    }
    if (strcmp(id, "file:open") == 0) {
        /* Open file dialog */
        return true;
    }

    for (int i = 0; i < g_dynamic_command_count; ++i) {
        if (strcmp(g_dynamic_commands[i].id, id) == 0) {
            return lua_bridge_execute_command(app, i);
        }
    }

    fprintf(stderr, "[cgui-commands] Unknown command '%s'\n", id);
    return false;
}

bool lua_bridge_handle_key(App *app, unsigned int key, unsigned int mod)
{
    for (int i = 0; i < g_keybinding_count; ++i) {
        KeyBinding *kb = &g_keybindings[i];
        if (kb->key == (SDL_Keycode)key) {
            bool ctrl_match = ((kb->mod & SDL_KMOD_CTRL) != 0) == ((mod & SDL_KMOD_CTRL) != 0);
            bool shift_match = ((kb->mod & SDL_KMOD_SHIFT) != 0) == ((mod & SDL_KMOD_SHIFT) != 0);
            bool alt_match = ((kb->mod & SDL_KMOD_ALT) != 0) == ((mod & SDL_KMOD_ALT) != 0);
            if (ctrl_match && shift_match && alt_match) {
                if (kb->lua_fn_ref != LUA_NOREF && g_L) {
                    lua_rawgeti(g_L, LUA_REGISTRYINDEX, kb->lua_fn_ref);
                    lua_bridge_pcall(g_L, 0, 0);
                    return true;
                } else if (kb->command_id[0]) {
                    return lua_bridge_execute_command_id(app, kb->command_id);
                }
            }
        }
    }
    return false;
}

bool lua_bridge_apply_config(App *app)
{
    if (!g_L) return false;
    lua_getglobal(g_L, "cgui");
    if (!lua_istable(g_L, -1)) {
        lua_pop(g_L, 1);
        return false;
    }

    lua_getfield(g_L, -1, "config");
    if (!lua_istable(g_L, -1)) {
        lua_pop(g_L, 2);
        return false;
    }

    /* theme */
    lua_getfield(g_L, -1, "theme");
    if (lua_isstring(g_L, -1)) {
        const char *th = lua_tostring(g_L, -1);
        if (app) cgui_apply_theme_all(app, th);
        else lua_bridge_set_theme(th);
    }
    lua_pop(g_L, 1);

    /* font_size */
    lua_getfield(g_L, -1, "font_size");
    if (lua_isnumber(g_L, -1) && app) {
        float sz = (float)lua_tonumber(g_L, -1);
        if (sz >= 8.0f && sz <= 48.0f) {
            app->font_size = sz;
        }
    }
    lua_pop(g_L, 1);

    /* soft_wrap */
    lua_getfield(g_L, -1, "soft_wrap");
    if (lua_isboolean(g_L, -1) && app) {
        bool sw = lua_toboolean(g_L, -1);
        app->soft_wrap = sw;
        for (int i = 0; i < app->tab_count; ++i)
            app->tabs[i].soft_wrap = sw;
    }
    lua_pop(g_L, 1);

    /* line_numbers */
    lua_getfield(g_L, -1, "line_numbers");
    if (lua_isboolean(g_L, -1) && app) {
        bool ln = lua_toboolean(g_L, -1);
        for (int i = 0; i < app->tab_count; ++i)
            app->tabs[i].show_line_numbers = ln;
    }
    lua_pop(g_L, 1);

    lua_pop(g_L, 2); /* pop config, cgui */
    return true;
}

bool lua_bridge_init(void)
{
    if (g_L) return true;

    g_L = luaL_newstate();
    if (!g_L) {
        fprintf(stderr, "[cgui-lua] Failed to create Lua state\n");
        return false;
    }

    luaL_openlibs(g_L);

    /* Create 'cgui' global table */
    lua_newtable(g_L);

    /* cgui.log */
    lua_pushcfunction(g_L, lua_cgui_log);
    lua_setfield(g_L, -2, "log");

    /* cgui.theme sub-table */
    lua_newtable(g_L);
    lua_pushcfunction(g_L, lua_cgui_theme_set);
    lua_setfield(g_L, -2, "set");
    lua_pushcfunction(g_L, lua_cgui_theme_get);
    lua_setfield(g_L, -2, "get");
    lua_pushcfunction(g_L, lua_cgui_theme_list);
    lua_setfield(g_L, -2, "list");
    lua_pushcfunction(g_L, lua_cgui_theme_toggle);
    lua_setfield(g_L, -2, "toggle");
    lua_setfield(g_L, -2, "theme");

    /* cgui.commands */
    lua_newtable(g_L);
    lua_pushcfunction(g_L, lua_cgui_commands_register);
    lua_setfield(g_L, -2, "register");
    lua_pushcfunction(g_L, lua_cgui_commands_execute);
    lua_setfield(g_L, -2, "execute");
    lua_setfield(g_L, -2, "commands");

    /* cgui.keymap */
    lua_newtable(g_L);
    lua_pushcfunction(g_L, lua_cgui_keymap_bind);
    lua_setfield(g_L, -2, "bind");
    lua_pushcfunction(g_L, lua_cgui_keymap_bind);
    lua_setfield(g_L, -2, "set");
    lua_setfield(g_L, -2, "keymap");

    /* cgui.config table */
    lua_newtable(g_L);
    lua_pushstring(g_L, "onedark");
    lua_setfield(g_L, -2, "theme");
    lua_pushnumber(g_L, 15.0);
    lua_setfield(g_L, -2, "font_size");
    lua_pushboolean(g_L, false);
    lua_setfield(g_L, -2, "soft_wrap");
    lua_pushboolean(g_L, true);
    lua_setfield(g_L, -2, "line_numbers");
    lua_setfield(g_L, -2, "config");

    lua_setglobal(g_L, "cgui");

    return true;
}

void lua_bridge_destroy(void)
{
    if (g_L) {
        for (int i = 0; i < g_dynamic_command_count; ++i) {
            if (g_dynamic_commands[i].lua_fn_ref != LUA_NOREF) {
                luaL_unref(g_L, LUA_REGISTRYINDEX, g_dynamic_commands[i].lua_fn_ref);
                g_dynamic_commands[i].lua_fn_ref = LUA_NOREF;
            }
        }
        for (int i = 0; i < g_keybinding_count; ++i) {
            if (g_keybindings[i].lua_fn_ref != LUA_NOREF) {
                luaL_unref(g_L, LUA_REGISTRYINDEX, g_keybindings[i].lua_fn_ref);
                g_keybindings[i].lua_fn_ref = LUA_NOREF;
            }
        }
        lua_close(g_L);
        g_L = NULL;
    }
}

lua_State *lua_bridge_get_state(void)
{
    return g_L;
}

bool lua_bridge_load_user_config(void)
{
    if (!g_L) return false;

    const char *home = getenv("HOME");
    if (!home) return false;

    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/.config/cgui/init.lua", home);

    struct stat st;
    if (stat(config_path, &st) != 0) {
        return false;
    }

    printf("[cgui-lua] Loading user config from %s\n", config_path);
    return lua_bridge_dofile(config_path);
}
