// LVGL display driver for the desktop simulator.
// Renders to an SDL2 window at 2× scale (640×344 for a 320×172 source).

#include "../src/lvgl_driver.h"
#include "sim_events.h"
#include <SDL.h>
#include <cstdio>

static constexpr int SCALE   = 2;
static constexpr int WIN_W   = LCD_W * SCALE;  // 640
static constexpr int WIN_H   = LCD_H * SCALE;  // 344

SDL_Window*   sim_window   = nullptr;
SDL_Renderer* sim_renderer = nullptr;
SDL_Texture*  sim_texture  = nullptr;

// ---------------------------------------------------------------------------
// Flush callback: push LVGL dirty rect into the SDL texture and present
// ---------------------------------------------------------------------------
void Lvgl_FlushCB(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    SDL_Rect rect = { area->x1, area->y1, w, h };
    // lv_color_t is uint16_t RGB565 with LV_COLOR_16_SWAP=0 → native layout
    SDL_UpdateTexture(sim_texture, &rect, buf, w * (int)sizeof(lv_color_t));

    SDL_RenderClear(sim_renderer);
    SDL_RenderCopy(sim_renderer, sim_texture, nullptr, nullptr);
    SDL_RenderPresent(sim_renderer);

    lv_disp_flush_ready(drv);
}

void Lvgl_TickCB(void*) { /* unused in sim — tick driven from main loop */ }

// ---------------------------------------------------------------------------
// Shared event pump (sim_events.h)
// ---------------------------------------------------------------------------
static int  s_pendingKey = 0;
static bool s_quit       = false;

bool sim_pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) s_quit = true;
        else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) s_quit = true;
            else s_pendingKey = e.key.keysym.sym;
        }
    }
    return !s_quit;
}

int sim_take_key() {
    int k = s_pendingKey;
    s_pendingKey = 0;
    return k;
}

// ---------------------------------------------------------------------------
// Lvgl_Init: create SDL2 window + register LVGL display driver
// ---------------------------------------------------------------------------

static lv_color_t _buf1[LVGL_BUF_PIXELS];
static lv_color_t _buf2[LVGL_BUF_PIXELS];
static lv_disp_draw_buf_t _draw_buf;

void Lvgl_Init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    sim_window = SDL_CreateWindow(
        "QueueWatch Simulator  [C=connect WiFi, D=drop WiFi, ESC=quit]",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );
    if (!sim_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }

    sim_renderer = SDL_CreateRenderer(sim_window, -1,
                                      SDL_RENDERER_ACCELERATED |
                                      SDL_RENDERER_PRESENTVSYNC);
    if (!sim_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return;
    }
    SDL_RenderSetLogicalSize(sim_renderer, LCD_W, LCD_H);

    // RGB565 texture — same pixel format LVGL writes (LV_COLOR_16_SWAP=0)
    sim_texture = SDL_CreateTexture(sim_renderer,
                                    SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    LCD_W, LCD_H);
    if (!sim_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }

    lv_init();

    lv_disp_draw_buf_init(&_draw_buf, _buf1, _buf2, LVGL_BUF_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_W;
    disp_drv.ver_res      = LCD_H;
    disp_drv.flush_cb     = Lvgl_FlushCB;
    disp_drv.draw_buf     = &_draw_buf;
    disp_drv.full_refresh = 1;   // always full-frame in sim (simpler)
    lv_disp_drv_register(&disp_drv);

    // No touch input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_NONE;
    indev_drv.read_cb = nullptr;
    lv_indev_drv_register(&indev_drv);
}
