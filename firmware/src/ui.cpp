#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include "logo.h"
#include "icons.h"
#include "display_cfg.h"

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

// Session rate-limit window length — denominator for the progress ring.
#define SESSION_WINDOW_MIN  300   // 5-hour unified window

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
    splash_init(scr);

    // Splash is touch-toggled — tap anywhere on the splash dismisses it
    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    // Logo on top of all containers (inset for rounded corners)
    logo_img = lv_image_create(scr);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_set_pos(logo_img, MARGIN, TITLE_Y - 10);

    // Battery indicator on top of all containers (upper-right, inset)
    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, SCR_W - 48 - MARGIN, TITLE_Y);
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
        lv_arc_set_value(countdown_arc, 0);
        return;
    }

    int h = remaining_sec / 3600;
    int m = (remaining_sec % 3600) / 60;
    int s = remaining_sec % 60;
    lv_label_set_text_fmt(lbl_countdown_big, "%d:%02d:%02d", h, m, s);

    int total = SESSION_WINDOW_MIN * 60;
    int v = (int)((int64_t)remaining_sec * 1000 / total);
    if (v > 1000) v = 1000;
    lv_arc_set_value(countdown_arc, v);
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

static screen_t prev_non_splash_screen = SCREEN_USAGE;
// Hide the battery indicator on the splash screen — the icon is visually
// noisy over the pixel-art creature animations.
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
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
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:     splash_show(); break;
    case SCREEN_USAGE:      lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_COUNTDOWN:
        lv_obj_clear_flag(countdown_container, LV_OBJ_FLAG_HIDDEN);
        countdown_last_shown = -99999;  // force a re-render on the first tick
        break;
    case SCREEN_CLOCK:
        lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
        clock_last_shown = -1;  // force a re-render on the first tick
        break;
    default: break;
    }

    // Hide the logo overlay on the splash screen so the animation has a clean canvas
    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
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
}
