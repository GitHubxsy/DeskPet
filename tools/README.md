# DeskPet animation tools

Pipeline for getting DeskPet MP4 animations onto the device.

## Generate

```bash
python3 generate_deskpet_animations.py
```

Reads all MP4 files in `logo/DeskPet-mp4/` and writes
`firmware/src/deskpet_animations.h`.

Each animation is sampled into 320×320 frames, quantized to a per-animation
64-color RGB565 palette, and RLE-encoded as `(run, palette_index)` byte pairs.
The firmware decodes frames through `deskpet_anim.cpp`.

The filename prefix controls ordering. For example, `01-idle.mp4` becomes
`DESKPET_ANIM_IDLE`, and `08-low-battery.mp4` becomes
`DESKPET_ANIM_LOW_BATTERY`.

## Current Catalog

- `01-idle.mp4`
- `02-happy.mp4`
- `03-sleepy.mp4`
- `04-curious.mp4`
- `05-angry.mp4`
- `06-love.mp4`
- `07-charging.mp4`
- `08-low-battery.mp4`
- `09-speaking.mp4`
- `10-listening.mp4`
- `11-transcribing.mp4`

## Re-running

The generator is idempotent. Re-run it after adding, deleting, or replacing MP4
files, then rebuild firmware.

## Legacy

Older claudepix scraper/converter scripts may exist in this directory for
history, but the firmware animation path now uses `logo/DeskPet-mp4/` only.
