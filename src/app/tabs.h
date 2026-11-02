#ifndef TABS_H
#define TABS_H

#include <stdbool.h>
#include <stddef.h>

#include "app/app.h"

Editor *app_ed(App *app);
const char *tab_label(const App *app, int index, char *buffer, size_t buffer_size);
void tab_update_title(App *app);
bool tab_add(App *app, const char *path_or_null, const char *initial);
void tab_remove_at(App *app, int index);
float tab_width(const App *app, int index);

#endif
