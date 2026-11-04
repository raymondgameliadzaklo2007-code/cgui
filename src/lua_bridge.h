/**
 * lua_bridge.h – C-Lua runtime bridge for cgui
 *
 * Embeds Lua 5.4 for configuration, user scripts, and NvChad base46 themes.
 */

#ifndef CGUI_LUA_BRIDGE_H
#define CGUI_LUA_BRIDGE_H

#include <stdbool.h>
#include <lua.h>
#include "theme.h"

/* Initialize Lua VM, standard libs, and cgui table */
bool lua_bridge_init(void);

/* Cleanly tear down Lua VM */
void lua_bridge_destroy(void);

/* Access underlying lua_State */
lua_State *lua_bridge_get_state(void);

/* Safe pcall with formatted traceback */
bool lua_bridge_pcall(lua_State *L, int nargs, int nresults);

/* Execute Lua code string safely */
bool lua_bridge_dostring(const char *code);

/* Execute Lua file safely */
bool lua_bridge_dofile(const char *path);

/* Load ~/.config/cgui/init.lua if present */
bool lua_bridge_load_user_config(void);

/* Load theme by name (e.g. "onedark", "catppuccin", "tokyonight") */
bool lua_bridge_set_theme(const char *name);

/* Parse NvChad base_30 table at stack index into Theme struct */
bool lua_bridge_parse_base30(lua_State *L, int index, Theme *th);

/* Scan theme directories and return count of discovered themes */
int lua_bridge_list_themes(char names[][64], int max_names);

struct App;

typedef struct DynamicCommand {
    char id[64];
    char label[128];
    char shortcut[64];
    int lua_fn_ref; /* LUA_NOREF or registry reference */
} DynamicCommand;

#define MAX_DYNAMIC_COMMANDS 64

extern DynamicCommand g_dynamic_commands[MAX_DYNAMIC_COMMANDS];
extern int g_dynamic_command_count;

void lua_bridge_set_app(struct App *app);
bool lua_bridge_apply_config(struct App *app);
bool lua_bridge_execute_command(struct App *app, int index);
bool lua_bridge_execute_command_id(struct App *app, const char *id);
bool lua_bridge_handle_key(struct App *app, unsigned int key, unsigned int mod);

#endif /* CGUI_LUA_BRIDGE_H */
