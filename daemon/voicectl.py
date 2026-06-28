#!/usr/bin/env python3
"""CLI: transcribe an audio file, then run it through lamp control."""

from __future__ import annotations

import argparse
import io
import sys
import wave
from pathlib import Path

from light_control import describe_intent, parse_light_text, run_light_command
from voice_control import pcm16_to_wav_bytes, transcribe_wav_bytes


def _read_audio(args) -> tuple[bytes, int]:
    data = Path(args.audio_file).read_bytes()
    if not args.raw:
        with wave.open(io.BytesIO(data), "rb") as wav:
            sample_rate = wav.getframerate()
        return data, sample_rate
    return (
        pcm16_to_wav_bytes(data, sample_rate=args.sample_rate, channels=args.channels),
        args.sample_rate,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="voicectl",
        description="Transcribe an audio file and use the text as a lamp command.",
    )
    parser.add_argument("audio_file", help="WAV file, or raw PCM16 with --raw")
    parser.add_argument("--raw", action="store_true", help="Treat input as raw PCM16")
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--channels", type=int, default=1)
    parser.add_argument("--dry-run", action="store_true", help="Do not control the lamp")
    args = parser.parse_args(argv)

    try:
        wav_bytes, sample_rate = _read_audio(args)
        text = transcribe_wav_bytes(
            wav_bytes,
            filename=Path(args.audio_file).name,
            sample_rate=sample_rate,
        )
        print(f"Transcript: {text}")
        intent = parse_light_text(text)
        print(f"Intent: {describe_intent(intent)}")
        if not args.dry_run:
            run_light_command(intent.action, intent.value)
    except wave.Error as e:
        print(f"voicectl: invalid audio: {e}", file=sys.stderr)
        return 2
    except Exception as e:  # noqa: BLE001
        print(f"voicectl: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
