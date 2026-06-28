#!/usr/bin/env python3
"""Natural-language Yeelight control CLI for Clawdmeter."""

from __future__ import annotations

import argparse
import json
import sys

from light_control import describe_intent, parse_light_text, run_light_command


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="lightctl",
        description="Control the Yeelight desk lamp with natural language.",
    )
    parser.add_argument("text", nargs="*", help="command text, e.g. 把台灯调成柔和绿光")
    parser.add_argument("--ip", help="override Yeelight IP")
    parser.add_argument("--dry-run", action="store_true", help="parse only; do not control the lamp")
    parser.add_argument("--json", action="store_true", help="print parsed intent as JSON")
    args = parser.parse_args(argv)

    text = " ".join(args.text).strip()
    if not text:
        parser.error("missing command text")

    try:
        intent = parse_light_text(text)
    except ValueError as e:
        print(f"lightctl: {e}", file=sys.stderr)
        print("examples: 打开台灯 | 关掉台灯 | 调成暖白 60% | 柔和绿光 | 闪一下红色", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(intent.to_dict(), ensure_ascii=False, separators=(",", ":")))
    else:
        print(f"{text} -> {describe_intent(intent)}")

    if args.dry_run:
        return 0

    try:
        run_light_command(intent.action, intent.value, bulb_ip=args.ip, logger=print)
    except Exception as e:  # noqa: BLE001
        print(f"lightctl: command failed: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
