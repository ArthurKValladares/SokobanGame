#!/usr/bin/env python3
"""Encode seamless terrain normal/ORM maps from authored height sources.

ImageGen source prompts are saved in terrain_pbr_prompts.json. This conversion
is mathematical texture encoding, preserves the existing albedo and painted
splat maps, and writes only changed bytes. Requires numpy and Pillow.
"""
from __future__ import annotations

import json

import numpy as np
from PIL import Image

from make_pbr_art_pass import save_image, save_text
from pbr_gltf import ASSETS

SIZE = 1024
PROFILES = {
    "ground_grass": ("GroundGrass", 12.0, .94, .035, .10),
    "ground_rock": ("GroundRock", 16.0, .86, .07, .14),
    "ground_rock_side": ("GroundRockSide", 8.0, .88, .05, .12),
}


def periodic_height(height):
    """Periodic-plus-smooth decomposition removes the source's boundary jump.

    Subtract the harmonic smooth component, keeping the local authored relief.
    The derivatives below use wrapped neighbours at every mip/tile boundary.
    """
    boundary = np.zeros_like(height)
    boundary[0] = height[-1] - height[0]
    boundary[-1] = -boundary[0]
    boundary[:, 0] += height[:, -1] - height[:, 0]
    boundary[:, -1] -= height[:, -1] - height[:, 0]
    frequencies = np.arange(len(height)) * (2 * np.pi / len(height))
    denominator = 2 * np.cos(frequencies)[:, None] + 2 * np.cos(frequencies)[None, :] - 4
    denominator[0, 0] = 1
    smooth = np.fft.fft2(boundary) / denominator
    smooth[0, 0] = 0
    return height - np.fft.ifft2(smooth).real


def blur(height, sigma):
    frequency = np.fft.fftfreq(len(height))
    kernel = np.exp(-2 * np.pi**2 * sigma**2 *
                    (frequency[:, None]**2 + frequency[None, :]**2))
    return np.fft.ifft2(np.fft.fft2(height) * kernel).real


def main():
    manifest_path = ASSETS / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    definitions = {entry["name"]: entry for entry in manifest["textures"]}
    for stem, (name, gain, roughness, variation, ao_depth) in PROFILES.items():
        source = ASSETS / "custom/pbr" / (stem + "_height_source.png")
        height = np.asarray(Image.open(source).convert("L").resize(
            (SIZE, SIZE), Image.Resampling.LANCZOS), dtype=float) / 255
        height = blur(periodic_height(height), 1.0)
        dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
        dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
        normal = np.stack((-dx * gain, -dy * gain, np.ones_like(height)), axis=-1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        normal = np.rint((normal * .5 + .5) * 255).astype(np.uint8)
        # Only local recesses occlude ambient; broad shade variations do not.
        recess = np.maximum(blur(height, 8) - height, 0)
        recess /= max(float(np.percentile(recess, 99)), .001)
        cavity = np.clip(recess, 0, 1)
        grain = np.clip((height - blur(height, 3)) * 12, -1, 1)
        orm = np.stack((1 - ao_depth * cavity,
                        np.clip(roughness + variation * cavity + variation * grain, .72, .99),
                        np.zeros_like(height)), axis=-1)
        for suffix, pixels in (("normal", normal), ("orm", np.rint(orm * 255).astype(np.uint8))):
            path = ASSETS / "custom/pbr" / (stem + "_" + suffix + ".png")
            save_image(path, pixels)
            entry = {"name": name + ("Normal" if suffix == "normal" else "Orm"),
                     "path": path.relative_to(ASSETS).as_posix(), "tiling": True,
                     "filter": "linear", "colorSpace": "linear"}
            if entry["name"] in definitions:
                definitions[entry["name"]].update(entry)
            else:
                manifest["textures"].append(entry)
        print(f"{name}: {SIZE}x{SIZE} normal and ORM, nonmetallic, roughness {roughness:.2f}")
    save_text(manifest_path, json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
