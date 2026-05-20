#!/usr/bin/env python3
"""DeepSeek chat helper for the CodePet daemon.

Wraps DeepSeek's OpenAI-compatible /chat/completions endpoint. The API key
is read from the environment — never hardcode it.

deepseek-v4-flash is a reasoning model: it emits `reasoning_content`
(its thinking) before `content` (the actual answer). This module streams
only the `content` answer to callers; max_tokens is generous so the answer
isn't starved by the reasoning budget.

Standalone test:
    python3 deepseek_chat.py "你好，介绍一下自己"
"""

import json
import os

import httpx

BASE_URL = os.environ.get("OPENAI_BASE_URL", "https://api.deepseek.com").rstrip("/")
MODEL = os.environ.get("OPENAI_MODEL", "deepseek-v4-flash")
MAX_TOKENS = 4096


def _api_key() -> str | None:
    return os.environ.get("DEEPSEEK_API_KEY") or os.environ.get("OPENAI_API_KEY")


def chat_stream(messages: list[dict]):
    """Stream the assistant's answer for a message list.

    Yields `content` text pieces as they arrive. Raises on auth/network
    errors so the caller can surface them.
    """
    key = _api_key()
    if not key:
        raise RuntimeError("DEEPSEEK_API_KEY (or OPENAI_API_KEY) not set")

    body = {
        "model": MODEL,
        "messages": messages,
        "max_tokens": MAX_TOKENS,
        "stream": True,
    }
    with httpx.stream(
        "POST",
        f"{BASE_URL}/chat/completions",
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": "application/json",
        },
        json=body,
        timeout=httpx.Timeout(90.0, connect=15.0),
    ) as resp:
        resp.raise_for_status()
        for line in resp.iter_lines():
            if not line.startswith("data: "):
                continue
            data = line[6:].strip()
            if data == "[DONE]":
                break
            try:
                chunk = json.loads(data)
            except json.JSONDecodeError:
                continue
            delta = chunk.get("choices", [{}])[0].get("delta", {})
            piece = delta.get("content")
            if piece:
                yield piece


def chat(messages: list[dict]) -> str:
    """Non-streaming convenience wrapper — returns the full answer."""
    return "".join(chat_stream(messages))


if __name__ == "__main__":
    import sys

    question = sys.argv[1] if len(sys.argv) > 1 else "用一句话介绍你自己"
    print(f"model : {MODEL}")
    print(f"Q     : {question}")
    print("A     : ", end="", flush=True)
    try:
        for piece in chat_stream([{"role": "user", "content": question}]):
            print(piece, end="", flush=True)
        print()
    except httpx.HTTPStatusError as e:
        print(f"\n[HTTP {e.response.status_code}] {e.response.text[:300]}")
    except Exception as e:  # noqa: BLE001
        print(f"\n[error] {e}")
