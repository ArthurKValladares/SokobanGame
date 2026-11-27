"""Draw the dust puff sprites the action feedback uses.

python tools/make_dust_puffs.py

Writes assets/custom/textures/dust_puff_01.png to dust_puff_04.png, which the
manifest names DustPuff01 to DustPuff04. Each is one soft-edged puff of a few
round lobes, white, with a faint shade on its underside: the particle tints
it, so the colour of the dust is not in here. Deterministic: running it again
writes the same pixels. Needs numpy and Pillow.
"""
from pathlib import Path
import random

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TEXTURES = ROOT / 'assets/custom/textures'
SIZE = 256
SEEDS = (11, 23, 37, 58)
# No lobe reaches past this share of the half-width, so a puff is never cut
# by the edge of its quad. The puffs come out about seven tenths of the quad
# across; an effect's sizes are tuned to that.
REACH = 0.9


def lobes(seed):
    """Centre and radius of each lobe, in half-widths from the middle."""
    rng = random.Random(seed)
    count = rng.choice((5, 6, 6, 7))
    turn = rng.uniform(0, 2 * np.pi)
    result = [(0.0, 0.0, rng.uniform(0.5, 0.58))]
    for index in range(count):
        angle = turn + 2 * np.pi * (index + rng.uniform(-0.25, 0.25)) / count
        radius = rng.uniform(0.27, 0.42)
        distance = min(rng.uniform(0.36, 0.5), REACH - radius)
        result.append((distance * np.cos(angle), distance * np.sin(angle), radius))
    return result


def blurred(field, sigma):
    """Gaussian blur, separably and in numpy so that every machine agrees."""
    reach = int(sigma * 3)
    taps = np.exp(-0.5 * (np.arange(-reach, reach + 1) / sigma) ** 2)
    taps /= taps.sum()
    padded = np.pad(field, reach, mode='constant')
    rows = np.apply_along_axis(lambda row: np.convolve(row, taps, 'valid'), 1, padded)
    return np.apply_along_axis(lambda column: np.convolve(column, taps, 'valid'), 0, rows)


def puff(seed):
    axis = (np.arange(SIZE) + 0.5) / SIZE * 2 - 1
    x, y = np.meshgrid(axis, axis)
    # A lobe is a dome: full height at its centre, nothing at its rim. The
    # puff is as tall as its tallest lobe at each point, with the creases
    # between lobes filled a little so that they read as one cloud.
    domes = [np.clip(1 - ((x - cx) ** 2 + (y - cy) ** 2) / (r * r), 0, None)
             for cx, cy, r in lobes(seed)]
    height = np.maximum.reduce(domes)
    height = np.maximum(height, np.clip(sum(domes) * 0.45, 0, 1))
    alpha = np.clip(height / 0.34, 0, 1)
    alpha = alpha * alpha * (3 - 2 * alpha)
    # Lit from the upper left. The shade comes from the outline alone, blurred,
    # so that it rounds the whole puff and draws no crease between its lobes.
    body = blurred(alpha, SIZE / 14)
    slope_y, slope_x = np.gradient(body, 2 / SIZE)
    toward_light = slope_x * 0.6 + slope_y * 0.8
    tone = np.clip(0.5 + toward_light * 0.55, 0, 1)
    tone = tone * tone * (3 - 2 * tone)
    shade = 0.82 + 0.18 * tone
    rgba = np.empty((SIZE, SIZE, 4), np.uint8)
    rgba[..., :3] = np.round(shade * 255).astype(np.uint8)[..., None]
    rgba[..., 3] = np.round(alpha * 255).astype(np.uint8)
    return rgba


def main():
    TEXTURES.mkdir(parents=True, exist_ok=True)
    for number, seed in enumerate(SEEDS, start=1):
        rgba = puff(seed)
        alpha = rgba[..., 3]
        if alpha[0].any() or alpha[-1].any() or alpha[:, 0].any() or alpha[:, -1].any():
            raise RuntimeError(f'puff {number} reaches the edge of its texture')
        Image.fromarray(rgba).save(
            TEXTURES / f'dust_puff_{number:02d}.png', optimize=True)


if __name__ == '__main__':
    main()
