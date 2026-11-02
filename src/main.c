#include <stdio.h>

#include <SDL3/SDL.h>

#include "app/app.h"
#include "app/input.h"

static SDL_Window *create_window(void)
{
    /* Size window from the display's usable bounds (swapchain follows window pixels). */
    int win_w = 1280, win_h = 800;
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    SDL_Rect bounds;
    if (SDL_GetDisplayUsableBounds(disp, &bounds)) {
        win_w = bounds.w * 4 / 5;
        win_h = bounds.h * 4 / 5;
        if (win_w < 800) win_w = 800;
        if (win_h < 600) win_h = 600;
    }

    SDL_Window *window = SDL_CreateWindow(
        "cgui – editor",
        win_w, win_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN
    );
    if (!window)
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return window;
}

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = create_window();
    if (!window) {
        SDL_Quit();
        return 1;
    }

    App app = {0};
    if (!app_init(&app, window, argc, argv))
        return 1;

    while (app.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            input_handle_event(&app, &ev);
        app_frame(&app);
    }

    app_shutdown(&app);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
