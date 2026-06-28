# DeskPet

DeskPet is a desk-side voice companion for Claude Code: a tiny animated pet on a
480×480 AMOLED screen that listens through an ESP32-S3 microphone, talks back
through your Mac, controls a Yeelight smart color bulb, and still keeps the
original Claude usage dashboard one middle-button press away.

It runs on a [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm?&aff_id=149786),
pairs with a host daemon over Bluetooth LE, and uses DeskPet MP4 animations for
idle, listening, transcribing, speaking, charging, low-battery, and mood states.

DeskPet introduction page: [https://deskpet.pages.dev](https://deskpet.pages.dev/)

| Opening Loop | Usage |
| :----------: | :---: |
| ![Splash](screenshots/splash.png) | ![Usage](screenshots/usage.png) |

## What Changed

This project has been upgraded from a Claude usage monitor into a voice-first
desktop pet:

- **Voice conversation as the main flow**: left button enters Chat; hold left to
  record; release to transcribe; DeskPet answers with speech.
- **Smart branching state machine**:
  `Idle -> Listening -> Transcribing -> Speaking -> Idle`.
- **Lamp command branch**:
  `Idle -> Listening -> Transcribing -> Lamp done -> Idle`.
- **Short-recording recovery**:
  `Voice too short` stays visible for 3 seconds, then returns to Idle.
- **DeskPet MP4 animation system**: all animations under `logo/DeskPet-mp4/`
  are generated into compressed firmware assets and rendered on device.
- **Natural-language Yeelight control**: say things like "把台灯调成柔和绿光".
- **AI fallback chat**: non-lamp voice input is answered through DeepSeek, then
  spoken with DashScope TTS.
- **Claude usage dashboard remains**: usage, countdown, clock, Pomodoro, light,
  and nudge screens still exist behind the main DeskPet experience.

## DeskPet States

Opening page cycles through six core moods:

`idle -> happy -> sleepy -> curious -> angry -> love`

Voice and power states use the rest of the catalog:

- `listening` while the left button is held
- `transcribing` after release while ASR runs
- `speaking` while the host plays TTS
- `charging` when the battery is charging on the Chat idle screen
- `low-battery` when the battery is low on the Chat idle screen

The animation source catalog lives here:

```text
logo/DeskPet-mp4/
01-idle.mp4
02-happy.mp4
03-sleepy.mp4
04-curious.mp4
05-angry.mp4
06-love.mp4
07-charging.mp4
08-low-battery.mp4
09-speaking.mp4
10-listening.mp4
11-transcribing.mp4
```

Regenerate firmware animation assets after replacing MP4 files:

```bash
python3 tools/generate_deskpet_animations.py
pio run -d firmware
```

## Screens

Press the middle (PWR) button to move through secondary screens. From Chat,
the middle button exits back to the opening DeskPet splash page.

| Countdown | Clock | Pomodoro | Nudge |
| :-------: | :---: | :------: | :---: |
| ![Countdown](screenshots/countdown.png) | ![Clock](screenshots/clock.png) | ![Pomodoro](screenshots/pomodoro.png) | ![Nudge](screenshots/nudge.png) |
| Session reset countdown | Host time + date | Focus timer + break flow | Idle reminder |

## Voice Flow

DeskPet sends microphone audio over BLE to the host daemon:

1. Left button down: firmware switches to `listening` and streams PCM16 chunks.
2. Left button up: firmware switches to `transcribing` and sends end-of-audio.
3. The daemon transcribes with DashScope ASR.
4. If the text is a lamp command, the daemon executes it and sends `speech done`.
5. Otherwise the daemon asks DeepSeek for a short answer.
6. The answer is sent to the screen, DashScope TTS plays it on the Mac, then the
   daemon sends `speech done`.
7. Firmware returns to Idle.

## Yeelight Lamp Control

The lamp is a Yeelight smart color bulb. The daemon can control power,
brightness, RGB color, and scene-like presets through natural language.

Examples:

```bash
~/.clawdmeter-daemon/lightctl "把台灯调成柔和绿光"
~/.clawdmeter-daemon/lightctl "台灯暖白高亮"
~/.clawdmeter-daemon/lightctl "关闭台灯"
```

Voice commands follow the same path. If the transcript is recognized as a lamp
command, DeskPet skips AI chat and returns to Idle right after the lamp finishes.

## Hardware

- [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm?&aff_id=149786)
  — ESP32-S3R8, 480×480 AMOLED, capacitive touch, PMU, IMU, microphone ADC
- ES7210 microphone path on the board
- Yeelight smart color bulb for lamp control
- USB-C cable for flashing and charging
- 3.7V Li-Po battery (MX1.25, optional)

## Prerequisites

- macOS for the current voice/daemon stack
- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
- Python 3 with venv support
- Bluetooth permission for the daemon runtime
- Reminders permission if you use reminder-related daemon features
- Claude Code credentials for usage polling
- DashScope API key for ASR/TTS
- DeepSeek API key for AI chat fallback
- Yeelight bulb reachable on the local network

## macOS Installation

### Flash The Firmware

```bash
./flash-mac.sh
./flash-mac.sh /dev/cu.usbmodem1101
```

Or directly:

```bash
pio run -d firmware
pio run -d firmware -t upload --upload-port /dev/cu.usbmodem21101
```

### Pair The Device

Open **System Settings -> Bluetooth** and connect to `Clawdmeter` / `DeskPet`
when it appears.

### Install The Daemon

```bash
./install-mac.sh
```

The installer copies runtime files into:

```text
~/.clawdmeter-daemon/
```

Important: launchd runs:

```text
~/.clawdmeter-daemon/.venv/bin/python ~/.clawdmeter-daemon/claude_usage_daemon.py
```

So when changing daemon behavior on this Mac, update the runtime copy and restart:

```bash
launchctl kickstart -k gui/$(id -u)/com.user.claude-usage-daemon
tail -F ~/Library/Logs/claude-usage-daemon.out.log
```

## Physical Buttons

| Button | GPIO | Function |
| ------ | ---- | -------- |
| **Left** | GPIO 0 | Enter Chat; hold to record voice; release to transcribe |
| **Middle** | AXP2101 PKEY | Cycle screens; from Chat exits to Splash; stops active Pomodoro |
| **Right** | GPIO 18 | Send Shift+Tab as a BLE HID shortcut |

## BLE Protocol

DeskPet exposes a custom GATT service alongside HID keyboard support:

| Characteristic | UUID |
| -------------- | ---- |
| Data Service | `4c41555a-4465-7669-6365-000000000001` |
| RX, host writes to device | `4c41555a-4465-7669-6365-000000000002` |
| TX, device notifies host | `4c41555a-4465-7669-6365-000000000003` |
| REQ, device notifies host | `4c41555a-4465-7669-6365-000000000004` |

Selected protocol tags:

| Direction | Tag | Meaning |
| --------- | --- | ------- |
| Device -> Host | `0x50` | Voice start |
| Device -> Host | `0x51` | Voice PCM16 chunk |
| Device -> Host | `0x52` | Voice end |
| Device -> Host | `0x40` | Text command |
| Host -> Device | `0x02` | Chat text chunk |
| Host -> Device | `0x04` | Chat text complete |
| Host -> Device | `0x05` | Speech/action done, return to Idle |
| Host -> Device | `0x15` | Error text |

Usage JSON is still written to RX for the dashboard:

```json
{ "s": 45, "sr": 120, "w": 28, "wr": 7200, "st": "allowed", "t": 52380, "d": "Sat, 17 May 2026", "ok": true }
```

## Development Notes

- Firmware UI is LVGL 9 on a 480×480 AMOLED.
- Display rotation is CPU-side pixel remapping; the CO5300 cannot rotate axes
  through MADCTL.
- MP4 animations are quantized to 64-color RGB565 palettes and RLE-compressed in
  `firmware/src/deskpet_animations.h`.
- Runtime animation decoding lives in `firmware/src/deskpet_anim.cpp`.
- Serial command `screenshot` dumps the framebuffer for visual QA.
- Serial command `deskpet 0` through `deskpet 10` previews animation states.

## Credits

- DeskPet visual assets in `logo/`
- [Lucide](https://lucide.dev) icon set (MIT) for UI glyphs
- Anthropic brand fonts (Tiempos Text, Styrene B)
- macOS host pieces were originally ported by
  [Chris Davidson (@lorddavidson)](https://github.com/lorddavidson)

## License

This repository includes third-party fonts and visual assets. The code is shared
for project development, but no formal open-source license is currently declared.
