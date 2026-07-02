#include "lvgl_driver.h"
#include <esp_timer.h>

// ---------------------------------------------------------------------------
// Static draw buffers (allocated in DRAM, not PSRAM, for SPI DMA access)
// ---------------------------------------------------------------------------
static lv_color_t _buf1[LVGL_BUF_PIXELS];
static lv_color_t _buf2[LVGL_BUF_PIXELS];
static lv_disp_draw_buf_t _draw_buf;

// ---------------------------------------------------------------------------
// Flush callback — LVGL calls this when a region of the framebuffer is ready
// ---------------------------------------------------------------------------
void Lvgl_FlushCB(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf) {
    uint32_t numPixels = (uint32_t)(area->x2 - area->x1 + 1) *
                         (uint32_t)(area->y2 - area->y1 + 1);

    LCD_SetWindow((uint16_t)area->x1, (uint16_t)area->y1,
                  (uint16_t)area->x2, (uint16_t)area->y2);
    LCD_WritePixels((uint16_t*)buf, numPixels);

    lv_disp_flush_ready(drv);
}

// ---------------------------------------------------------------------------
// Tick callback — called from esp_timer ISR context every LVGL_TICK_MS ms
// ---------------------------------------------------------------------------
void Lvgl_TickCB(void* arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
}

// ---------------------------------------------------------------------------
// Lvgl_Init — call once after LCD_Init()
// ---------------------------------------------------------------------------
void Lvgl_Init(void) {
    lv_init();

    // Initialise the double draw buffer
    lv_disp_draw_buf_init(&_draw_buf, _buf1, _buf2, LVGL_BUF_PIXELS);

    // Register the display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res    = LCD_W;   // 320 px (landscape width)
    disp_drv.ver_res    = LCD_H;   // 172 px (landscape height)
    disp_drv.flush_cb   = Lvgl_FlushCB;
    disp_drv.draw_buf   = &_draw_buf;
    disp_drv.full_refresh = 0;     // partial refresh — only dirty regions sent
    lv_disp_drv_register(&disp_drv);

    // Register a dummy input device (no touch on this board variant)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_NONE;
    indev_drv.read_cb = nullptr;
    lv_indev_drv_register(&indev_drv);

    // Start a high-resolution timer to feed lv_tick_inc()
    const esp_timer_create_args_t tick_timer_args = {
        .callback = Lvgl_TickCB,
        .arg      = nullptr,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = nullptr;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000ULL); // µs
}
