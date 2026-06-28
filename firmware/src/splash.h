#pragma once
#include <stdint.h>
#include <lvgl.h>

// Initialize splash module. Creates the canvas widget inside `parent` and
// allocates the DeskPet animation buffer (PSRAM).
void splash_init(lv_obj_t *parent);

// Advance animation frame if hold time elapsed. Call from main loop.
void splash_tick(void);

// Cycle to the next animation in the catalog.
void splash_next(void);

// Show/hide the splash container.
void splash_show(void);
void splash_hide(void);

// Restart the current opening animation. Kept for older call sites; the
// opening page no longer chooses animations from usage-rate groups.
void splash_pick_for_current_rate(void);

// True when splash is currently rendering (used to gate re-picks).
bool splash_is_active(void);

// Root container (so ui.cpp can attach a click event).
lv_obj_t* splash_get_root(void);

// Look up an animation by name. Returns index or -1.
int splash_find_anim(const char* name);
int splash_anim_frames(int anim_idx);
uint16_t splash_frame_hold(int anim_idx, int frame_idx);
// Render a frame into an external buffer at an arbitrary cell size.
void splash_render_scaled(int anim_idx, int frame_idx, uint16_t* buf, int cell_size);
