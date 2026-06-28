#!/usr/bin/env python3
"""Yeelight control helpers for Clawdmeter.

This module is shared by the BLE daemon and the local natural-language
CLI. It keeps device actions structured even when the input is fuzzy text.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
import re
import time
from typing import Any, Callable

try:
    from yeelight import Bulb
except ImportError:
    Bulb = None

YEELIGHT_IP = os.environ.get("CLAWDMETER_YEELIGHT_IP", "192.168.5.162")

WARM_WHITE = (255, 244, 229)
SOFT_GREEN = (64, 201, 92)

COLORS: tuple[tuple[tuple[str, ...], tuple[int, int, int], str], ...] = (
    (("暖白", "暖光", "暖色", "warm white", "warm"), WARM_WHITE, "warm white"),
    (("白色", "白光", "white"), (255, 255, 255), "white"),
    (("红色", "红光", "red"), (255, 0, 0), "red"),
    (("绿色", "绿光", "green"), SOFT_GREEN, "green"),
    (("蓝色", "蓝光", "blue"), (0, 122, 255), "blue"),
    (("黄色", "黄光", "yellow"), (255, 214, 10), "yellow"),
    (("紫色", "紫光", "purple"), (175, 82, 222), "purple"),
    (("橙色", "橙光", "orange"), (255, 149, 0), "orange"),
    (("粉色", "粉光", "pink"), (255, 45, 85), "pink"),
    (("青色", "青光", "cyan"), (0, 199, 190), "cyan"),
)

SCENES: tuple[tuple[tuple[str, ...], tuple[int, int, int, int], str], ...] = (
    (("专注", "工作", "写代码", "编程", "focus", "work"), (100, *WARM_WHITE), "focus"),
    (("休息", "放松", "break", "rest", "relax"), (40, *SOFT_GREEN), "break"),
    (("阅读", "看书", "reading", "read"), (75, *WARM_WHITE), "reading"),
    (("夜灯", "睡觉", "助眠", "night", "sleep"), (15, 255, 166, 87), "night"),
)


@dataclass(frozen=True)
class LightIntent:
    action: str
    value: Any = None
    label: str = ""

    def to_dict(self) -> dict[str, Any]:
        value = self.value
        if isinstance(value, tuple):
            value = list(value)
        return {"action": self.action, "value": value, "label": self.label}


def parse_light_text(text: str) -> LightIntent:
    """Parse a small natural-language light command into a safe action."""
    raw = text.strip()
    if not raw:
        raise ValueError("empty light command")

    normalized = _normalize(raw)
    pct = _extract_percent(normalized)
    color = _find_color(normalized)

    if _contains_any(normalized, ("关灯", "关台灯", "关闭", "关掉", "熄灭", "turn off", "off")):
        return LightIntent("off", label="off")

    if _is_red_alert(normalized):
        return LightIntent("alert_red", label="red alert")

    scene = _find_scene(normalized)
    if scene is not None:
        value, label = scene
        if pct is not None:
            value = (pct, value[1], value[2], value[3])
        return LightIntent("scene", value, label=label)

    if _contains_any(normalized, ("打开", "开灯", "开台灯", "亮起来", "turn on", "on")):
        if pct is not None and color is not None:
            rgb, label = color
            return LightIntent("scene", (pct, *rgb), label=f"{label} {pct}%")
        if pct is not None:
            return LightIntent("brightness", pct, label=f"brightness {pct}%")
        if color is not None:
            rgb, label = color
            return LightIntent("color", rgb, label=label)
        return LightIntent("on", label="on")

    if color is not None:
        rgb, label = color
        if pct is not None:
            return LightIntent("scene", (pct, *rgb), label=f"{label} {pct}%")
        if _contains_any(normalized, ("柔和", "低亮", "暗一点", "暗些", "soft", "dim")):
            return LightIntent("scene", (35, *rgb), label=f"soft {label}")
        if _contains_any(normalized, ("高亮", "明亮", "亮一点", "亮些", "bright")):
            return LightIntent("scene", (85, *rgb), label=f"bright {label}")
        return LightIntent("color", rgb, label=label)

    if pct is not None:
        return LightIntent("brightness", pct, label=f"brightness {pct}%")

    if _contains_any(normalized, ("最亮", "全亮", "满亮", "maximum", "max")):
        return LightIntent("brightness", 100, label="brightness 100%")
    if _contains_any(normalized, ("亮一点", "调亮", "更亮", "brighten", "brighter")):
        return LightIntent("brightness_delta", 15, label="brightness +15")
    if _contains_any(normalized, ("暗一点", "调暗", "更暗", "dim", "dimmer")):
        return LightIntent("brightness_delta", -15, label="brightness -15")
    if _looks_like_soft_green_asr(normalized):
        return LightIntent("scene", (35, *SOFT_GREEN), label="soft green")
    if _contains_any(normalized, ("柔和", "低亮", "soft")):
        return LightIntent("brightness", 35, label="soft brightness")

    raise ValueError(f"unsupported light command: {raw!r}")


def describe_intent(intent: LightIntent) -> str:
    if intent.action in ("on", "off", "alert_red"):
        return intent.label or intent.action
    if intent.action == "brightness":
        return f"brightness {intent.value}%"
    if intent.action == "brightness_delta":
        return f"brightness {intent.value:+d}"
    if intent.action == "color":
        r, g, b = intent.value
        return f"color #{r:02x}{g:02x}{b:02x} ({intent.label})"
    if intent.action == "scene":
        pct, r, g, b = intent.value
        return f"scene {pct}% #{r:02x}{g:02x}{b:02x} ({intent.label})"
    return intent.action


def run_light_command(
    action: str,
    value: Any = None,
    *,
    bulb_ip: str | None = None,
    logger: Callable[[str], None] | None = None,
) -> None:
    if Bulb is None:
        raise RuntimeError("yeelight package is not installed")

    ip = bulb_ip or YEELIGHT_IP
    bulb = Bulb(ip)
    if action == "on":
        bulb.turn_on()
        _log(logger, f"Light on: {ip}")
    elif action == "off":
        bulb.turn_off()
        _log(logger, f"Light off: {ip}")
    elif action == "brightness":
        pct = _clamp_int(value or 50, 1, 100)
        bulb.turn_on()
        bulb.set_brightness(pct)
        _log(logger, f"Light brightness {pct}%: {ip}")
    elif action == "brightness_delta":
        current = _current_brightness(bulb, 50)
        pct = _clamp_int(current + int(value or 0), 1, 100)
        bulb.turn_on()
        bulb.set_brightness(pct)
        _log(logger, f"Light brightness {pct}%: {ip}")
    elif action == "color":
        r, g, b = _clamp_rgb(value)
        bulb.turn_on()
        bulb.set_rgb(r, g, b)
        _log(logger, f"Light color #{r:02x}{g:02x}{b:02x}: {ip}")
    elif action == "scene":
        brightness, r, g, b = value
        pct = _clamp_int(brightness, 1, 100)
        r, g, b = _clamp_rgb((r, g, b))
        bulb.turn_on()
        bulb.set_rgb(r, g, b)
        bulb.set_brightness(pct)
        _log(logger, f"Light scene {pct}% #{r:02x}{g:02x}{b:02x}: {ip}")
    elif action == "alert_red":
        _flash_red_alert(bulb, ip, logger)
    else:
        raise ValueError(f"unknown light action {action!r}")


def _normalize(text: str) -> str:
    text = text.lower()
    return re.sub(r"\s+", " ", text)


def _contains_any(text: str, needles: tuple[str, ...]) -> bool:
    return any(needle in text for needle in needles)


def _find_color(text: str) -> tuple[tuple[int, int, int], str] | None:
    for names, rgb, label in COLORS:
        if _contains_any(text, names):
            return rgb, label
    return None


def _find_scene(text: str) -> tuple[tuple[int, int, int, int], str] | None:
    for names, value, label in SCENES:
        if _contains_any(text, names):
            return value, label
    return None


def _is_red_alert(text: str) -> bool:
    has_flash = _contains_any(text, ("闪", "闪烁", "提醒", "alert", "warn"))
    has_red = _contains_any(text, ("红", "red"))
    return has_flash and has_red


def _looks_like_soft_green_asr(text: str) -> bool:
    if not _contains_any(text, ("柔和", "柔合", "soft")):
        return False
    if not _contains_any(text, ("灯", "光", "lamp", "light")):
        return False
    return _contains_any(text, ("的光", "一光", "衣光", "绿", "滤", "吕"))


def _extract_percent(text: str) -> int | None:
    patterns = (
        r"(\d{1,3})\s*%",
        r"百分之\s*([零〇一二两三四五六七八九十百]+)",
        r"亮度\s*(?:到|调到|设为|设置为)?\s*(\d{1,3})",
        r"brightness\s*(\d{1,3})",
    )
    for pattern in patterns:
        match = re.search(pattern, text)
        if not match:
            continue
        raw = match.group(1)
        num = _chinese_number_to_int(raw) if not raw.isdigit() else int(raw)
        if num is not None:
            return _clamp_int(num, 1, 100)
    if _contains_any(text, ("一半", "半亮")):
        return 50
    return None


def _chinese_number_to_int(text: str) -> int | None:
    digits = {
        "零": 0,
        "〇": 0,
        "一": 1,
        "二": 2,
        "两": 2,
        "三": 3,
        "四": 4,
        "五": 5,
        "六": 6,
        "七": 7,
        "八": 8,
        "九": 9,
    }
    if text in digits:
        return digits[text]
    if text == "十":
        return 10
    if text == "百" or text == "一百":
        return 100
    if "十" in text:
        left, _, right = text.partition("十")
        tens = digits.get(left, 1 if left == "" else None)
        ones = digits.get(right, 0 if right == "" else None)
        if tens is None or ones is None:
            return None
        return tens * 10 + ones
    return None


def _clamp_int(value: Any, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


def _clamp_rgb(value: Any) -> tuple[int, int, int]:
    r, g, b = value
    return (
        _clamp_int(r, 0, 255),
        _clamp_int(g, 0, 255),
        _clamp_int(b, 0, 255),
    )


def _current_brightness(bulb, default: int) -> int:
    try:
        props = bulb.get_properties(["bright"])
    except Exception:  # noqa: BLE001
        return default
    return _parse_int(props.get("bright"), default)


def _flash_red_alert(bulb, bulb_ip: str, logger: Callable[[str], None] | None) -> None:
    props = {}
    try:
        props = bulb.get_properties(["power", "bright", "rgb"])
    except Exception as e:  # noqa: BLE001
        _log(logger, f"Light alert restore snapshot failed: {e}")

    was_on = props.get("power") == "on"
    prev_bright = _parse_int(props.get("bright") or props.get("current_brightness"), 50)
    prev_rgb = _parse_int(props.get("rgb"), None)

    bulb.turn_on()
    bulb.set_rgb(255, 0, 0)
    bulb.set_brightness(100)
    time.sleep(0.35)

    if not was_on:
        bulb.turn_off()
        _log(logger, f"Light red alert flashed, restored off: {bulb_ip}")
        return

    if prev_rgb is not None:
        bulb.set_rgb((prev_rgb >> 16) & 0xFF, (prev_rgb >> 8) & 0xFF, prev_rgb & 0xFF)
    bulb.set_brightness(_clamp_int(prev_bright, 1, 100))
    _log(logger, f"Light red alert flashed, restored on: {bulb_ip}")


def _parse_int(value: Any, default: int | None) -> int | None:
    try:
        if value is None:
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def _log(logger: Callable[[str], None] | None, message: str) -> None:
    if logger is not None:
        logger(message)
