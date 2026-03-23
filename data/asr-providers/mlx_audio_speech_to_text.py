#!/usr/bin/env python3
# ==vinput-asr-provider==
# @name         MLX Audio Speech to Text
# @description  通过本地或远程 mlx-audio / Whisper 兼容服务器调用 ASR
# @author       fcitx5-vinput
# @version      1.0.0
# @env MLX_AUDIO_URL      (optional, default: http://localhost:8000)
# @env MLX_AUDIO_MODEL    (optional, default: whisper-1)
# @env MLX_AUDIO_LANGUAGE (optional, ISO-639-1 code, e.g. zh)
# @env MLX_AUDIO_TIMEOUT  (optional, seconds, default: 30)
# ==/vinput-asr-provider==
"""vinput ASR provider — OpenAI-compatible /v1/audio/transcriptions endpoint.

Reads raw PCM S16_LE 16 kHz mono audio from stdin, wraps it in a WAV
container, and posts it to an OpenAI-compatible transcription endpoint
(mlx-audio server, whisper.cpp server, or any compatible service).

Environment variables:
    MLX_AUDIO_URL       Base URL of the server.
                        Default: http://localhost:8000
    MLX_AUDIO_MODEL     Model name passed in the request form field.
                        Default: whisper-1
    MLX_AUDIO_LANGUAGE  Optional language hint (ISO-639-1 code).
    MLX_AUDIO_TIMEOUT   Request timeout in seconds.  Default: 30

Example config snippet (~/.config/vinput/config.json):
    {
      "asr_providers": [
        {
          "name": "mlx-audio",
          "command": "python3",
          "args": ["/usr/share/fcitx5-vinput/asr-providers/mlx_audio_speech_to_text.py"],
          "env": {
            "MLX_AUDIO_URL": "http://192.168.1.100:8000",
            "MLX_AUDIO_LANGUAGE": "zh"
          }
        }
      ]
    }
"""

import io
import json
import os
import struct
import sys
import uuid
from typing import Optional, Tuple
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

DEFAULT_URL = "http://localhost:8000"
DEFAULT_MODEL = "whisper-1"
DEFAULT_TIMEOUT = 30

# Audio parameters of vinput's PCM stream
SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2  # S16_LE = 2 bytes per sample


def pcm_to_wav(pcm: bytes) -> bytes:
    """Wrap raw S16_LE PCM in a minimal WAV container."""
    data_size = len(pcm)
    buf = io.BytesIO()
    # RIFF header
    buf.write(b"RIFF")
    buf.write(struct.pack("<I", 36 + data_size))  # file size - 8
    buf.write(b"WAVE")
    # fmt chunk
    buf.write(b"fmt ")
    buf.write(struct.pack("<I", 16))              # chunk size
    buf.write(struct.pack("<H", 1))               # PCM format
    buf.write(struct.pack("<H", CHANNELS))
    buf.write(struct.pack("<I", SAMPLE_RATE))
    buf.write(struct.pack("<I", SAMPLE_RATE * CHANNELS * SAMPLE_WIDTH))  # byte rate
    buf.write(struct.pack("<H", CHANNELS * SAMPLE_WIDTH))                # block align
    buf.write(struct.pack("<H", SAMPLE_WIDTH * 8))                       # bits per sample
    # data chunk
    buf.write(b"data")
    buf.write(struct.pack("<I", data_size))
    buf.write(pcm)
    return buf.getvalue()


def build_multipart(
    fields: list[Tuple[str, str]],
    file_name: str,
    file_content: bytes,
) -> Tuple[bytes, str]:
    boundary = f"----vinput-{uuid.uuid4().hex}"
    body = bytearray()

    for name, value in fields:
        body += f"--{boundary}\r\n".encode()
        body += f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode()
        body += value.encode()
        body += b"\r\n"

    body += f"--{boundary}\r\n".encode()
    body += (
        f'Content-Disposition: form-data; name="file"; filename="{file_name}"\r\n'
        f"Content-Type: audio/wav\r\n\r\n"
    ).encode()
    body += file_content
    body += b"\r\n"
    body += f"--{boundary}--\r\n".encode()

    return bytes(body), boundary


def transcribe(
    pcm: bytes,
    base_url: str,
    model: str,
    language: Optional[str],
    timeout: int,
) -> str:
    wav = pcm_to_wav(pcm)
    url = base_url.rstrip("/") + "/v1/audio/transcriptions"

    fields = [("model", model)]
    if language:
        fields.append(("language", language))

    body, boundary = build_multipart(fields, "audio.wav", wav)

    req = Request(
        url,
        data=body,
        headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Accept": "application/json",
        },
        method="POST",
    )

    with urlopen(req, timeout=timeout) as resp:
        data = json.loads(resp.read())

    text = data.get("text")
    if not isinstance(text, str) or not text.strip():
        raise RuntimeError("Server returned an empty transcript.")
    return text.strip()


def main() -> int:
    base_url = os.getenv("MLX_AUDIO_URL", DEFAULT_URL).strip() or DEFAULT_URL
    model = os.getenv("MLX_AUDIO_MODEL", DEFAULT_MODEL).strip() or DEFAULT_MODEL
    language = os.getenv("MLX_AUDIO_LANGUAGE", "").strip() or None
    timeout = int(os.getenv("MLX_AUDIO_TIMEOUT", str(DEFAULT_TIMEOUT)))

    pcm = sys.stdin.buffer.read()
    if not pcm:
        print("No audio received on stdin.", file=sys.stderr)
        return 2

    try:
        text = transcribe(pcm, base_url, model, language, timeout)
    except HTTPError as exc:
        payload = exc.read()
        try:
            msg = json.loads(payload).get("error", {}).get("message") or payload.decode(errors="replace")
        except Exception:
            msg = payload.decode(errors="replace")
        print(f"HTTP {exc.code}: {msg}", file=sys.stderr)
        return 1
    except URLError as exc:
        print(f"Cannot reach {base_url}: {exc.reason}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
