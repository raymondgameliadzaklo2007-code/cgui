#ifndef DIALOGS_H
#define DIALOGS_H

#include "app/app.h"

void file_open_dialog(App *app);
void file_save_as_dialog(App *app);
void project_show_folder_dialog(App *app);
void tab_close_at(App *app, int index);
void continue_quit(App *app);

#endif
