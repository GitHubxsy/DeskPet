#!/bin/bash
# macOS installer for Clawdmeter daemon (Python + bleak + launchd).
# Mirrors install.sh but uses LaunchAgents instead of systemd user units.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_LABEL="com.user.claude-usage-daemon"
PLIST_SRC="$SCRIPT_DIR/daemon/$SERVICE_LABEL.plist"
PLIST_DST="$HOME/Library/LaunchAgents/$SERVICE_LABEL.plist"
APP_DIR="$HOME/.clawdmeter-daemon"
VENV_DIR="$APP_DIR/.venv"
DAEMON_PY="$APP_DIR/claude_usage_daemon.py"
LIGHTCTL="$APP_DIR/lightctl"
LOG_DIR="$HOME/Library/Logs"
LOG_OUT="$LOG_DIR/claude-usage-daemon.out.log"
LOG_ERR="$LOG_DIR/claude-usage-daemon.err.log"

escape_sed_replacement() {
    printf '%s' "$1" | sed 's/[&|\\]/\\&/g'
}

echo "=== Clawdmeter macOS install ==="
echo ""

echo "[1/6] Checking prerequisites..."
for cmd in curl; do
    command -v "$cmd" >/dev/null || { echo "Error: $cmd is required"; exit 1; }
done
PYTHON_BOOTSTRAP="${CLAWDMETER_PYTHON:-}"
if [ -z "$PYTHON_BOOTSTRAP" ]; then
    for candidate in /usr/bin/python3 /opt/homebrew/bin/python3 python3; do
        if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import venv, xml.parsers.expat' >/dev/null 2>&1; then
            PYTHON_BOOTSTRAP="$(command -v "$candidate")"
            break
        fi
    done
fi
if [ -z "$PYTHON_BOOTSTRAP" ]; then
    echo "Error: no usable Python with venv and pyexpat support found"
    exit 1
fi
if [ ! -f "$HOME/.claude/.credentials.json" ]; then
    echo "Warning: ~/.claude/.credentials.json not found."
    echo "  Sign in via Claude Code first, then re-run this installer."
    echo "  Continuing anyway — the daemon will retry on each poll."
fi
echo "  OK"
echo "  Python: $PYTHON_BOOTSTRAP"
echo ""

echo "[2/6] Installing daemon files to $APP_DIR ..."
mkdir -p "$APP_DIR"
install -m 755 "$SCRIPT_DIR/daemon/claude_usage_daemon.py" "$APP_DIR/claude_usage_daemon.py"
install -m 644 "$SCRIPT_DIR/daemon/deepseek_chat.py" "$APP_DIR/deepseek_chat.py"
install -m 644 "$SCRIPT_DIR/daemon/light_control.py" "$APP_DIR/light_control.py"
install -m 644 "$SCRIPT_DIR/daemon/lightctl.py" "$APP_DIR/lightctl.py"
install -m 755 "$SCRIPT_DIR/daemon/lightctl" "$LIGHTCTL"
install -m 644 "$SCRIPT_DIR/daemon/voice_control.py" "$APP_DIR/voice_control.py"
install -m 644 "$SCRIPT_DIR/daemon/voicectl.py" "$APP_DIR/voicectl.py"
install -m 755 "$SCRIPT_DIR/daemon/voicectl" "$APP_DIR/voicectl"
install -m 644 "$SCRIPT_DIR/daemon/tts_control.py" "$APP_DIR/tts_control.py"
echo "  OK"
echo ""

echo "[3/6] Creating Python virtualenv at $VENV_DIR ..."
if [ ! -x "$VENV_DIR/bin/python" ] || ! "$VENV_DIR/bin/python" -c 'import xml.parsers.expat' >/dev/null 2>&1; then
    "$PYTHON_BOOTSTRAP" -m venv --clear "$VENV_DIR"
fi
"$VENV_DIR/bin/pip" install --quiet --upgrade pip
"$VENV_DIR/bin/pip" install --quiet "bleak>=0.22" "httpx>=0.27" "yeelight>=0.7" "dashscope>=1.20"
PYTHON_BIN="$VENV_DIR/bin/python"
echo "  OK ($PYTHON_BIN)"
echo ""

echo "[4/6] Rendering launchd plist..."
mkdir -p "$HOME/Library/LaunchAgents" "$LOG_DIR"
DEEPSEEK_API_KEY_VALUE="${DEEPSEEK_API_KEY:-}"
if [ -z "$DEEPSEEK_API_KEY_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DEEPSEEK_API_KEY_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DEEPSEEK_API_KEY' "$PLIST_DST" 2>/dev/null || true)"
fi
DEEPSEEK_API_KEY_ESC="$(escape_sed_replacement "$DEEPSEEK_API_KEY_VALUE")"
OPENAI_API_KEY_VALUE="${OPENAI_API_KEY:-}"
if [ -z "$OPENAI_API_KEY_VALUE" ] && [ -f "$PLIST_DST" ]; then
    OPENAI_API_KEY_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:OPENAI_API_KEY' "$PLIST_DST" 2>/dev/null || true)"
fi
TRANSCRIBE_PROVIDER_VALUE="${TRANSCRIBE_PROVIDER:-}"
if [ -z "$TRANSCRIBE_PROVIDER_VALUE" ] && [ -f "$PLIST_DST" ]; then
    TRANSCRIBE_PROVIDER_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:TRANSCRIBE_PROVIDER' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$TRANSCRIBE_PROVIDER_VALUE" ]; then
    TRANSCRIBE_PROVIDER_VALUE="dashscope"
fi
DASHSCOPE_API_HOST_VALUE="${DASHSCOPE_API_HOST:-}"
if [ -z "$DASHSCOPE_API_HOST_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_API_HOST_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_API_HOST' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_API_HOST_VALUE" ]; then
    DASHSCOPE_API_HOST_VALUE="ws-okdtcezvjpiyixbk.cn-beijing.maas.aliyuncs.com"
fi
DASHSCOPE_COMPATIBLE_BASE_URL_VALUE="${DASHSCOPE_COMPATIBLE_BASE_URL:-}"
if [ -z "$DASHSCOPE_COMPATIBLE_BASE_URL_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_COMPATIBLE_BASE_URL_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_COMPATIBLE_BASE_URL' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_COMPATIBLE_BASE_URL_VALUE" ]; then
    DASHSCOPE_COMPATIBLE_BASE_URL_VALUE="https://${DASHSCOPE_API_HOST_VALUE}/compatible-mode/v1"
fi
DASHSCOPE_API_KEY_VALUE="${DASHSCOPE_API_KEY:-}"
if [ -z "$DASHSCOPE_API_KEY_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_API_KEY_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_API_KEY' "$PLIST_DST" 2>/dev/null || true)"
fi
DASHSCOPE_TRANSCRIBE_MODE_VALUE="${DASHSCOPE_TRANSCRIBE_MODE:-}"
if [ -z "$DASHSCOPE_TRANSCRIBE_MODE_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_TRANSCRIBE_MODE_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_TRANSCRIBE_MODE' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_TRANSCRIBE_MODE_VALUE" ]; then
    DASHSCOPE_TRANSCRIBE_MODE_VALUE="compatible"
fi
DASHSCOPE_TRANSCRIBE_MODEL_VALUE="${DASHSCOPE_TRANSCRIBE_MODEL:-}"
if [ -z "$DASHSCOPE_TRANSCRIBE_MODEL_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_TRANSCRIBE_MODEL_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_TRANSCRIBE_MODEL' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_TRANSCRIBE_MODEL_VALUE" ]; then
    DASHSCOPE_TRANSCRIBE_MODEL_VALUE="qwen3-asr-flash"
fi
DASHSCOPE_ASR_LANGUAGE_VALUE="${DASHSCOPE_ASR_LANGUAGE:-}"
if [ -z "$DASHSCOPE_ASR_LANGUAGE_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_ASR_LANGUAGE_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_ASR_LANGUAGE' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_ASR_LANGUAGE_VALUE" ]; then
    DASHSCOPE_ASR_LANGUAGE_VALUE="zh"
fi
DASHSCOPE_TRANSCRIBE_TIMEOUT_VALUE="${DASHSCOPE_TRANSCRIBE_TIMEOUT:-}"
if [ -z "$DASHSCOPE_TRANSCRIBE_TIMEOUT_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_TRANSCRIBE_TIMEOUT_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_TRANSCRIBE_TIMEOUT' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_TRANSCRIBE_TIMEOUT_VALUE" ]; then
    DASHSCOPE_TRANSCRIBE_TIMEOUT_VALUE="12"
fi
DASHSCOPE_TTS_MODEL_VALUE="${DASHSCOPE_TTS_MODEL:-}"
if [ -z "$DASHSCOPE_TTS_MODEL_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_TTS_MODEL_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_TTS_MODEL' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_TTS_MODEL_VALUE" ]; then
    DASHSCOPE_TTS_MODEL_VALUE="qwen3-tts-instruct-flash-realtime-2026-01-22"
fi
DASHSCOPE_TTS_VOICE_VALUE="${DASHSCOPE_TTS_VOICE:-}"
if [ -z "$DASHSCOPE_TTS_VOICE_VALUE" ] && [ -f "$PLIST_DST" ]; then
    DASHSCOPE_TTS_VOICE_VALUE="$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:DASHSCOPE_TTS_VOICE' "$PLIST_DST" 2>/dev/null || true)"
fi
if [ -z "$DASHSCOPE_TTS_VOICE_VALUE" ]; then
    DASHSCOPE_TTS_VOICE_VALUE="Cherry"
fi
if [ -n "$DEEPSEEK_API_KEY_VALUE" ] && [ "$OPENAI_API_KEY_VALUE" = "$DEEPSEEK_API_KEY_VALUE" ]; then
    OPENAI_API_KEY_VALUE=""
fi
OPENAI_API_KEY_ESC="$(escape_sed_replacement "$OPENAI_API_KEY_VALUE")"
TRANSCRIBE_PROVIDER_ESC="$(escape_sed_replacement "$TRANSCRIBE_PROVIDER_VALUE")"
DASHSCOPE_API_HOST_ESC="$(escape_sed_replacement "$DASHSCOPE_API_HOST_VALUE")"
DASHSCOPE_API_KEY_ESC="$(escape_sed_replacement "$DASHSCOPE_API_KEY_VALUE")"
DASHSCOPE_COMPATIBLE_BASE_URL_ESC="$(escape_sed_replacement "$DASHSCOPE_COMPATIBLE_BASE_URL_VALUE")"
DASHSCOPE_TRANSCRIBE_MODE_ESC="$(escape_sed_replacement "$DASHSCOPE_TRANSCRIBE_MODE_VALUE")"
DASHSCOPE_TRANSCRIBE_MODEL_ESC="$(escape_sed_replacement "$DASHSCOPE_TRANSCRIBE_MODEL_VALUE")"
DASHSCOPE_ASR_LANGUAGE_ESC="$(escape_sed_replacement "$DASHSCOPE_ASR_LANGUAGE_VALUE")"
DASHSCOPE_TRANSCRIBE_TIMEOUT_ESC="$(escape_sed_replacement "$DASHSCOPE_TRANSCRIBE_TIMEOUT_VALUE")"
DASHSCOPE_TTS_MODEL_ESC="$(escape_sed_replacement "$DASHSCOPE_TTS_MODEL_VALUE")"
DASHSCOPE_TTS_VOICE_ESC="$(escape_sed_replacement "$DASHSCOPE_TTS_VOICE_VALUE")"
sed \
    -e "s|__PYTHON_BIN__|${PYTHON_BIN}|g" \
    -e "s|__DAEMON_PATH__|${DAEMON_PY}|g" \
    -e "s|__REPO_DIR__|${APP_DIR}|g" \
    -e "s|__LOG_OUT__|${LOG_OUT}|g" \
    -e "s|__LOG_ERR__|${LOG_ERR}|g" \
    -e "s|__HOME__|${HOME}|g" \
    -e "s|__DEEPSEEK_API_KEY__|${DEEPSEEK_API_KEY_ESC}|g" \
    -e "s|__OPENAI_API_KEY__|${OPENAI_API_KEY_ESC}|g" \
    -e "s|__TRANSCRIBE_PROVIDER__|${TRANSCRIBE_PROVIDER_ESC}|g" \
    -e "s|__DASHSCOPE_API_HOST__|${DASHSCOPE_API_HOST_ESC}|g" \
    -e "s|__DASHSCOPE_API_KEY__|${DASHSCOPE_API_KEY_ESC}|g" \
    -e "s|__DASHSCOPE_COMPATIBLE_BASE_URL__|${DASHSCOPE_COMPATIBLE_BASE_URL_ESC}|g" \
    -e "s|__DASHSCOPE_TRANSCRIBE_MODE__|${DASHSCOPE_TRANSCRIBE_MODE_ESC}|g" \
    -e "s|__DASHSCOPE_TRANSCRIBE_MODEL__|${DASHSCOPE_TRANSCRIBE_MODEL_ESC}|g" \
    -e "s|__DASHSCOPE_ASR_LANGUAGE__|${DASHSCOPE_ASR_LANGUAGE_ESC}|g" \
    -e "s|__DASHSCOPE_TRANSCRIBE_TIMEOUT__|${DASHSCOPE_TRANSCRIBE_TIMEOUT_ESC}|g" \
    -e "s|__DASHSCOPE_TTS_MODEL__|${DASHSCOPE_TTS_MODEL_ESC}|g" \
    -e "s|__DASHSCOPE_TTS_VOICE__|${DASHSCOPE_TTS_VOICE_ESC}|g" \
    "$PLIST_SRC" > "$PLIST_DST"
echo "  Installed: $PLIST_DST"
echo ""

echo "[5/6] Bluetooth permission check..."
echo "  On first run the daemon will trigger a Bluetooth permission prompt."
echo "  macOS only prompts for foreground processes — so we'll run it"
echo "  interactively once below. Press Ctrl+C after you see 'Scanning...'"
echo "  and grant permission when prompted. Then re-run this installer"
echo "  (or just continue) to enable launchd autostart."
echo ""
read -r -p "Run a permission-priming scan now? [Y/n] " ans
if [[ ! "$ans" =~ ^[Nn]$ ]]; then
    "$PYTHON_BIN" "$DAEMON_PY" || true
fi
echo ""

echo "[6/6] Loading launchd service..."
launchctl unload "$PLIST_DST" 2>/dev/null || true
launchctl load -w "$PLIST_DST"
echo "  Loaded."
echo ""

echo "=== Done ==="
echo ""
echo "First-time Bluetooth pairing (after firmware is flashed):"
echo "  1. Power on the device."
echo "  2. Open System Settings → Bluetooth."
echo "  3. Click 'Connect' next to 'Clawdmeter'."
echo "  4. The daemon will discover it within ~30 s and start polling."
echo ""
echo "Useful commands:"
echo "  launchctl list | grep claude-usage     # check it's running"
echo "  tail -F $LOG_OUT                       # live logs"
echo "  $LIGHTCTL \"把台灯调成柔和绿光\"        # natural-language light control"
echo "  $APP_DIR/voicectl recording.wav       # transcribe audio, then control lamp"
echo "  launchctl unload $PLIST_DST            # stop"
echo "  launchctl load -w $PLIST_DST           # start"
