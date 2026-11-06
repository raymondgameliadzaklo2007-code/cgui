/**
 * ui_layout.c – Clay-powered flexbox layout engine for cgui (JetBrains New UI)
 */

#define CLAY_IMPLEMENTATION

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#include "clay.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "ui_layout.h"
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clay_error_handler(Clay_ErrorData errorData)
{
    fprintf(stderr, "[Clay Layout Error] %.*s\n", (int)errorData.errorText.length, errorData.errorText.chars);
}

bool ui_layout_init(UiLayout *ui, float width, float height)
{
    if (!ui) return false;
    memset(ui, 0, sizeof(*ui));

    ui->width = width > 0.0f ? width : 1280.0f;
    ui->height = height > 0.0f ? height : 800.0f;

    ui->sidebar_open = true;
    ui->sidebar_width = 240.0f;
    ui->active_tool = TOOL_STRIPE_PROJECT;

    ui->dock_open = false;
    ui->dock_tab = DOCK_TAB_TERMINAL;
    ui->dock_height = 200.0f;

    uint32_t min_memory = Clay_MinMemorySize();
    ui->clay_memory_size = (size_t)min_memory * 2;
    if (ui->clay_memory_size < 1024 * 1024) {
        ui->clay_memory_size = 1024 * 1024;
    }

    ui->clay_memory = malloc(ui->clay_memory_size);
    if (!ui->clay_memory) {
        fprintf(stderr, "Failed to allocate memory for Clay layout arena (%zu bytes)\n", ui->clay_memory_size);
        return false;
    }

    ui->arena = Clay_CreateArenaWithCapacityAndMemory(ui->clay_memory_size, ui->clay_memory);
    Clay_Initialize(ui->arena, (Clay_Dimensions){ ui->width, ui->height }, (Clay_ErrorHandler){ .errorHandlerFunction = clay_error_handler });

    ui->initialized = true;
    return true;
}

void ui_layout_destroy(UiLayout *ui)
{
    if (!ui) return;
    if (ui->clay_memory) {
        free(ui->clay_memory);
        ui->clay_memory = NULL;
    }
    ui->initialized = false;
}

static Clay_BoundingBox get_bounds(const char *id_str)
{
    Clay_String s = {
        .isStaticallyAllocated = true,
        .length = (int32_t)strlen(id_str),
        .chars = id_str,
    };
    Clay_ElementData data = Clay_GetElementData(Clay_GetElementId(s));
    if (data.found) {
        return data.boundingBox;
    }
    return (Clay_BoundingBox){ 0, 0, 0, 0 };
}

void ui_layout_update(UiLayout *ui, struct App *app, float width, float height,
                      float mouse_x, float mouse_y, bool mouse_down)
{
    (void)app;
    if (!ui || !ui->initialized) return;

    if (width <= 0.0f) width = 800.0f;
    if (height <= 0.0f) height = 600.0f;

    ui->width = width;
    ui->height = height;

    Clay_SetLayoutDimensions((Clay_Dimensions){ width, height });
    Clay_SetPointerState((Clay_Vector2){ mouse_x, mouse_y }, mouse_down);

    Clay_BeginLayout();

    /* Root Container: Column (Header Toolbar, Middle Body, Status Bar) */
    CLAY(CLAY_ID("Root"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(width), .height = CLAY_SIZING_FIXED(height) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        }
    }) {
        /* 1. Main Toolbar (JetBrains Top Header) */
        CLAY(CLAY_ID("MainToolbar"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(40.0f) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                .padding = { 12, 12, 0, 0 },
                .childGap = 8,
            }
        }) {
            /* Left: Project title & Git branch */
            CLAY(CLAY_ID("ToolbarLeft"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIXED(28.0f) },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 8,
                }
            }) {
                CLAY(CLAY_ID("BranchBtn"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIXED(26.0f) }, .padding = { 8, 8, 0, 0 } }
                }) {}
            }

            /* Spacer */
            CLAY(CLAY_ID("ToolbarSpacer1"), {
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1.0f) } }
            }) {}

            /* Center: JetBrains Run Widget */
            CLAY(CLAY_ID("RunWidgetContainer"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIXED(28.0f) },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 3,
                }
            }) {
                CLAY(CLAY_ID("RunDropdown"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(160.0f), .height = CLAY_SIZING_FIXED(28.0f) } }
                }) {}
                CLAY(CLAY_ID("RunBtn"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(28.0f), .height = CLAY_SIZING_FIXED(28.0f) } }
                }) {}
                CLAY(CLAY_ID("BuildBtn"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(28.0f), .height = CLAY_SIZING_FIXED(28.0f) } }
                }) {}
                CLAY(CLAY_ID("DebugBtn"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(28.0f), .height = CLAY_SIZING_FIXED(28.0f) } }
                }) {}
            }

            /* Spacer */
            CLAY(CLAY_ID("ToolbarSpacer2"), {
                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1.0f) } }
            }) {}

            /* Right: Search Everywhere & Settings */
            CLAY(CLAY_ID("ToolbarRight"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIXED(28.0f) },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 6,
                }
            }) {
                CLAY(CLAY_ID("SearchBtn"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(28.0f), .height = CLAY_SIZING_FIXED(28.0f) } }
                }) {}
                CLAY(CLAY_ID("SettingsBtn"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(28.0f), .height = CLAY_SIZING_FIXED(28.0f) } }
                }) {}
            }
        }

        /* 2. Middle Row: Tool Stripe + Sidebar + Main Workspace */
        CLAY(CLAY_ID("MiddleRow"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            }
        }) {
            /* Left Tool Window Stripe (JetBrains 40px Icon Rail) */
            CLAY(CLAY_ID("ToolStripe"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIXED(40.0f), .height = CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                    .padding = { 0, 0, 6, 6 },
                    .childGap = 4,
                }
            }) {
                CLAY(CLAY_ID("StripeProject"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(36.0f), .height = CLAY_SIZING_FIXED(36.0f) } }
                }) {}
                CLAY(CLAY_ID("StripeSearch"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(36.0f), .height = CLAY_SIZING_FIXED(36.0f) } }
                }) {}
                CLAY(CLAY_ID("StripeGit"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(36.0f), .height = CLAY_SIZING_FIXED(36.0f) } }
                }) {}
                CLAY(CLAY_ID("StripeDebug"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(36.0f), .height = CLAY_SIZING_FIXED(36.0f) } }
                }) {}

                /* Stripe Spacer to push Settings to bottom */
                CLAY(CLAY_ID("StripeSpacer"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(1.0f), .height = CLAY_SIZING_GROW(0) } }
                }) {}

                CLAY(CLAY_ID("StripeSettings"), {
                    .layout = { .sizing = { .width = CLAY_SIZING_FIXED(36.0f), .height = CLAY_SIZING_FIXED(36.0f) } }
                }) {}
            }

            /* Left Tool Window (Project Sidebar drawer) */
            if (ui->sidebar_open) {
                float sb_w = ui->sidebar_width > 120.0f ? ui->sidebar_width : 240.0f;
                CLAY(CLAY_ID("ProjectSidebar"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_FIXED(sb_w), .height = CLAY_SIZING_GROW(0) },
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    }
                }) {}
            }

            /* Center Workspace (Tabs + Editor Buffer + Bottom Dock) */
            CLAY(CLAY_ID("MainWorkspace"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                }
            }) {
                /* Flush Editor Tab Bar */
                CLAY(CLAY_ID("EditorTabs"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(34.0f) },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    }
                }) {}

                /* Editor Buffer */
                CLAY(CLAY_ID("EditorViewport"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                    }
                }) {}

                /* Bottom Dockable Tool Window (Terminal & Problems) */
                if (ui->dock_open) {
                    float dk_h = ui->dock_height > 80.0f ? ui->dock_height : 200.0f;
                    CLAY(CLAY_ID("BottomDock"), {
                        .layout = {
                            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(dk_h) },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        }
                    }) {
                        /* Dock Tab Header */
                        CLAY(CLAY_ID("DockHeader"), {
                            .layout = {
                                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(28.0f) },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                .padding = { 8, 8, 0, 0 },
                                .childGap = 4,
                            }
                        }) {
                            CLAY(CLAY_ID("DockTabTerm"), {
                                .layout = { .sizing = { .width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIXED(24.0f) }, .padding = { 8, 8, 0, 0 } }
                            }) {}
                            CLAY(CLAY_ID("DockTabProb"), {
                                .layout = { .sizing = { .width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIXED(24.0f) }, .padding = { 8, 8, 0, 0 } }
                            }) {}
                            CLAY(CLAY_ID("DockHeaderSpacer"), {
                                .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1.0f) } }
                            }) {}
                            CLAY(CLAY_ID("DockCloseBtn"), {
                                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(22.0f), .height = CLAY_SIZING_FIXED(22.0f) } }
                            }) {}
                        }

                        /* Dock Body */
                        CLAY(CLAY_ID("DockBody"), {
                            .layout = {
                                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                            }
                        }) {}
                    }
                }
            }
        }

        /* 3. Bottom Status Bar */
        CLAY(CLAY_ID("StatusBar"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(24.0f) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                .padding = { 8, 8, 0, 0 },
            }
        }) {}
    }

    Clay_EndLayout(0.016f);

    /* Extract exact computed bounding boxes */
    ui->toolbar_bounds   = get_bounds("MainToolbar");
    ui->stripe_bounds    = get_bounds("ToolStripe");
    ui->sidebar_bounds   = get_bounds("ProjectSidebar");
    ui->tabbar_bounds    = get_bounds("EditorTabs");
    ui->editor_bounds    = get_bounds("EditorViewport");
    ui->dock_bounds      = get_bounds("BottomDock");
    ui->statusbar_bounds = get_bounds("StatusBar");

    /* Sub-elements */
    ui->run_dropdown_bounds = get_bounds("RunDropdown");
    ui->run_btn_bounds      = get_bounds("RunBtn");
    ui->build_btn_bounds    = get_bounds("BuildBtn");
    ui->debug_btn_bounds    = get_bounds("DebugBtn");
    ui->search_btn_bounds   = get_bounds("SearchBtn");
    ui->settings_btn_bounds = get_bounds("SettingsBtn");
    ui->branch_btn_bounds   = get_bounds("BranchBtn");

    ui->stripe_project_bounds  = get_bounds("StripeProject");
    ui->stripe_search_bounds   = get_bounds("StripeSearch");
    ui->stripe_git_bounds      = get_bounds("StripeGit");
    ui->stripe_debug_bounds    = get_bounds("StripeDebug");
    ui->stripe_settings_bounds = get_bounds("StripeSettings");

    ui->dock_tab_term_bounds   = get_bounds("DockTabTerm");
    ui->dock_tab_prob_bounds   = get_bounds("DockTabProb");
    ui->dock_close_bounds      = get_bounds("DockCloseBtn");
}
