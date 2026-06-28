#!/usr/bin/env python3
"""DashScope text-to-speech playback for Clawdmeter voice chat."""

from __future__ import annotations

import base64
import os
import subprocess
import threading
import wave
from pathlib import Path

import dashscope
from dashscope.audio.qwen_tts_realtime import (
    AudioFormat,
    QwenTtsRealtime,
    QwenTtsRealtimeCallback,
)

TTS_MODEL = os.environ.get(
    "DASHSCOPE_TTS_MODEL",
    "qwen3-tts-instruct-flash-realtime-2026-01-22",
)
TTS_VOICE = os.environ.get("DASHSCOPE_TTS_VOICE", "Cherry")
TTS_TIMEOUT = float(os.environ.get("DASHSCOPE_TTS_TIMEOUT", "30"))
TTS_WAV_PATH = Path(
    os.environ.get("CLAWDMETER_TTS_WAV", "~/.clawdmeter-daemon/last_tts.wav")
).expanduser()


def _api_key() -> str:
    key = os.environ.get("DASHSCOPE_API_KEY", "").strip()
    if not key:
        raise RuntimeError("DASHSCOPE_API_KEY is not set")
    return key


class _TtsCallback(QwenTtsRealtimeCallback):
    def __init__(self) -> None:
        self.complete_event = threading.Event()
        self.audio = bytearray()
        self.error: Exception | None = None

    def on_open(self) -> None:
        return

    def on_close(self, close_status_code, close_msg) -> None:
        self.complete_event.set()

    def on_event(self, response) -> None:
        try:
            event_type = response.get("type")
            if event_type == "response.audio.delta":
                self.audio.extend(base64.b64decode(response.get("delta", "")))
            elif event_type == "session.finished":
                self.complete_event.set()
        except Exception as exc:  # noqa: BLE001
            self.error = exc
            self.complete_event.set()

    def wait_for_finished(self, timeout: float) -> None:
        if not self.complete_event.wait(timeout):
            raise TimeoutError("DashScope TTS timed out")
        if self.error is not None:
            raise RuntimeError(f"DashScope TTS callback failed: {self.error}") from self.error


def _write_wav(path: Path, pcm: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(24000)
        wav.writeframes(pcm)


def speak_text(text: str, *, logger=print) -> Path:
    """Synthesize text with DashScope and play it through macOS audio."""
    text = text.strip()
    if not text:
        raise ValueError("empty TTS text")

    dashscope.api_key = _api_key()
    callback = _TtsCallback()
    tts = QwenTtsRealtime(model=TTS_MODEL, callback=callback)
    logger(f"TTS start: {len(text)} chars, voice={TTS_VOICE}")
    tts.connect()
    try:
        tts.update_session(
            voice=TTS_VOICE,
            response_format=AudioFormat.PCM_24000HZ_MONO_16BIT,
            mode="server_commit",
        )
        tts.append_text(text)
        tts.finish()
        callback.wait_for_finished(TTS_TIMEOUT)
    finally:
        try:
            tts.close()
        except Exception:  # noqa: BLE001
            pass

    if not callback.audio:
        raise RuntimeError("DashScope TTS returned no audio")
    _write_wav(TTS_WAV_PATH, bytes(callback.audio))
    subprocess.run(["afplay", str(TTS_WAV_PATH)], check=True, timeout=TTS_TIMEOUT)
    logger(f"TTS played: {TTS_WAV_PATH}")
    return TTS_WAV_PATH


if __name__ == "__main__":
    import sys

    speak_text(" ".join(sys.argv[1:]) if len(sys.argv) > 1 else "你好，我在。")
