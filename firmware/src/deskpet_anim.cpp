#include "deskpet_anim.h"
#include <string.h>

const char* deskpet_anim_name(int anim_id) {
    if (anim_id < 0 || anim_id >= DESKPET_ANIM_COUNT) return "";
    return deskpet_anim_assets[anim_id].name;
}

int deskpet_find_anim(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < DESKPET_ANIM_COUNT; ++i) {
        if (strcmp(deskpet_anim_assets[i].name, name) == 0) return i;
    }
    return -1;
}

int deskpet_anim_frames(int anim_id) {
    if (anim_id < 0 || anim_id >= DESKPET_ANIM_COUNT) return 0;
    return deskpet_anim_assets[anim_id].frame_count;
}

uint16_t deskpet_frame_hold(int anim_id, int frame_idx) {
    if (anim_id < 0 || anim_id >= DESKPET_ANIM_COUNT) return 0;
    const deskpet_anim_asset_t* a = &deskpet_anim_assets[anim_id];
    if (frame_idx < 0 || frame_idx >= a->frame_count) return 0;
    return a->holds[frame_idx];
}

bool deskpet_render_frame(int anim_id, int frame_idx, uint16_t* dst, int dst_w, int dst_h) {
    if (!dst || dst_w <= 0 || dst_h <= 0) return false;
    if (anim_id < 0 || anim_id >= DESKPET_ANIM_COUNT) return false;

    const deskpet_anim_asset_t* a = &deskpet_anim_assets[anim_id];
    if (frame_idx < 0 || frame_idx >= a->frame_count) return false;

    const uint8_t* p = a->data + a->offsets[frame_idx];
    const uint8_t* end = p + a->sizes[frame_idx];
    int src_pos = 0;

    if (dst_w == DESKPET_ANIM_W && dst_h == DESKPET_ANIM_H) {
        int dst_pos = 0;
        while (p + 1 < end && dst_pos < dst_w * dst_h) {
            uint8_t run = *p++;
            uint8_t idx = *p++;
            uint16_t color = a->palette[idx % DESKPET_ANIM_PALETTE_SIZE];
            for (int i = 0; i < run && dst_pos < dst_w * dst_h; ++i) {
                dst[dst_pos++] = color;
            }
        }
        return true;
    }

    while (p + 1 < end && src_pos < DESKPET_ANIM_W * DESKPET_ANIM_H) {
        uint8_t run = *p++;
        uint8_t idx = *p++;
        uint16_t color = a->palette[idx % DESKPET_ANIM_PALETTE_SIZE];

        for (int i = 0; i < run && src_pos < DESKPET_ANIM_W * DESKPET_ANIM_H; ++i, ++src_pos) {
            int sx = src_pos % DESKPET_ANIM_W;
            int sy = src_pos / DESKPET_ANIM_W;
            int x0 = (sx * dst_w) / DESKPET_ANIM_W;
            int x1 = ((sx + 1) * dst_w) / DESKPET_ANIM_W;
            int y0 = (sy * dst_h) / DESKPET_ANIM_H;
            int y1 = ((sy + 1) * dst_h) / DESKPET_ANIM_H;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            if (x1 > dst_w) x1 = dst_w;
            if (y1 > dst_h) y1 = dst_h;
            for (int y = y0; y < y1; ++y) {
                uint16_t* row = dst + y * dst_w;
                for (int x = x0; x < x1; ++x) row[x] = color;
            }
        }
    }
    return true;
}
