#ifndef PROJECT_TREE_H
#define PROJECT_TREE_H

#include <stdbool.h>

#include "app/app.h"

bool project_set_root(App *app, const char *path);
bool project_open_initial(App *app, const char *path);
bool project_load_children(App *app, int parent);
void project_update_visible(App *app);

#endif
