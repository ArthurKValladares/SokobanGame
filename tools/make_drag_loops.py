#!/usr/bin/env python3
"""Generate seamlessly looping stone-drag sounds.

The raw Kenney `stoneDrag*.ogg` foley samples fade to silence at both ends,
so looping the whole file produces an audible dip every few seconds. This
tool converts each sample into a loop-ready asset:

1. Decode to PCM (via ffmpeg).
2. Trim leading/trailing frames below a silence threshold.
3. Equal-power crossfade the final CROSSFADE_SECONDS into the beginning and
   drop the faded tail, so end -> start playback is continuous.
4. Encode to OGG Vorbis in `assets/custom/audio/` (via ffmpeg).

Tweakable knobs:
- SILENCE_THRESHOLD: fraction of peak amplitude treated as silence when
  trimming edges (raise it to trim more aggressively).
- CROSSFADE_SECONDS: length of the loop seam blend. Longer is smoother but
  eats more of the sample's character near the loop point.
- EDGE_RAMP_SECONDS: tiny fade at the very first/last samples. Vorbis does
  not reconstruct file edges exactly, so even a sample-continuous seam can
  decode with a click; pinning the edges near zero suppresses it while being
  far too short to hear as a gap.

Run from the repository root:  python tools/make_drag_loops.py
Requires ffmpeg on PATH. Outputs are committed assets; re-run only when the
source samples or knobs change, then rebuild so CMake re-copies them.
"""

from __future__ import annotations

import math
import struct
import subprocess
import sys
from pathlib import Path

SILENCE_THRESHOLD = 0.02
CROSSFADE_SECONDS = 0.25
EDGE_RAMP_SECONDS = 0.0015
SAMPLE_RATE = 44100

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIR = (
    REPO_ROOT
    / "assets"
    / "Kenney Game Assets All-in-1 3.5.0"
    / "Audio"
    / "Foley Sounds"
    / "Audio"
    / "Rocks"
)
OUTPUT_DIR = REPO_ROOT / "assets" / "custom" / "audio"
SOURCES = ["stoneDrag1.ogg", "stoneDrag2.ogg", "stoneDrag3.ogg", "stoneDrag4.ogg"]


def decode(path: Path) -> list[float]:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-f", "f32le", "-ac", "1",
         "-ar", str(SAMPLE_RATE), "-"],
        capture_output=True,
        check=True,
    ).stdout
    count = len(raw) // 4
    return list(struct.unpack(f"<{count}f", raw))


def encode(samples: list[float], path: Path) -> None:
    raw = struct.pack(f"<{len(samples)}f", *samples)
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ac", "1",
         "-ar", str(SAMPLE_RATE), "-i", "-", "-c:a", "libvorbis",
         "-qscale:a", "6", str(path)],
        input=raw,
        check=True,
    )


def trim_silence(samples: list[float]) -> list[float]:
    peak = max(abs(s) for s in samples)
    threshold = peak * SILENCE_THRESHOLD
    start = 0
    while start < len(samples) and abs(samples[start]) < threshold:
        start += 1
    end = len(samples)
    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1
    return samples[start:end]


def crossfade_loop(samples: list[float]) -> list[float]:
    fade = min(int(CROSSFADE_SECONDS * SAMPLE_RATE), len(samples) // 4)
    body = samples[: len(samples) - fade]
    tail = samples[len(samples) - fade :]
    for i in range(fade):
        t = i / fade
        fade_in = math.sin(t * math.pi / 2.0)
        fade_out = math.cos(t * math.pi / 2.0)
        body[i] = body[i] * fade_in + tail[i] * fade_out
    ramp = max(int(EDGE_RAMP_SECONDS * SAMPLE_RATE), 1)
    for i in range(ramp):
        gain = math.sin((i / ramp) * math.pi / 2.0)
        body[i] *= gain
        body[len(body) - 1 - i] *= gain
    return body


def main() -> int:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(SOURCES, start=1):
        source = SOURCE_DIR / name
        output = OUTPUT_DIR / f"stoneDragLoop{index}.ogg"
        samples = crossfade_loop(trim_silence(decode(source)))
        encode(samples, output)
        seconds = len(samples) / SAMPLE_RATE
        print(f"{output.name}: {seconds:.2f}s from {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
