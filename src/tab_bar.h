/**
 * tab_bar.h – open buffers (tabs) and the tab strip
 */

#ifndef CGUI_TAB_BAR_H
#define CGUI_TAB_BAR_H

#include "app.h"

/** Active editor, or NULL when no tab is open. Clamps tab_active. */
Editor *app_ed(App *app);

const char *tab_label(const App *app, int index, char *buffer, size_t buffer_size);
void  tab_update_title(App *app);
int   tab_find_path(App *app, const char *path);
/** Open a tab (or focus the one already showing path_or_null). */
bool  tab_add(App *app, const char *path_or_null, const char *initial);
/** Close without prompting. Keeps at least one tab open. */
void  tab_remove_at(App *app, int index);
/** Close, prompting to save unsaved changes first. */
void  tab_close_at(App *app, int index);
/** Save / Discard / Cancel prompt. Returns 2 = save, 1 = discard, 0 = cancel. */
int   tab_prompt_unsaved(App *app, const Editor *ed);

float tab_width(const App *app, int index);
void  draw_tab_bar(App *app, VkContext *ctx);
/** Handle a left click at x inside the tab strip (select or close). */
void  tab_bar_handle_click(App *app, float mx);

#endif /* CGUI_TAB_BAR_H */
