#!/usr/bin/env python3
"""Speech-to-text bridge for Clawdmeter voice lamp control."""

from __future__ import annotations

import base64
import io
import os
from pathlib import Path
import struct
import wave

import httpx

TRANSCRIBE_PROVIDER = os.environ.get("TRANSCRIBE_PROVIDER", "dashscope").strip().lower()
TRANSCRIBE_TIMEOUT = float(
    os.environ.get("TRANSCRIBE_TIMEOUT")
    or os.environ.get("DASHSCOPE_TRANSCRIBE_TIMEOUT")
    or "12"
)

DASHSCOPE_API_HOST = os.environ.get(
    "DASHSCOPE_API_HOST",
    "ws-okdtcezvjpiyixbk.cn-beijing.maas.aliyuncs.com",
).strip()
DASHSCOPE_TRANSCRIBE_URL = os.environ.get(
    "DASHSCOPE_TRANSCRIBE_URL",
    f"https://{DASHSCOPE_API_HOST}/api/v1/services/aigc/multimodal-generation/generation",
)
DASHSCOPE_COMPATIBLE_BASE_URL = os.environ.get(
    "DASHSCOPE_COMPATIBLE_BASE_URL",
    f"https://{DASHSCOPE_API_HOST}/compatible-mode/v1",
).rstrip("/")
DASHSCOPE_TRANSCRIBE_MODE = os.environ.get(
    "DASHSCOPE_TRANSCRIBE_MODE",
    "compatible",
).strip().lower()
DASHSCOPE_TRANSCRIBE_MODEL = os.environ.get(
    "DASHSCOPE_TRANSCRIBE_MODEL",
    "qwen3-asr-flash",
)
DASHSCOPE_ASR_LANGUAGE = os.environ.get("DASHSCOPE_ASR_LANGUAGE", "zh").strip()
DASHSCOPE_TARGET_SAMPLE_RATE = int(os.environ.get("DASHSCOPE_TARGET_SAMPLE_RATE", "16000"))
NORMALIZE_TARGET_PEAK = int(os.environ.get("TRANSCRIBE_NORMALIZE_TARGET_PEAK", "24000"))


def _dashscope_api_key() -> str:
    key = os.environ.get("DASHSCOPE_API_KEY")
    if not key:
        raise RuntimeError("DASHSCOPE_API_KEY is not set")
    return key


def pcm16_to_wav_bytes(pcm: bytes, sample_rate: int = 16000, channels: int = 1) -> bytes:
    if sample_rate <= 0:
        raise ValueError("sample_rate must be positive")
    if channels <= 0:
        raise ValueError("channels must be positive")
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)
    return buf.getvalue()


def _wav_from_pcm16(samples: list[int], sample_rate: int, channels: int) -> bytes:
    out = io.BytesIO()
    with wave.open(out, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    return out.getvalue()


def _normalize_samples(samples: list[int]) -> list[int]:
    if not samples:
        return samples
    peak = max(abs(s) for s in samples)
    if peak <= 0 or peak >= NORMALIZE_TARGET_PEAK:
        return samples
    gain = min(12.0, NORMALIZE_TARGET_PEAK / peak)
    return [max(-32768, min(32767, int(s * gain))) for s in samples]


def _upsample_linear(samples: list[int], channels: int, src_rate: int, dst_rate: int) -> list[int]:
    if src_rate <= 0 or dst_rate <= src_rate or not samples:
        return samples
    frames = len(samples) // channels
    if frames <= 1:
        return samples
    dst_frames = int(frames * dst_rate / src_rate)
    out: list[int] = []
    for dst_i in range(dst_frames):
        src_pos = dst_i * src_rate / dst_rate
        i0 = int(src_pos)
        if i0 >= frames - 1:
            i0 = frames - 1
            frac = 0.0
        else:
            frac = src_pos - i0
        i1 = min(i0 + 1, frames - 1)
        for ch in range(channels):
            a = samples[i0 * channels + ch]
            b = samples[i1 * channels + ch]
            out.append(int(a + (b - a) * frac))
    return out


def _prepare_dashscope_wav(wav_bytes: bytes) -> tuple[bytes, int]:
    """Make short ESP32 recordings friendlier to DashScope ASR."""
    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            frames = wav.readframes(wav.getnframes())
    except wave.Error:
        return wav_bytes, DASHSCOPE_TARGET_SAMPLE_RATE

    if sample_width != 2 or channels <= 0:
        return wav_bytes, sample_rate

    usable = len(frames) - (len(frames) % 2)
    if usable <= 0:
        return wav_bytes, sample_rate
    samples = list(struct.unpack(f"<{usable // 2}h", frames[:usable]))
    target_rate = sample_rate
    if sample_rate < DASHSCOPE_TARGET_SAMPLE_RATE:
        samples = _upsample_linear(samples, channels, sample_rate, DASHSCOPE_TARGET_SAMPLE_RATE)
        target_rate = DASHSCOPE_TARGET_SAMPLE_RATE
    samples = _normalize_samples(samples)
    return _wav_from_pcm16(samples, target_rate, channels), target_rate


def _clip_detail(text: str) -> str:
    return text[:240].replace("\n", " ").strip()


def _raise_http_error(provider: str, resp: httpx.Response) -> None:
    try:
        resp.raise_for_status()
    except httpx.HTTPStatusError as e:
        detail = _clip_detail(e.response.text)
        if e.response.status_code in (401, 403):
            raise RuntimeError(
                f"{provider} auth/access failed ({e.response.status_code}): {detail}"
            ) from e
        raise RuntimeError(
            f"{provider} STT request failed ({e.response.status_code}): {detail}"
        ) from e


def _as_text(value) -> str:
    if isinstance(value, str):
        return value.strip()
    if isinstance(value, list):
        return " ".join(_as_text(v) for v in value).strip()
    if isinstance(value, dict):
        for key in ("text", "transcript", "sentence", "content"):
            if key in value:
                text = _as_text(value[key])
                if text:
                    return text
    return ""


def _find_text(value, depth: int = 0) -> str:
    if depth > 8:
        return ""
    if isinstance(value, dict):
        for key in ("text", "transcript", "sentence"):
            text = _as_text(value.get(key))
            if text:
                return text
        for key in ("message", "content", "output", "choices", "result", "results"):
            if key in value:
                if key == "content":
                    text = _as_text(value[key])
                    if text:
                        return text
                text = _find_text(value[key], depth + 1)
                if text:
                    return text
        for child in value.values():
            text = _find_text(child, depth + 1)
            if text:
                return text
    elif isinstance(value, list):
        for child in value:
            text = _find_text(child, depth + 1)
            if text:
                return text
    return ""


def _extract_dashscope_text(result: dict) -> str:
    text = _find_text(result)
    if text:
        return text
    raise RuntimeError(f"DashScope response did not include transcript: {_clip_detail(str(result))}")


def _extract_compatible_text(result: dict) -> str:
    try:
        text = result["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        text = _find_text(result)
    if isinstance(text, str):
        return text.strip()
    raise RuntimeError(f"DashScope compatible response did not include text: {_clip_detail(str(result))}")


def _transcribe_dashscope_native_wav(
    wav_bytes: bytes,
    sample_rate: int = 16000,
    filename: str = "clawdmeter-voice.wav",
) -> str:
    wav_bytes, sample_rate = _prepare_dashscope_wav(wav_bytes)
    headers = {
        "Authorization": f"Bearer {_dashscope_api_key()}",
        "Content-Type": "application/json",
        "X-DashScope-SSE": "disable",
    }
    audio_b64 = base64.b64encode(wav_bytes).decode("ascii")
    payload = {
        "model": DASHSCOPE_TRANSCRIBE_MODEL,
        "input": {
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {
                            "type": "input_audio",
                            "input_audio": {
                                "data": f"data:audio/wav;base64,{audio_b64}",
                            },
                        }
                    ],
                }
            ]
        },
        "parameters": {
            "format": "wav",
            "sample_rate": str(sample_rate),
        },
    }
    if DASHSCOPE_ASR_LANGUAGE:
        payload["parameters"]["language"] = DASHSCOPE_ASR_LANGUAGE
    with httpx.Client(timeout=TRANSCRIBE_TIMEOUT) as client:
        resp = client.post(DASHSCOPE_TRANSCRIBE_URL, headers=headers, json=payload)
        _raise_http_error("DashScope", resp)
    return _extract_dashscope_text(resp.json()).strip()


def _transcribe_dashscope_compatible_wav(
    wav_bytes: bytes,
    sample_rate: int = 16000,
    filename: str = "clawdmeter-voice.wav",
) -> str:
    wav_bytes, sample_rate = _prepare_dashscope_wav(wav_bytes)
    headers = {
        "Authorization": f"Bearer {_dashscope_api_key()}",
        "Content-Type": "application/json",
    }
    audio_b64 = base64.b64encode(wav_bytes).decode("ascii")
    asr_options = {
        "enable_itn": False,
    }
    if DASHSCOPE_ASR_LANGUAGE:
        asr_options["language"] = DASHSCOPE_ASR_LANGUAGE

    payload = {
        "model": DASHSCOPE_TRANSCRIBE_MODEL,
        "messages": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_audio",
                        "input_audio": {
                            "data": f"data:audio/wav;base64,{audio_b64}",
                        },
                    }
                ],
            }
        ],
        "stream": False,
        "asr_options": asr_options,
    }
    with httpx.Client(timeout=TRANSCRIBE_TIMEOUT) as client:
        resp = client.post(
            f"{DASHSCOPE_COMPATIBLE_BASE_URL}/chat/completions",
            headers=headers,
            json=payload,
        )
        _raise_http_error("DashScope", resp)
    return _extract_compatible_text(resp.json())


def _transcribe_dashscope_wav(
    wav_bytes: bytes,
    sample_rate: int = 16000,
    filename: str = "clawdmeter-voice.wav",
) -> str:
    if DASHSCOPE_TRANSCRIBE_MODE in ("native", "maas"):
        return _transcribe_dashscope_native_wav(
            wav_bytes,
            sample_rate=sample_rate,
            filename=filename,
        )
    if DASHSCOPE_TRANSCRIBE_MODE in ("compatible", "openai-compatible"):
        return _transcribe_dashscope_compatible_wav(
            wav_bytes,
            sample_rate=sample_rate,
            filename=filename,
        )
    raise RuntimeError(f"Unknown DASHSCOPE_TRANSCRIBE_MODE: {DASHSCOPE_TRANSCRIBE_MODE}")


def transcribe_wav_bytes(
    wav_bytes: bytes,
    filename: str = "clawdmeter-voice.wav",
    sample_rate: int = 16000,
) -> str:
    provider = TRANSCRIBE_PROVIDER or "dashscope"
    if provider in ("dashscope", "aliyun"):
        try:
            return _transcribe_dashscope_wav(
                wav_bytes,
                sample_rate=sample_rate,
                filename=filename,
            )
        except Exception as dashscope_error:
            if "did not include text" in str(dashscope_error):
                return ""
            raise RuntimeError(f"DashScope failed: {_clip_detail(str(dashscope_error))}") from dashscope_error
    raise RuntimeError("Only DashScope STT is enabled; set TRANSCRIBE_PROVIDER=dashscope")


def transcribe_pcm16(pcm: bytes, sample_rate: int = 16000, channels: int = 1) -> str:
    wav_bytes = pcm16_to_wav_bytes(pcm, sample_rate=sample_rate, channels=channels)
    return transcribe_wav_bytes(wav_bytes, sample_rate=sample_rate)


def save_pcm16_wav(
    path: str | os.PathLike[str],
    pcm: bytes,
    sample_rate: int = 16000,
    channels: int = 1,
) -> None:
    Path(path).write_bytes(pcm16_to_wav_bytes(pcm, sample_rate=sample_rate, channels=channels))
