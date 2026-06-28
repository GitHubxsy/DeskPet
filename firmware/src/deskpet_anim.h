#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "deskpet_animations.h"

const char* deskpet_anim_name(int anim_id);
int deskpet_find_anim(const char* name);
int deskpet_anim_frames(int anim_id);
uint16_t deskpet_frame_hold(int anim_id, int frame_idx);

// Decode one DeskPet frame into an RGB565 destination buffer. The destination
// may be any size; nearest-neighbor scaling keeps this cheap on the ESP32-S3.
bool deskpet_render_frame(int anim_id, int frame_idx, uint16_t* dst, int dst_w, int dst_h);
