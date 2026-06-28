#include "ui.h"
#include "splash.h"
#include "ble.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "logo.h"
#include "deskpet_anim.h"
#include "icons.h"
#include "display_cfg.h"
#include "usage_rate.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_64);
LV_FONT_DECLARE(font_mono_96);

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Layout constants for 480x480 (scaled for 2.16" high-DPI + rounded corners) ----
#define SCR_W         480
#define SCR_H         480
#define MARGIN        20    // wider margin for rounded display corners
#define TITLE_Y       30
#define CONTENT_Y     100
#define CONTENT_W     (SCR_W - 2 * MARGIN)   // 440

// ---- Usage screen widgets ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* lbl_anim;

// ---- Countdown screen widgets ----
static lv_obj_t* countdown_container;
static lv_obj_t* countdown_arc;        // inner ring — session reset progress
static lv_obj_t* countdown_week_arc;   // outer ring — weekly consumption
static lv_obj_t* lbl_countdown_caption;
static lv_obj_t* lbl_countdown_big;
static lv_obj_t* lbl_countdown_pct;    // session utilization legend
static lv_obj_t* lbl_countdown_week;   // weekly utilization legend

// Countdown state. countdown_target_ms is the lv_tick value at which the
// session resets; the screen decrements locally each second between polls.
static uint32_t countdown_target_ms = 0;
static bool     countdown_valid = false;
static int      countdown_last_shown = -99999;  // last whole-second value rendered

// ---- Clock screen widgets ----
static lv_obj_t* clock_container;
static lv_obj_t* lbl_clock_date;   // weekday + date
static lv_obj_t* lbl_clock_time;   // big HH:MM
static lv_obj_t* lbl_clock_secs;   // small SS

// Clock state. The host sends its local time (seconds since midnight) on
// each poll; the screen advances locally each second between polls.
static int      clock_base_secs = 0;
static uint32_t clock_base_ms = 0;
static bool     clock_valid = false;
static int      clock_last_shown = -1;

// ---- Chat screen widgets ----
static lv_obj_t* chat_container;
static lv_obj_t* chat_list;          // idle conversation scene
static lv_obj_t* chat_hero_canvas;
static lv_obj_t* chat_status;        // "Thinking..." label
static lv_obj_t* chat_answer_box;    // scrollable answer container
static lv_obj_t* chat_answer_label;
static lv_obj_t* chat_idle_bubble_label;
static uint16_t* chat_hero_buf = nullptr;
static int chat_anim_id = DESKPET_ANIM_IDLE;
static uint8_t chat_anim_frame = 0;
static uint32_t chat_anim_frame_ms = 0;
static int ui_battery_percent = -1;
static bool ui_battery_charging = false;

enum chat_state_t { CHAT_STATE_LIST, CHAT_STATE_WAITING, CHAT_STATE_ANSWER };
static chat_state_t chat_state = CHAT_STATE_LIST;

// Preset questions — the device sends an index; the daemon holds the
// matching prompt text, so this list must stay in sync with the daemon.
static const char* const chat_questions[] = {
    "Give me a Claude Code tip",
    "Tell me a short joke",
    "Explain recursion simply",
    "What can you do?",
    "Lamp soft green",
    "Lamp warm white 60%",
    "Lamp focus",
    "Lamp off",
};
#define CHAT_QUESTION_COUNT ((int)(sizeof(chat_questions) / sizeof(chat_questions[0])))
#define CHAT_VOICE_DEMO_TEXT "Lamp soft green"

// ---- Light screen widgets ----
static lv_obj_t* light_container;
static lv_obj_t* light_on_btn;
static lv_obj_t* light_off_btn;
static lv_obj_t* light_bar;
static lv_obj_t* lbl_light_pct;
static lv_obj_t* lbl_light_state;
static lv_obj_t* lbl_light_status;

struct light_color_preset_t {
    const char* name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t hex;
};

static const light_color_preset_t light_colors[] = {
    {"red",    255,  56,  48, 0xff3830},
    {"green",   64, 201,  92, 0x40c95c},
    {"blue",    10, 132, 255, 0x0a84ff},
    {"yellow", 255, 214,  10, 0xffd60a},
    {"purple", 191,  90, 242, 0xbf5af2},
    {"white",  255, 244, 229, 0xfff4e5},
};
#define LIGHT_COLOR_COUNT (sizeof(light_colors) / sizeof(light_colors[0]))
#define LIGHT_RED_IDX    0
#define LIGHT_GREEN_IDX  1
#define LIGHT_YELLOW_IDX 3
#define LIGHT_WHITE_IDX  5

static lv_obj_t* light_color_btns[LIGHT_COLOR_COUNT];
static bool light_on = false;
static uint8_t light_brightness = 50;
static uint8_t light_color_idx = 5;
static bool quota_limited_seen = false;
static int clawd_light_sent_group = -1;

// ---- Nudge overlay (idle reminder — finger + DeskPet animation, on top of everything) ----
static lv_obj_t* nudge_overlay = nullptr;
static lv_obj_t* nudge_anim_canvas = nullptr;
static lv_obj_t* nudge_finger_canvas = nullptr;
static uint16_t* nudge_anim_buf = nullptr;
static uint16_t* nudge_finger_buf = nullptr;
static int       nudge_anim_idx = -1;
static int       nudge_frame = 0;
static uint32_t  nudge_frame_ms = 0;
static uint16_t  nudge_cur_hold = 200;

#define NUDGE_CELL   12
#define NUDGE_ANIM_W (20 * NUDGE_CELL)  // 240
#define NUDGE_ANIM_H (20 * NUDGE_CELL)  // 240

// Classic cursor hand rotated 90° CW — pointing RIGHT toward DeskPet
#define FINGER_GW    16
#define FINGER_GH    11
#define FINGER_CELL  10
#define FINGER_PX_W  (FINGER_GW * FINGER_CELL)  // 160
#define FINGER_PX_H  (FINGER_GH * FINGER_CELL)  // 110

static const uint16_t finger_palette[] = {
    0x0000,  // 0 = bg (black)
    0x7BEF,  // 1 = outline (gray)
    0xFFFF,  // 2 = fill (white)
};
static const uint8_t finger_grid[FINGER_GH * FINGER_GW] = {
    0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,
    0,0,0,0,1,1,2,2,1,0,0,0,0,0,0,0,
    0,1,1,1,2,2,2,1,1,1,1,1,1,1,1,0,
    1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,
    1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,
    1,2,2,2,2,2,2,2,1,1,1,1,1,1,1,0,
    1,2,2,2,2,2,2,2,2,2,2,1,0,0,0,0,
    1,2,2,2,2,2,2,2,1,1,1,0,0,0,0,0,
    0,1,1,1,1,2,2,2,2,1,0,0,0,0,0,0,
    0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
};

// Cursor hand + animation horizontal layout (centered in 480px)
#define NUDGE_GAP     16
#define NUDGE_TOTAL_W (FINGER_PX_W + NUDGE_GAP + NUDGE_ANIM_W)  // 416
#define NUDGE_LEFT    ((SCR_W - NUDGE_TOTAL_W) / 2)              // 32
#define NUDGE_FINGER_X NUDGE_LEFT
#define NUDGE_ANIM_X  (NUDGE_LEFT + FINGER_PX_W + NUDGE_GAP)    // 208
#define NUDGE_ANIM_Y  ((SCR_H - NUDGE_ANIM_H) / 2)              // 120
#define NUDGE_FINGER_Y ((SCR_H - FINGER_PX_H) / 2)              // 185

// ---- Pomodoro screen widgets ----
static lv_obj_t* pomo_container = nullptr;
static lv_obj_t* pomo_arc = nullptr;
static lv_obj_t* lbl_pomo_title = nullptr;
static lv_obj_t* lbl_pomo_caption = nullptr;
static lv_obj_t* lbl_pomo_big = nullptr;
static lv_obj_t* lbl_pomo_status = nullptr;

enum pomo_phase_t { POMO_IDLE, POMO_FOCUS, POMO_CELEBRATE, POMO_BREAK };
static pomo_phase_t pomo_phase = POMO_IDLE;
static uint32_t pomo_end_ms = 0;
static uint32_t pomo_duration_ms = 0;
static int      pomo_last_shown = -1;

#define POMO_FOCUS_MS     (25 * 60 * 1000UL)
#define POMO_BREAK_MS     ( 5 * 60 * 1000UL)
#define POMO_CELEBRATE_MS 5000UL

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

// Per-frame hold time. Modeled on Claude Code's spinner (Cavalry triangle
// oscillator, range 0..5, period 5s) — turn-around frames (0 and 5) appear
// once per cycle, middle frames twice, so 0/5 read as held longer.
static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 16, 0);
    lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    // Bubble click events up to the screen / usage_container so a tap anywhere
    // on the panel fires the global click handler.
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

// RGB565A8: planar — w*h RGB565 pixels followed by w*h alpha bytes.
// Stride is RGB565-only (w*2); LVGL infers alpha plane location from header.
static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static void init_image_dsc_rgb565(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 2;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, 18, 0);
    lv_obj_set_style_pad_right(lbl, 18, 0);
    lv_obj_set_style_pad_top(lbl, 6, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

// ---- Battery icon initialization ----
static void init_battery_icons(void) {
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen (480x480) ========

#define PANEL_H     150
#define PANEL_GAP   16

// One Session/Weekly panel: big % label, pill on the right, bar, reset label.
// Pill y=1: symmetric inside the panel — panel-outer-top → pill-top equals
// pill-bottom → bar-top (pill height 42 + panel pad_top 12 + bar y=56).
static void make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                             lv_obj_t** out_pct, lv_obj_t** out_pill,
                             lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, MARGIN, y, CONTENT_W, PANEL_H);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, &font_styrene_48, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, 56, CONTENT_W - 32, 24);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, &font_styrene_28, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, 94);
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, SCR_W, SCR_H);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    make_usage_panel(usage_container, CONTENT_Y, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);
    make_usage_panel(usage_container, CONTENT_Y + PANEL_H + PANEL_GAP, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);

    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, &font_mono_32, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -15);
}

// ======== Clock Screen (480x480) ========

static void init_clock_screen(lv_obj_t* scr) {
    clock_container = lv_obj_create(scr);
    lv_obj_set_size(clock_container, SCR_W, SCR_H);
    lv_obj_set_pos(clock_container, 0, 0);
    lv_obj_set_style_bg_opa(clock_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_container, 0, 0);
    lv_obj_set_style_pad_all(clock_container, 0, 0);
    lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(clock_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_clock_date = lv_label_create(clock_container);
    lv_label_set_text(lbl_clock_date, "");
    lv_obj_set_style_text_font(lbl_clock_date, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_clock_date, COL_DIM, 0);
    lv_obj_align(lbl_clock_date, LV_ALIGN_CENTER, 0, -96);

    lbl_clock_time = lv_label_create(clock_container);
    lv_label_set_text(lbl_clock_time, "--:--");
    lv_obj_set_style_text_font(lbl_clock_time, &font_mono_96, 0);
    lv_obj_set_style_text_color(lbl_clock_time, COL_TEXT, 0);
    lv_obj_align(lbl_clock_time, LV_ALIGN_CENTER, 0, -6);

    lbl_clock_secs = lv_label_create(clock_container);
    lv_label_set_text(lbl_clock_secs, "--");
    lv_obj_set_style_text_font(lbl_clock_secs, &font_mono_32, 0);
    lv_obj_set_style_text_color(lbl_clock_secs, COL_DIM, 0);
    lv_obj_align(lbl_clock_secs, LV_ALIGN_CENTER, 0, 66);

    // Attribution
    lv_obj_t* lbl_credit = lv_label_create(clock_container);
    lv_label_set_text(lbl_credit, "built by xieshiyi");
    lv_obj_set_style_text_font(lbl_credit, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_credit, COL_DIM, 0);
    lv_obj_align(lbl_credit, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_obj_add_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Countdown Screen (480x480) ========

// A full-circle progress ring. range 0..1000, starts at 12 o'clock.
static lv_obj_t* make_ring(lv_obj_t* parent, int diameter, int track_w,
                           lv_color_t indic_color, int y_offset) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, y_offset);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, track_w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, track_w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indic_color, LV_PART_INDICATOR);
    return arc;
}

static void init_countdown_screen(lv_obj_t* scr) {
    countdown_container = lv_obj_create(scr);
    lv_obj_set_size(countdown_container, SCR_W, SCR_H);
    lv_obj_set_pos(countdown_container, 0, 0);
    lv_obj_set_style_bg_opa(countdown_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(countdown_container, 0, 0);
    lv_obj_set_style_pad_all(countdown_container, 0, 0);
    lv_obj_clear_flag(countdown_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(countdown_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(countdown_container);
    lv_label_set_text(title, "Session");
    lv_obj_set_style_text_font(title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    // Concentric rings: outer = weekly consumption (fills as quota is used),
    // inner = session reset progress (depletes as the reset approaches).
    countdown_week_arc = make_ring(countdown_container, 376, 14, COL_GREEN, 44);
    countdown_arc      = make_ring(countdown_container, 326, 14, COL_ACCENT, 44);

    lbl_countdown_caption = lv_label_create(countdown_container);
    lv_label_set_text(lbl_countdown_caption, "RESETS IN");
    lv_obj_set_style_text_font(lbl_countdown_caption, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_countdown_caption, COL_DIM, 0);
    lv_obj_align(lbl_countdown_caption, LV_ALIGN_CENTER, 0, -18);

    lbl_countdown_big = lv_label_create(countdown_container);
    lv_label_set_text(lbl_countdown_big, "-:--:--");
    lv_obj_set_style_text_font(lbl_countdown_big, &font_mono_64, 0);
    lv_obj_set_style_text_color(lbl_countdown_big, COL_TEXT, 0);
    lv_obj_align(lbl_countdown_big, LV_ALIGN_CENTER, 0, 44);

    // Ring legends — colour-matched to their ring.
    lbl_countdown_pct = lv_label_create(countdown_container);
    lv_label_set_text(lbl_countdown_pct, "Session --%");
    lv_obj_set_style_text_font(lbl_countdown_pct, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_countdown_pct, COL_ACCENT, 0);
    lv_obj_align(lbl_countdown_pct, LV_ALIGN_CENTER, 0, 90);

    lbl_countdown_week = lv_label_create(countdown_container);
    lv_label_set_text(lbl_countdown_week, "Week --%");
    lv_obj_set_style_text_font(lbl_countdown_week, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_countdown_week, COL_GREEN, 0);
    lv_obj_align(lbl_countdown_week, LV_ALIGN_CENTER, 0, 116);

    lv_obj_add_flag(countdown_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Chat Screen (480x480) ========

static void chat_show_only(lv_obj_t* obj, chat_state_t for_state, chat_state_t cur) {
    if (!obj) return;
    if (for_state == cur) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else                  lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void chat_render_anim_frame(void) {
    if (!chat_hero_buf) return;
    deskpet_render_frame(chat_anim_id, chat_anim_frame,
                         chat_hero_buf, DESKPET_ANIM_W, DESKPET_ANIM_H);
    if (chat_hero_canvas) lv_obj_invalidate(chat_hero_canvas);
}

static void chat_set_anim(int anim_id) {
    if (anim_id < 0 || anim_id >= DESKPET_ANIM_COUNT) anim_id = DESKPET_ANIM_IDLE;
    if (chat_anim_id == anim_id && chat_anim_frame == 0) return;
    chat_anim_id = anim_id;
    chat_anim_frame = 0;
    chat_anim_frame_ms = lv_tick_get();
    chat_render_anim_frame();
}

static int chat_idle_anim_for_power(void) {
    if (ui_battery_charging) return DESKPET_ANIM_CHARGING;
    if (ui_battery_percent >= 0 && ui_battery_percent <= 20) return DESKPET_ANIM_LOW_BATTERY;
    return DESKPET_ANIM_IDLE;
}

static void chat_set_state(chat_state_t s) {
    chat_state = s;
    chat_show_only(chat_list,       CHAT_STATE_LIST,    s);
    chat_show_only(chat_status,     CHAT_STATE_WAITING, s);
    chat_show_only(chat_answer_box, CHAT_STATE_ANSWER,  s);
    if (s == CHAT_STATE_ANSWER && chat_answer_box)
        lv_obj_scroll_to_y(chat_answer_box, 0, LV_ANIM_OFF);
    if (s == CHAT_STATE_LIST) {
        if (chat_idle_bubble_label) lv_label_set_text(chat_idle_bubble_label, "Hold left to talk.");
        chat_set_anim(chat_idle_anim_for_power());
    }
}

static lv_obj_t* chat_make_dot(lv_obj_t* parent, int x, int y, int size, lv_color_t color) {
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

static void chat_make_avatar(lv_obj_t* parent, int x, int y) {
    lv_obj_t* shadow = lv_obj_create(parent);
    lv_obj_set_pos(shadow, x + 12, y + 14);
    lv_obj_set_size(shadow, 240, 156);
    lv_obj_set_style_bg_color(shadow, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(shadow, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(shadow, 8, 0);
    lv_obj_set_style_border_width(shadow, 0, 0);
    lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_set_pos(body, x, y);
    lv_obj_set_size(body, 240, 156);
    lv_obj_set_style_bg_color(body, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(body, 8, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, COL_ACCENT, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = lv_label_create(body);
    lv_label_set_text(name, "DeskPet");
    lv_obj_set_style_text_font(name, &font_styrene_28, 0);
    lv_obj_set_style_text_color(name, COL_TEXT, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 18);

    chat_make_dot(body, 22, 22, 12, COL_GREEN);
    chat_make_dot(body, 206, 22, 12, COL_AMBER);

    lv_obj_t* left_eye = lv_obj_create(body);
    lv_obj_set_pos(left_eye, 58, 78);
    lv_obj_set_size(left_eye, 42, 10);
    lv_obj_set_style_bg_color(left_eye, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(left_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left_eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(left_eye, 0, 0);
    lv_obj_clear_flag(left_eye, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* right_eye = lv_obj_create(body);
    lv_obj_set_pos(right_eye, 140, 78);
    lv_obj_set_size(right_eye, 42, 10);
    lv_obj_set_style_bg_color(right_eye, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(right_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(right_eye, 0, 0);
    lv_obj_clear_flag(right_eye, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        static const int heights[] = {12, 24, 36, 24, 12};
        lv_obj_t* bar = lv_obj_create(body);
        lv_obj_set_pos(bar, 82 + i * 18, 122 - heights[i]);
        lv_obj_set_size(bar, 8, heights[i]);
        lv_obj_set_style_bg_color(bar, COL_GREEN, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void chat_question_click_cb(lv_event_t* e) {
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    lv_label_set_text(chat_answer_label, "");
    ble_send_chat_request((uint8_t)idx);
    chat_set_anim(DESKPET_ANIM_CURIOUS);
    chat_set_state(CHAT_STATE_WAITING);
}

void ui_chat_voice_press(void) {
    if (current_screen != SCREEN_CHAT) return;
    lv_label_set_text(chat_answer_label, "");
    lv_label_set_text(chat_status, "Listening...");
    chat_set_anim(DESKPET_ANIM_LISTENING);
    chat_set_state(CHAT_STATE_WAITING);
}

void ui_chat_voice_release(bool real_audio) {
    if (current_screen != SCREEN_CHAT) return;
    if (ble_get_state() != BLE_STATE_CONNECTED) {
        lv_label_set_text(chat_answer_label, "BLE offline");
        chat_set_anim(DESKPET_ANIM_ANGRY);
        chat_set_state(CHAT_STATE_ANSWER);
        return;
    }
    if (real_audio) {
        lv_label_set_text(chat_status, "Transcribing...");
        chat_set_anim(DESKPET_ANIM_TRANSCRIBING);
        chat_set_state(CHAT_STATE_WAITING);
        return;
    }
    lv_label_set_text(chat_status, "Heard: " CHAT_VOICE_DEMO_TEXT);
    ble_send_text_command(CHAT_VOICE_DEMO_TEXT);
    chat_set_anim(DESKPET_ANIM_TRANSCRIBING);
    chat_set_state(CHAT_STATE_WAITING);
}

void ui_chat_debug_set_anim(int anim_id) {
    if (anim_id < 0 || anim_id >= DESKPET_ANIM_COUNT) return;
    if (current_screen != SCREEN_CHAT) ui_show_screen(SCREEN_CHAT);
    chat_set_state(CHAT_STATE_LIST);
    chat_set_anim(anim_id);
}

// Tap the answer to go back to the question list.
static void chat_screen_click_cb(lv_event_t* e) {
    (void)e;
    if (chat_state == CHAT_STATE_ANSWER) chat_set_state(CHAT_STATE_LIST);
}

static void init_chat_screen(lv_obj_t* scr) {
    chat_container = lv_obj_create(scr);
    lv_obj_set_size(chat_container, SCR_W, SCR_H);
    lv_obj_set_pos(chat_container, 0, 0);
    lv_obj_set_style_bg_opa(chat_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chat_container, 0, 0);
    lv_obj_set_style_pad_all(chat_container, 0, 0);
    lv_obj_clear_flag(chat_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chat_container, chat_screen_click_cb, LV_EVENT_CLICKED, NULL);

    // Idle conversation scene.
    chat_hero_buf = (uint16_t*)heap_caps_malloc(DESKPET_ANIM_W * DESKPET_ANIM_H * 2, MALLOC_CAP_SPIRAM);
    if (chat_hero_buf) {
        chat_hero_canvas = lv_canvas_create(chat_container);
        lv_canvas_set_buffer(chat_hero_canvas, chat_hero_buf,
                             DESKPET_ANIM_W, DESKPET_ANIM_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(chat_hero_canvas, (SCR_W - DESKPET_ANIM_W) / 2, 44);
        chat_render_anim_frame();
    } else {
        chat_hero_canvas = nullptr;
        Serial.println("chat: failed to alloc DeskPet canvas buffer");
    }

    chat_list = lv_obj_create(chat_container);
    lv_obj_set_size(chat_list, CONTENT_W, 92);
    lv_obj_set_pos(chat_list, MARGIN, 370);
    lv_obj_set_style_bg_opa(chat_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chat_list, 0, 0);
    lv_obj_set_style_pad_all(chat_list, 0, 0);
    lv_obj_clear_flag(chat_list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bubble = lv_obj_create(chat_list);
    lv_obj_set_pos(bubble, 48, 0);
    lv_obj_set_size(bubble, 344, 72);
    lv_obj_set_style_bg_color(bubble, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_90, 0);
    lv_obj_set_style_radius(bubble, 8, 0);
    lv_obj_set_style_border_width(bubble, 2, 0);
    lv_obj_set_style_border_color(bubble, COL_BAR_BG, 0);
    lv_obj_set_style_pad_left(bubble, 18, 0);
    lv_obj_set_style_pad_right(bubble, 18, 0);
    lv_obj_set_style_pad_top(bubble, 12, 0);
    lv_obj_set_style_pad_bottom(bubble, 12, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    chat_idle_bubble_label = lv_label_create(bubble);
    lv_label_set_text(chat_idle_bubble_label, "Hold left to talk.");
    lv_obj_set_style_text_font(chat_idle_bubble_label, &font_styrene_24, 0);
    lv_obj_set_style_text_color(chat_idle_bubble_label, COL_TEXT, 0);
    lv_obj_set_width(chat_idle_bubble_label, LV_PCT(100));
    lv_label_set_long_mode(chat_idle_bubble_label, LV_LABEL_LONG_WRAP);
    lv_obj_center(chat_idle_bubble_label);

    // "Thinking..." status
    chat_status = lv_label_create(chat_container);
    lv_label_set_text(chat_status, "Thinking\xE2\x80\xA6");
    lv_obj_set_style_text_font(chat_status, &font_styrene_28, 0);
    lv_obj_set_style_text_color(chat_status, COL_TEXT, 0);
    lv_obj_set_style_bg_color(chat_status, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(chat_status, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chat_status, 8, 0);
    lv_obj_set_style_pad_left(chat_status, 22, 0);
    lv_obj_set_style_pad_right(chat_status, 22, 0);
    lv_obj_set_style_pad_top(chat_status, 14, 0);
    lv_obj_set_style_pad_bottom(chat_status, 14, 0);
    lv_obj_align(chat_status, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_add_flag(chat_status, LV_OBJ_FLAG_HIDDEN);

    // Answer box — scrollable for long replies.
    chat_answer_box = lv_obj_create(chat_container);
    lv_obj_set_size(chat_answer_box, 384, 92);
    lv_obj_set_pos(chat_answer_box, 48, 370);
    lv_obj_set_style_bg_color(chat_answer_box, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(chat_answer_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chat_answer_box, 8, 0);
    lv_obj_set_style_border_width(chat_answer_box, 2, 0);
    lv_obj_set_style_border_color(chat_answer_box, COL_BAR_BG, 0);
    lv_obj_set_style_pad_all(chat_answer_box, 16, 0);
    lv_obj_add_flag(chat_answer_box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(chat_answer_box, LV_OBJ_FLAG_HIDDEN);

    chat_answer_label = lv_label_create(chat_answer_box);
    lv_label_set_text(chat_answer_label, "");
    lv_obj_set_style_text_font(chat_answer_label, &font_styrene_20, 0);
    lv_obj_set_style_text_color(chat_answer_label, COL_TEXT, 0);
    lv_obj_set_width(chat_answer_label, LV_PCT(100));
    lv_label_set_long_mode(chat_answer_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(chat_answer_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_add_flag(chat_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_tick_chat(void) {
    if (current_screen != SCREEN_CHAT) return;
    uint32_t now = lv_tick_get();
    uint16_t hold = deskpet_frame_hold(chat_anim_id, chat_anim_frame);
    if (hold == 0) hold = DESKPET_ANIM_DEFAULT_FRAME_MS;
    if (now - chat_anim_frame_ms >= hold) {
        int nframes = deskpet_anim_frames(chat_anim_id);
        if (nframes <= 0) nframes = 1;
        chat_anim_frame_ms = now;
        chat_anim_frame = (chat_anim_frame + 1) % nframes;
        chat_render_anim_frame();
    }
    if (chat_state == CHAT_STATE_LIST) return;
    if (!ble_chat_has_update()) return;

    if (ble_chat_speech_done()) {
        lv_label_set_text(chat_answer_label, "");
        chat_set_state(CHAT_STATE_LIST);
        return;
    }

    const char* txt = ble_chat_text();
    if (chat_state == CHAT_STATE_WAITING && (txt[0] != '\0' || ble_chat_is_complete())) {
        chat_set_anim(ble_chat_has_error() ? DESKPET_ANIM_ANGRY : DESKPET_ANIM_SPEAKING);
        chat_set_state(CHAT_STATE_ANSWER);
    }
    if (chat_state == CHAT_STATE_ANSWER) {
        lv_label_set_text(chat_answer_label, txt[0] ? txt : "(no response)");
    }
}

// ======== Light Screen (480x480) ========

static void light_refresh_ui(void) {
    lv_obj_set_style_bg_color(light_on_btn, light_on ? COL_GREEN : COL_PANEL, 0);
    lv_obj_set_style_bg_color(light_off_btn, light_on ? COL_PANEL : COL_RED, 0);
    lv_label_set_text(lbl_light_state, light_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(lbl_light_state, light_on ? COL_GREEN : COL_DIM, 0);

    lv_bar_set_value(light_bar, light_brightness, LV_ANIM_ON);
    lv_obj_set_style_bg_color(light_bar, lv_color_hex(light_colors[light_color_idx].hex), LV_PART_INDICATOR);
    lv_label_set_text_fmt(lbl_light_pct, "%u%%", light_brightness);

    for (size_t i = 0; i < LIGHT_COLOR_COUNT; ++i) {
        if (!light_color_btns[i]) continue;
        bool selected = (i == light_color_idx);
        lv_obj_set_style_border_width(light_color_btns[i], selected ? 4 : 2, 0);
        lv_obj_set_style_border_color(light_color_btns[i], selected ? COL_TEXT : COL_BAR_BG, 0);
    }
}

static lv_obj_t* make_light_button(lv_obj_t* parent, int x, int y, int w, int h,
                                   const char* text, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);
    return btn;
}

static bool light_ble_ready(void) {
    if (ble_get_state() == BLE_STATE_CONNECTED) return true;
    lv_label_set_text(lbl_light_status, "BLE offline");
    return false;
}

static void light_on_click_cb(lv_event_t* e) {
    (void)e;
    light_on = true;
    light_refresh_ui();
    if (light_ble_ready()) {
        lv_label_set_text(lbl_light_status, "Sent: on");
        ble_send_light_power(true);
    }
}

static void light_off_click_cb(lv_event_t* e) {
    (void)e;
    light_on = false;
    light_refresh_ui();
    if (light_ble_ready()) {
        lv_label_set_text(lbl_light_status, "Sent: off");
        ble_send_light_power(false);
    }
}

static void light_brightness_click_cb(lv_event_t* e) {
    intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    int next = (delta == 1000) ? 100 : (int)light_brightness + (int)delta;
    if (next < 1) next = 1;
    if (next > 100) next = 100;
    light_brightness = (uint8_t)next;
    light_on = true;
    light_refresh_ui();
    if (light_ble_ready()) {
        lv_label_set_text_fmt(lbl_light_status, "Sent: %u%%", light_brightness);
        ble_send_light_brightness(light_brightness);
    }
}

static void light_apply_auto_scene(const char* status, uint8_t color_idx, uint8_t brightness) {
    if (!light_container || color_idx >= LIGHT_COLOR_COUNT) return;
    if (brightness < 1) brightness = 1;
    if (brightness > 100) brightness = 100;

    const light_color_preset_t* c = &light_colors[color_idx];
    light_on = true;
    light_brightness = brightness;
    light_color_idx = color_idx;
    light_refresh_ui();

    if (ble_get_state() != BLE_STATE_CONNECTED) {
        lv_label_set_text(lbl_light_status, "Auto: BLE offline");
        return;
    }

    lv_label_set_text(lbl_light_status, status);
    ble_send_light_scene(brightness, c->r, c->g, c->b);
}

static void light_flash_quota_alert(void) {
    if (!light_container) return;
    if (ble_get_state() != BLE_STATE_CONNECTED) {
        lv_label_set_text(lbl_light_status, "Alert: BLE offline");
        return;
    }

    lv_label_set_text(lbl_light_status, "Alert: quota");
    ble_send_light_alert_red();
}

void ui_light_apply_clawd_rate(int group) {
    if (pomo_phase != POMO_IDLE) return;
    if (ble_get_state() != BLE_STATE_CONNECTED) {
        clawd_light_sent_group = -1;
        return;
    }
    if (group == clawd_light_sent_group) return;

    switch (group) {
    case 0:
        light_apply_auto_scene("Clawd: idle", LIGHT_WHITE_IDX, 35);
        break;
    case 1:
        light_apply_auto_scene("Clawd: normal", LIGHT_GREEN_IDX, 45);
        break;
    case 2:
        light_apply_auto_scene("Clawd: active", LIGHT_YELLOW_IDX, 60);
        break;
    default:
        light_apply_auto_scene("Clawd: heavy", LIGHT_RED_IDX, 80);
        break;
    }
    clawd_light_sent_group = group;
}

static void light_color_click_cb(lv_event_t* e) {
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (intptr_t)LIGHT_COLOR_COUNT) return;
    light_color_idx = (uint8_t)idx;
    light_on = true;
    light_refresh_ui();
    if (light_ble_ready()) {
        const light_color_preset_t* c = &light_colors[light_color_idx];
        lv_label_set_text_fmt(lbl_light_status, "Sent: %s", c->name);
        ble_send_light_color(c->r, c->g, c->b);
    }
}

static lv_obj_t* make_light_swatch(lv_obj_t* parent, int x, int y, int w, int h, size_t idx) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(light_colors[idx].hex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, COL_BAR_BG, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, light_color_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    return btn;
}

static void init_light_screen(lv_obj_t* scr) {
    light_container = lv_obj_create(scr);
    lv_obj_set_size(light_container, SCR_W, SCR_H);
    lv_obj_set_pos(light_container, 0, 0);
    lv_obj_set_style_bg_opa(light_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(light_container, 0, 0);
    lv_obj_set_style_pad_all(light_container, 0, 0);
    lv_obj_clear_flag(light_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(light_container);
    lv_label_set_text(title, "Light");
    lv_obj_set_style_text_font(title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    light_on_btn = make_light_button(light_container, MARGIN, CONTENT_Y,
                                     (CONTENT_W - 16) / 2, 74,
                                     "On", light_on_click_cb, NULL);
    light_off_btn = make_light_button(light_container,
                                      MARGIN + (CONTENT_W - 16) / 2 + 16, CONTENT_Y,
                                      (CONTENT_W - 16) / 2, 74,
                                      "Off", light_off_click_cb, NULL);

    lv_obj_t* panel = make_panel(light_container, MARGIN, CONTENT_Y + 88, CONTENT_W, 136);

    lbl_light_state = lv_label_create(panel);
    lv_label_set_text(lbl_light_state, "OFF");
    lv_obj_set_style_text_font(lbl_light_state, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_light_state, COL_DIM, 0);
    lv_obj_set_pos(lbl_light_state, 0, 0);

    lbl_light_pct = lv_label_create(panel);
    lv_label_set_text(lbl_light_pct, "50%");
    lv_obj_set_style_text_font(lbl_light_pct, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl_light_pct, COL_TEXT, 0);
    lv_obj_align(lbl_light_pct, LV_ALIGN_TOP_RIGHT, 0, -2);

    light_bar = make_bar(panel, 0, 48, CONTENT_W - 32, 18);
    lv_obj_set_style_bg_color(light_bar, COL_ACCENT, LV_PART_INDICATOR);

    make_light_button(panel, 0, 78, 124, 38, "-10", light_brightness_click_cb, (void*)(intptr_t)-10);
    make_light_button(panel, 142, 78, 124, 38, "+10", light_brightness_click_cb, (void*)(intptr_t)10);
    make_light_button(panel, 284, 78, 124, 38, "100", light_brightness_click_cb, (void*)(intptr_t)1000);

    lv_obj_t* color_panel = make_panel(light_container, MARGIN, CONTENT_Y + 238, CONTENT_W, 90);
    for (size_t i = 0; i < LIGHT_COLOR_COUNT; ++i) {
        light_color_btns[i] = make_light_swatch(color_panel, (int)i * 71, 6, 54, 54, i);
    }

    lbl_light_status = lv_label_create(light_container);
    lv_label_set_text(lbl_light_status, "192.168.5.117");
    lv_obj_set_style_text_font(lbl_light_status, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_light_status, COL_DIM, 0);
    lv_obj_align(lbl_light_status, LV_ALIGN_BOTTOM_MID, 0, -20);

    light_refresh_ui();
    lv_obj_add_flag(light_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Pomodoro Screen (480x480) ========

static void pomo_click_cb(lv_event_t* e) {
    (void)e;
    if (pomo_phase == POMO_IDLE) ui_pomodoro_start();
    else                         ui_toggle_splash();
}

static void init_pomodoro_screen(lv_obj_t* scr) {
    pomo_container = lv_obj_create(scr);
    lv_obj_set_size(pomo_container, SCR_W, SCR_H);
    lv_obj_set_pos(pomo_container, 0, 0);
    lv_obj_set_style_bg_opa(pomo_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pomo_container, 0, 0);
    lv_obj_set_style_pad_all(pomo_container, 0, 0);
    lv_obj_clear_flag(pomo_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pomo_container, pomo_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_pomo_title = lv_label_create(pomo_container);
    lv_label_set_text(lbl_pomo_title, "Focus");
    lv_obj_set_style_text_font(lbl_pomo_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_pomo_title, COL_TEXT, 0);
    lv_obj_align(lbl_pomo_title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    pomo_arc = make_ring(pomo_container, 350, 14, COL_ACCENT, 44);

    lbl_pomo_caption = lv_label_create(pomo_container);
    lv_label_set_text(lbl_pomo_caption, "TAP TO START");
    lv_obj_set_style_text_font(lbl_pomo_caption, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_pomo_caption, COL_DIM, 0);
    lv_obj_align(lbl_pomo_caption, LV_ALIGN_CENTER, 0, -18);

    lbl_pomo_big = lv_label_create(pomo_container);
    lv_label_set_text(lbl_pomo_big, "25:00");
    lv_obj_set_style_text_font(lbl_pomo_big, &font_mono_96, 0);
    lv_obj_set_style_text_color(lbl_pomo_big, COL_TEXT, 0);
    lv_obj_align(lbl_pomo_big, LV_ALIGN_CENTER, 0, 44);

    lbl_pomo_status = lv_label_create(pomo_container);
    lv_label_set_text(lbl_pomo_status, "25 min focus · 5 min break");
    lv_obj_set_style_text_font(lbl_pomo_status, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_pomo_status, COL_DIM, 0);
    lv_obj_align(lbl_pomo_status, LV_ALIGN_CENTER, 0, 104);

    lv_obj_add_flag(pomo_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Nudge overlay (finger → DeskPet animation, tap to open) ========

static void render_finger(uint16_t* buf) {
    for (int gy = 0; gy < FINGER_GH; gy++) {
        uint16_t* first = &buf[gy * FINGER_CELL * FINGER_PX_W];
        for (int gx = 0; gx < FINGER_GW; gx++) {
            uint16_t c = finger_palette[finger_grid[gy * FINGER_GW + gx]];
            for (int dx = 0; dx < FINGER_CELL; dx++) first[gx * FINGER_CELL + dx] = c;
        }
        for (int dy = 1; dy < FINGER_CELL; dy++)
            memcpy(first + dy * FINGER_PX_W, first, FINGER_PX_W * 2);
    }
}

static void nudge_click_cb(lv_event_t* e) {
    (void)e;
    ble_send_open_app();
    ui_hide_nudge();
    ui_pomodoro_start();
}

static void init_nudge_overlay(lv_obj_t* scr) {
    nudge_anim_idx = splash_find_anim("curious");

    nudge_anim_buf = (uint16_t*)heap_caps_malloc(NUDGE_ANIM_W * NUDGE_ANIM_H * 2, MALLOC_CAP_SPIRAM);
    nudge_finger_buf = (uint16_t*)heap_caps_malloc(FINGER_PX_W * FINGER_PX_H * 2, MALLOC_CAP_SPIRAM);
    if (!nudge_anim_buf || !nudge_finger_buf) {
        Serial.println("nudge: PSRAM alloc failed");
        return;
    }

    nudge_overlay = lv_obj_create(scr);
    lv_obj_set_size(nudge_overlay, SCR_W, SCR_H);
    lv_obj_set_pos(nudge_overlay, 0, 0);
    lv_obj_set_style_bg_color(nudge_overlay, COL_BG, 0);
    lv_obj_set_style_bg_opa(nudge_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nudge_overlay, 0, 0);
    lv_obj_clear_flag(nudge_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(nudge_overlay, nudge_click_cb, LV_EVENT_CLICKED, NULL);

    // Finger canvas (static, rendered once)
    render_finger(nudge_finger_buf);
    nudge_finger_canvas = lv_canvas_create(nudge_overlay);
    lv_canvas_set_buffer(nudge_finger_canvas, nudge_finger_buf,
                         FINGER_PX_W, FINGER_PX_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(nudge_finger_canvas, NUDGE_FINGER_X, NUDGE_FINGER_Y);

    // DeskPet animation canvas
    nudge_anim_canvas = lv_canvas_create(nudge_overlay);
    lv_canvas_set_buffer(nudge_anim_canvas, nudge_anim_buf,
                         NUDGE_ANIM_W, NUDGE_ANIM_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(nudge_anim_canvas, NUDGE_ANIM_X, NUDGE_ANIM_Y);

    lv_obj_add_flag(nudge_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_tick_nudge(void) {
    if (!nudge_overlay || lv_obj_has_flag(nudge_overlay, LV_OBJ_FLAG_HIDDEN)) return;
    if (nudge_anim_idx < 0 || !nudge_anim_buf) return;

    int nframes = splash_anim_frames(nudge_anim_idx);
    if (nframes == 0) return;

    if (millis() - nudge_frame_ms >= nudge_cur_hold) {
        nudge_frame = (nudge_frame + 1) % nframes;
        splash_render_scaled(nudge_anim_idx, nudge_frame, nudge_anim_buf, NUDGE_CELL);
        nudge_cur_hold = splash_frame_hold(nudge_anim_idx, nudge_frame);
        nudge_frame_ms = millis();
        lv_obj_invalidate(nudge_anim_canvas);
    }
}

// ======== Public API ========

void ui_init(void) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Logo (shared, always visible, on top of all containers)
    // Logo is RGB565A8 (planar: w*h RGB565 then w*h alpha) so it composites
    // cleanly against whatever bg is behind it.
    init_icon_dsc_rgb565a8(&logo_dsc, LOGO_WIDTH, LOGO_HEIGHT, logo_data);

    // Initialize battery icon descriptors
    init_battery_icons();

    init_usage_screen(scr);
    init_countdown_screen(scr);
    init_clock_screen(scr);
    init_chat_screen(scr);
    init_light_screen(scr);
    init_pomodoro_screen(scr);
    splash_init(scr);

    // Logo on top of all containers (inset for rounded corners)
    logo_img = lv_image_create(scr);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_set_pos(logo_img, MARGIN, TITLE_Y - 10);

    // Battery indicator on top of all containers (upper-right, inset)
    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, SCR_W - 48 - MARGIN, TITLE_Y);

    // Nudge overlay — created last so it sits on top of everything
    init_nudge_overlay(scr);
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;

    int s_pct = (int)(data->session_pct + 0.5f);

    // Usage screen
    lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);

    char buf[48];
    format_reset_time(data->session_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_session_reset, buf);

    int w_pct = (int)(data->weekly_pct + 0.5f);
    lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
    lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);

    format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_weekly_reset, buf);

    // Countdown screen — anchor the session reset target for live ticking.
    if (data->session_reset_mins >= 0) {
        countdown_target_ms = lv_tick_get() + (uint32_t)data->session_reset_mins * 60000UL;
        countdown_valid = true;
    } else {
        countdown_valid = false;
    }
    countdown_last_shown = -99999;  // force the next tick to re-render

    lv_label_set_text_fmt(lbl_countdown_pct, "Session %d%%", s_pct);
    lv_label_set_text_fmt(lbl_countdown_week, "Week %d%%", w_pct);
    int sess_v = s_pct * 10;
    if (sess_v > 1000) sess_v = 1000;
    lv_arc_set_value(countdown_arc, sess_v);
    int week_v = w_pct * 10;
    if (week_v > 1000) week_v = 1000;
    lv_arc_set_value(countdown_week_arc, week_v);

    // Clock screen — anchor the host time for local ticking.
    if (data->time_of_day >= 0) {
        clock_base_secs = data->time_of_day;
        clock_base_ms = lv_tick_get();
        clock_valid = true;
    } else {
        clock_valid = false;
    }
    clock_last_shown = -1;
    if (data->date_str[0]) lv_label_set_text(lbl_clock_date, data->date_str);

    bool limited = (strcmp(data->status, "limited") == 0) ||
                   data->session_pct >= 99.5f ||
                   data->weekly_pct >= 99.5f;
    if (limited && !quota_limited_seen) {
        light_flash_quota_alert();
    }
    quota_limited_seen = limited;
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;

    uint32_t now = lv_tick_get();

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms >= spinner_ms[anim_spinner_idx]) {
        anim_last_ms = now;
        anim_phase = (anim_phase + 1) % SPINNER_PHASES;
        anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                        : (SPINNER_PHASES - anim_phase);

        static char buf[80];
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[anim_spinner_idx],
                 anim_messages[anim_msg_idx]);
        lv_label_set_text(lbl_anim, buf);
    }
}

void ui_tick_countdown(void) {
    if (current_screen != SCREEN_COUNTDOWN) return;

    int remaining_sec;
    if (!countdown_valid) {
        remaining_sec = -1;
    } else {
        int32_t diff = (int32_t)(countdown_target_ms - lv_tick_get());
        remaining_sec = (diff > 0) ? (diff / 1000) : 0;
    }

    if (remaining_sec == countdown_last_shown) return;
    countdown_last_shown = remaining_sec;

    if (remaining_sec < 0) {
        lv_label_set_text(lbl_countdown_big, "-:--:--");
        return;
    }

    int h = remaining_sec / 3600;
    int m = (remaining_sec % 3600) / 60;
    int s = remaining_sec % 60;
    lv_label_set_text_fmt(lbl_countdown_big, "%d:%02d:%02d", h, m, s);
}

void ui_tick_clock(void) {
    if (current_screen != SCREEN_CLOCK) return;

    if (!clock_valid) {
        if (clock_last_shown == -2) return;
        clock_last_shown = -2;
        lv_label_set_text(lbl_clock_time, "--:--");
        lv_label_set_text(lbl_clock_secs, "--");
        return;
    }

    uint32_t elapsed = (lv_tick_get() - clock_base_ms) / 1000;
    int cur = (clock_base_secs + (int)elapsed) % 86400;
    if (cur == clock_last_shown) return;
    clock_last_shown = cur;

    lv_label_set_text_fmt(lbl_clock_time, "%02d:%02d", cur / 3600, (cur % 3600) / 60);
    lv_label_set_text_fmt(lbl_clock_secs, "%02d", cur % 60);
}

static screen_t prev_non_splash_screen = SCREEN_CHAT;
// Hide the battery indicator on the splash screen — the icon is visually
// noisy over the pixel-art creature animations.
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH || current_screen == SCREEN_CHAT) {
        lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    }
}

// LVGL handles click debouncing internally. A tap anywhere on any
// non-splash screen toggles the splash on/off.
static void global_click_cb(lv_event_t* e) {
    (void)e;
    ui_toggle_splash();
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(countdown_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(chat_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(light_container, LV_OBJ_FLAG_HIDDEN);
    if (pomo_container) lv_obj_add_flag(pomo_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:
        splash_show();
        break;
    case SCREEN_USAGE:      lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_COUNTDOWN:
        lv_obj_clear_flag(countdown_container, LV_OBJ_FLAG_HIDDEN);
        countdown_last_shown = -99999;  // force a re-render on the first tick
        break;
    case SCREEN_CLOCK:
        lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
        clock_last_shown = -1;  // force a re-render on the first tick
        break;
    case SCREEN_CHAT:
        lv_obj_clear_flag(chat_container, LV_OBJ_FLAG_HIDDEN);
        ble_chat_reset();
        lv_label_set_text(chat_answer_label, "");
        chat_set_state(CHAT_STATE_LIST);
        break;
    case SCREEN_LIGHT:
        lv_obj_clear_flag(light_container, LV_OBJ_FLAG_HIDDEN);
        break;
    case SCREEN_POMODORO:
        lv_obj_clear_flag(pomo_container, LV_OBJ_FLAG_HIDDEN);
        pomo_last_shown = -1;
        break;
    default: break;
    }

    // Hide the logo overlay on Splash and Chat so those screens keep a clean canvas.
    if (logo_img) {
        if (screen == SCREEN_SPLASH || screen == SCREEN_CHAT) {
            lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_battery_visibility();
}

void ui_cycle_screen(void) {
    screen_t next;
    switch (current_screen) {
    case SCREEN_USAGE:     next = SCREEN_COUNTDOWN; break;
    case SCREEN_COUNTDOWN: next = SCREEN_CLOCK; break;
    case SCREEN_CLOCK:     next = SCREEN_CHAT; break;
    case SCREEN_CHAT:      next = SCREEN_SPLASH; break;
    case SCREEN_LIGHT:     next = SCREEN_POMODORO; break;
    case SCREEN_POMODORO:  next = SCREEN_USAGE; break;
    default:               next = SCREEN_USAGE; break;
    }
    ui_show_screen(next);
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_battery(int percent, bool charging) {
    ui_battery_percent = percent;
    ui_battery_charging = charging;

    int idx;
    if (charging) {
        idx = 4;  // charging icon
    } else if (percent < 0) {
        idx = 0;  // no battery / unknown
    } else if (percent <= 10) {
        idx = 0;  // empty
    } else if (percent <= 35) {
        idx = 1;  // low
    } else if (percent <= 75) {
        idx = 2;  // medium
    } else {
        idx = 3;  // full
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();

    if (current_screen == SCREEN_CHAT && chat_state == CHAT_STATE_LIST) {
        chat_set_anim(chat_idle_anim_for_power());
    }
}

static void finger_anim_xcb(void* var, int32_t val) {
    lv_obj_set_x((lv_obj_t*)var, val);
}

void ui_show_nudge(void) {
    if (!nudge_overlay) return;
    lv_obj_clear_flag(nudge_overlay, LV_OBJ_FLAG_HIDDEN);

    // Render first frame
    nudge_frame = 0;
    nudge_frame_ms = millis();
    if (nudge_anim_idx >= 0 && nudge_anim_buf) {
        splash_render_scaled(nudge_anim_idx, 0, nudge_anim_buf, NUDGE_CELL);
        nudge_cur_hold = splash_frame_hold(nudge_anim_idx, 0);
        lv_obj_invalidate(nudge_anim_canvas);
    }

    // Start finger oscillation
    if (nudge_finger_canvas) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, nudge_finger_canvas);
        lv_anim_set_values(&a, NUDGE_FINGER_X - 10, NUDGE_FINGER_X + 10);
        lv_anim_set_duration(&a, 700);
        lv_anim_set_playback_duration(&a, 700);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, finger_anim_xcb);
        lv_anim_start(&a);
    }
}

void ui_hide_nudge(void) {
    if (!nudge_overlay) return;
    lv_obj_add_flag(nudge_overlay, LV_OBJ_FLAG_HIDDEN);
    if (nudge_finger_canvas)
        lv_anim_delete(nudge_finger_canvas, finger_anim_xcb);
}

bool ui_nudge_is_visible(void) {
    if (!nudge_overlay) return false;
    return !lv_obj_has_flag(nudge_overlay, LV_OBJ_FLAG_HIDDEN);
}

// ======== Pomodoro public API ========

static void pomo_set_focus_ui(void) {
    lv_label_set_text(lbl_pomo_title, "Focus");
    lv_label_set_text(lbl_pomo_caption, "REMAINING");
    lv_label_set_text(lbl_pomo_status, "stay focused");
    lv_obj_set_style_arc_color(pomo_arc, COL_ACCENT, LV_PART_INDICATOR);
    lv_arc_set_value(pomo_arc, 0);
}

static void pomo_set_break_ui(void) {
    lv_label_set_text(lbl_pomo_title, "Break");
    lv_label_set_text(lbl_pomo_caption, "REMAINING");
    lv_label_set_text(lbl_pomo_status, "take a rest");
    lv_obj_set_style_arc_color(pomo_arc, COL_GREEN, LV_PART_INDICATOR);
    lv_arc_set_value(pomo_arc, 0);
}

static void pomo_set_idle_ui(void) {
    lv_label_set_text(lbl_pomo_title, "Focus");
    lv_label_set_text(lbl_pomo_caption, "TAP TO START");
    lv_label_set_text(lbl_pomo_big, "25:00");
    lv_label_set_text(lbl_pomo_status, "25 min focus \xC2\xB7 5 min break");
    lv_obj_set_style_arc_color(pomo_arc, COL_ACCENT, LV_PART_INDICATOR);
    lv_arc_set_value(pomo_arc, 0);
}

void ui_pomodoro_start(void) {
    pomo_phase = POMO_FOCUS;
    pomo_duration_ms = POMO_FOCUS_MS;
    pomo_end_ms = millis() + pomo_duration_ms;
    pomo_last_shown = -1;
    pomo_set_focus_ui();
    clawd_light_sent_group = -1;
    light_apply_auto_scene("Scene: focus", LIGHT_WHITE_IDX, 100);
    ui_show_screen(SCREEN_POMODORO);
}

void ui_pomodoro_stop(void) {
    pomo_phase = POMO_IDLE;
    pomo_last_shown = -1;
    pomo_set_idle_ui();
    clawd_light_sent_group = -1;
    light_apply_auto_scene("Scene: idle", LIGHT_WHITE_IDX, 45);
}

bool ui_pomodoro_is_active(void) {
    return pomo_phase != POMO_IDLE;
}

bool ui_pomodoro_is_focus(void) {
    return pomo_phase == POMO_FOCUS;
}

void ui_tick_pomodoro(void) {
    if (pomo_phase == POMO_IDLE) return;

    if (pomo_phase == POMO_CELEBRATE) {
        if (millis() >= pomo_end_ms) {
            pomo_phase = POMO_BREAK;
            pomo_duration_ms = POMO_BREAK_MS;
            pomo_end_ms = millis() + pomo_duration_ms;
            pomo_last_shown = -1;
            pomo_set_break_ui();
            clawd_light_sent_group = -1;
            light_apply_auto_scene("Scene: break", LIGHT_GREEN_IDX, 40);
            ui_show_screen(SCREEN_POMODORO);
        }
        return;
    }

    int32_t diff = (int32_t)(pomo_end_ms - millis());
    int remaining_sec = (diff > 0) ? (diff / 1000) : 0;

    if (remaining_sec == pomo_last_shown) return;
    pomo_last_shown = remaining_sec;

    if (remaining_sec > 0) {
        int m = remaining_sec / 60;
        int s = remaining_sec % 60;
        lv_label_set_text_fmt(lbl_pomo_big, "%02d:%02d", m, s);

        uint32_t elapsed = pomo_duration_ms - (uint32_t)(diff > 0 ? diff : 0);
        int arc_val = (int)((uint64_t)elapsed * 1000 / pomo_duration_ms);
        if (arc_val > 1000) arc_val = 1000;
        lv_arc_set_value(pomo_arc, arc_val);
        return;
    }

    // Phase complete
    lv_arc_set_value(pomo_arc, 1000);

    if (pomo_phase == POMO_FOCUS) {
        pomo_phase = POMO_CELEBRATE;
        pomo_end_ms = millis() + POMO_CELEBRATE_MS;
        lv_label_set_text(lbl_pomo_big, "00:00");
        ui_show_screen(SCREEN_SPLASH);
    } else if (pomo_phase == POMO_BREAK) {
        pomo_phase = POMO_IDLE;
        pomo_set_idle_ui();
        clawd_light_sent_group = -1;
        light_apply_auto_scene("Scene: idle", LIGHT_WHITE_IDX, 45);
    }
}
