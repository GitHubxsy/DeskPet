#include "splash.h"
#include "deskpet_anim.h"
#include "theme.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

#define CANVAS_W     DESKPET_ANIM_W
#define CANVAS_H     DESKPET_ANIM_H

// Background fallback when palette is missing
#define COL_EMPTY    0x0000  // true black (matches THEME_BG)

LV_FONT_DECLARE(font_styrene_28);

static lv_obj_t *splash_container = NULL;
static lv_obj_t *canvas = NULL;
static lv_obj_t *label_status = NULL;     // shown only when no animations loaded
static uint16_t *canvas_buf = NULL;        // 480x480 RGB565 (PSRAM)

static uint16_t cur_anim = 0;
static uint16_t cur_frame = 0;
static uint32_t frame_started_ms = 0;
static uint32_t anim_started_ms = 0;
static bool active = false;

// Opening page loops through the six core DeskPet moods, independent of usage
// rate, battery, voice state, or lamp state.
static const uint8_t SPLASH_LOOP[] = {
    DESKPET_ANIM_IDLE,
    DESKPET_ANIM_HAPPY,
    DESKPET_ANIM_SLEEPY,
    DESKPET_ANIM_CURIOUS,
    DESKPET_ANIM_ANGRY,
    DESKPET_ANIM_LOVE,
};
#define SPLASH_LOOP_COUNT (sizeof(SPLASH_LOOP) / sizeof(SPLASH_LOOP[0]))
#define SPLASH_ANIM_DURATION_MS 3000
static uint8_t splash_loop_idx = 0;

static void render_frame(int anim_idx, int frame_idx) {
    deskpet_render_frame(anim_idx, frame_idx, canvas_buf, CANVAS_W, CANVAS_H);
    if (canvas) lv_obj_invalidate(canvas);
}

static void show_placeholder() {
    // Solid dark background + centered status label.
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas_buf[i] = COL_EMPTY;
    if (canvas) lv_obj_invalidate(canvas);
    if (label_status) lv_obj_clear_flag(label_status, LV_OBJ_FLAG_HIDDEN);
}

void splash_init(lv_obj_t *parent) {
    canvas_buf = (uint16_t*)heap_caps_malloc(CANVAS_W * CANVAS_H * 2, MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        Serial.println("splash: failed to alloc canvas buffer");
        return;
    }

    splash_container = lv_obj_create(parent);
    lv_obj_set_size(splash_container, 480, 480);
    lv_obj_set_pos(splash_container, 0, 0);
    lv_obj_set_style_bg_color(splash_container, THEME_BG, 0);
    lv_obj_set_style_bg_opa(splash_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_container, 0, 0);
    lv_obj_set_style_pad_all(splash_container, 0, 0);
    lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_SCROLLABLE);

    canvas = lv_canvas_create(splash_container);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas);

    // Placeholder label (visible only when no animations are loaded)
    label_status = lv_label_create(splash_container);
    lv_label_set_text(label_status,
        "no animations loaded\n\n"
        "run tools/generate_deskpet_animations.py");
    lv_obj_set_style_text_font(label_status, &font_styrene_28, 0);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0xb0aea5), 0);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_status);

    if (DESKPET_ANIM_COUNT == 0) {
        show_placeholder();
    } else {
        lv_obj_add_flag(label_status, LV_OBJ_FLAG_HIDDEN);
        cur_anim = SPLASH_LOOP[0];
        render_frame(cur_anim, 0);
        frame_started_ms = millis();
        anim_started_ms = frame_started_ms;
    }

    lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
}

void splash_tick(void) {
    if (!active || DESKPET_ANIM_COUNT == 0) return;

    if (millis() - anim_started_ms >= SPLASH_ANIM_DURATION_MS) {
        splash_next();
        return;
    }

    int nframes = deskpet_anim_frames(cur_anim);
    if (nframes == 0) return;

    uint16_t hold = deskpet_frame_hold(cur_anim, cur_frame);
    if (millis() - frame_started_ms >= hold) {
        cur_frame = (cur_frame + 1) % nframes;
        frame_started_ms = millis();
        render_frame(cur_anim, cur_frame);
    }
}

void splash_next(void) {
    if (DESKPET_ANIM_COUNT == 0) return;
    splash_loop_idx = (splash_loop_idx + 1) % SPLASH_LOOP_COUNT;
    cur_anim = SPLASH_LOOP[splash_loop_idx];
    cur_frame = 0;
    frame_started_ms = millis();
    anim_started_ms = frame_started_ms;
    render_frame(cur_anim, 0);
    Serial.printf("splash: -> %s\n", deskpet_anim_name(cur_anim));
}

void splash_pick_for_current_rate(void) {
    if (DESKPET_ANIM_COUNT == 0) return;
    cur_frame = 0;
    frame_started_ms = millis();
    anim_started_ms = frame_started_ms;
    render_frame(cur_anim, 0);
}

bool splash_is_active(void) { return active; }

void splash_show(void) {
    cur_anim = SPLASH_LOOP[splash_loop_idx];
    splash_pick_for_current_rate();
    if (splash_container) lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = true;
}

void splash_hide(void) {
    if (splash_container) lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = false;
}

lv_obj_t* splash_get_root(void) {
    return splash_container;
}

int splash_find_anim(const char* name) {
    return deskpet_find_anim(name);
}

int splash_anim_frames(int anim_idx) {
    return deskpet_anim_frames(anim_idx);
}

uint16_t splash_frame_hold(int anim_idx, int frame_idx) {
    return deskpet_frame_hold(anim_idx, frame_idx);
}

void splash_render_scaled(int anim_idx, int frame_idx, uint16_t* buf, int cell_size) {
    int w = 20 * cell_size;
    deskpet_render_frame(anim_idx, frame_idx, buf, w, w);
}
