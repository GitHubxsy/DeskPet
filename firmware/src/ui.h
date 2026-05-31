#pragma once
#include "data.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_COUNTDOWN,
    SCREEN_CLOCK,
    SCREEN_CHAT,
    SCREEN_LIGHT,
    SCREEN_POMODORO,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_tick_countdown(void);
void ui_tick_clock(void);
void ui_tick_chat(void);
void ui_show_screen(screen_t screen);
void ui_cycle_screen(void);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_battery(int percent, bool charging);

void ui_tick_nudge(void);
void ui_show_nudge(void);
void ui_hide_nudge(void);
bool ui_nudge_is_visible(void);

void ui_tick_pomodoro(void);
void ui_pomodoro_start(void);
void ui_pomodoro_stop(void);
bool ui_pomodoro_is_active(void);
bool ui_pomodoro_is_focus(void);
void ui_light_apply_clawd_rate(int group);
