#!/usr/bin/env python3
"""Encode aligned quarry-marble PBR data from authored albedo/height images.

Inputs: assets/custom/pbr/wall_quarry_marble/{albedo_source,height_source}.png.
Outputs: albedo, normal, roughness, occlusion, metallic, and orm.png, all 1024 square.
The native albedo and height sources are never rewritten. There is no random texture
generation or painted detail: every variation is derived from these two images.

Encoding palette/profile (linear scalar data, quantized to 8-bit PNG):
* OpenGL/Blender +Y tangent normal: target 90th-percentile tilt 8 degrees,
  maximum tilt 10 degrees, after a wrapped 1-pixel Gaussian height filter.
* Main matte roughness .70-.86: .76 + .06 signed microrelief + .04 cavity.
  The brightest authored 3% of albedo smoothly approaches fresh-stone .60;
  the darkest 8% approaches weathered .90 only where local cavities exist.
  Those brightness-derived candidates are encoding heuristics, not semantic
  claims that every pale vein is a fresh fracture or every dark vein is weather.
* Microcavity AO .90-1.00, from positive 4-pixel local mean minus height;
  this is local relief encoding, not a geometric/contact AO bake.
* Metallic 0. ORM packing is R=AO, G=perceptual roughness, B=metallic.

Orientation matters: NumPy rows increase downward; Blender image V increases
upward (PNG top row is V=1). Therefore dh/dv=-dy and normal=(-dx,+dy,1).
Blender glTF export flips UV V but retains its authored tangent handedness.
Export TANGENT with the GLB; the game's fallback derives tangents from raw
glTF UV0 and is not a substitute for the baked Blender tangent frame.
Use a Non-Color image for normal/ORM, roughnessFactor=1, metallicFactor=0,
and bind ORM to both metallicRoughnessTexture and occlusionTexture.

Run --self-check without inputs for analytic orientation/packing checks;
--dry-run computes and reports without writing; --verify-only compares existing
outputs with the deterministic encoding. Requires numpy and Pillow.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import numpy as np
from PIL import Image

from make_pbr_art_pass import save_image
from make_terrain_pbr import blur, periodic_height
from pbr_gltf import ASSETS

SIZE = 1024
DIRECTORY = ASSETS / "custom/pbr/wall_quarry_marble"
PROFILE = {
    "normal_p90_degrees": 8.0,
    "normal_max_degrees": 10.0,
    "height_blur_pixels": 1.0,
    "microrelief_blur_pixels": 3.0,
    "cavity_blur_pixels": 4.0,
    "normalization_percentile": 99.0,
    "roughness_base": .76,
    "roughness_microrelief": .06,
    "roughness_cavity": .04,
    "roughness_main_range": [.70, .86],
    "fresh_roughness": .60,
    "fresh_luminance_percentiles": [97.0, 99.8],
    "weathered_roughness": .90,
    "weathered_luminance_percentiles": [1.0, 8.0],
    "ao_depth": .10,
    "metallic": 0.0,
}


def quantize(values):
    return np.rint(np.clip(values, 0, 1) * 255).astype(np.uint8)


def wrapped_derivatives(height):
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
    return dx, dy


def tangent_normals(height):
    dx, dy = wrapped_derivatives(height)
    slope = np.hypot(dx, dy)
    p90 = float(np.percentile(slope, 90))
    gain = np.tan(np.radians(PROFILE["normal_p90_degrees"])) / p90 if p90 > 1e-12 else 0.0
    sx, sy = -dx * gain, dy * gain
    magnitude = np.hypot(sx, sy)
    cap = np.tan(np.radians(PROFILE["normal_max_degrees"]))
    limiter = np.minimum(1, cap / np.maximum(magnitude, 1e-12))
    normal = np.stack((sx * limiter, sy * limiter, np.ones_like(height)), -1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return normal, gain


def normalized_relief(values):
    scale = max(float(np.percentile(np.abs(values), PROFILE["normalization_percentile"])), 1e-6)
    return np.clip(values / scale, -1, 1)


def smooth_band(values, low, high):
    if high - low <= 1e-9:
        return np.zeros_like(values)
    t = np.clip((values - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)


def scalar_maps(height, albedo):
    # Positive deficits only, at a local scale; broad tonal regions do not AO.
    recess = np.maximum(blur(height, PROFILE["cavity_blur_pixels"]) - height, 0)
    cavity = np.maximum(normalized_relief(recess), 0)
    grain = normalized_relief(height - blur(height, PROFILE["microrelief_blur_pixels"]))
    roughness = np.clip(PROFILE["roughness_base"] + PROFILE["roughness_microrelief"] * grain
                        + PROFILE["roughness_cavity"] * cavity, *PROFILE["roughness_main_range"])
    # Convert authored sRGB albedo to linear luminance before ranking patches.
    linear = np.where(albedo <= .04045, albedo / 12.92, ((albedo + .055) / 1.055) ** 2.4)
    luminance = blur(linear @ np.array([.2126, .7152, .0722]), 1.0)
    fresh_low, fresh_high = np.percentile(luminance, PROFILE["fresh_luminance_percentiles"])
    dark_low, dark_high = np.percentile(luminance, PROFILE["weathered_luminance_percentiles"])
    fresh = smooth_band(luminance, fresh_low, fresh_high) * (1 - cavity)
    weathered = smooth_band(-luminance, -dark_high, -dark_low) * cavity
    roughness += (PROFILE["fresh_roughness"] - roughness) * fresh
    roughness += (PROFILE["weathered_roughness"] - roughness) * weathered
    occlusion = 1 - PROFILE["ao_depth"] * cavity
    metallic = np.zeros_like(height)
    return roughness, occlusion, metallic


def encode(height, albedo):
    filtered = blur(periodic_height(height), PROFILE["height_blur_pixels"])
    normal, gain = tangent_normals(filtered)
    roughness, occlusion, metallic = scalar_maps(filtered, albedo)
    maps = {
        "normal": quantize(normal * .5 + .5),
        "roughness": quantize(roughness),
        "occlusion": quantize(occlusion),
        "metallic": quantize(metallic),
        "orm": quantize(np.stack((occlusion, roughness, metallic), -1)),
    }
    validate_maps(maps)
    return maps, gain


def validate_maps(maps):
    normal = maps["normal"].astype(float) / 255 * 2 - 1
    if not np.allclose(np.linalg.norm(normal, axis=-1), 1, atol=.007):
        raise ValueError("Encoded normal vectors are not unit length within 8-bit quantization")
    if not np.all(normal[..., 2] > 0):
        raise ValueError("Encoded normal points into the surface")
    if not np.all(maps["metallic"] == 0):
        raise ValueError("Marble must remain nonmetallic")
    if maps["roughness"].min() < round(.60 * 255) or maps["roughness"].max() > round(.90 * 255):
        raise ValueError("Roughness escaped the declared fresh/weathered range")
    if maps["occlusion"].min() < round(.90 * 255):
        raise ValueError("Microcavity AO escaped its conservative range")
    expected = np.stack((maps["occlusion"], maps["roughness"], maps["metallic"]), -1)
    if not np.array_equal(maps["orm"], expected):
        raise ValueError("ORM channels differ from standalone scalar maps")


def self_check():
    # Periodic analytic ramps avoid boundary ambiguity and generate no files.
    phase = np.arange(64) * (2 * np.pi / 64)
    x_ramp = np.broadcast_to(np.sin(phase), (64, 64))
    y_ramp = x_ramp.T
    nx, _ = tangent_normals(x_ramp)
    ny, _ = tangent_normals(y_ramp)
    if not (nx[0, 0, 0] < 0 and ny[0, 0, 1] > 0):
        raise ValueError("Height ramp orientation failed: right-rise tilts -X; downward-rise tilts +Y")
    if not (nx[0, 32, 0] > 0 and ny[32, 0, 1] < 0):
        raise ValueError("Reverse height ramp orientation failed")
    if not np.allclose(blur(periodic_height(np.full((64, 64), .5)), 1), .5):
        raise ValueError("Periodic decomposition changed a flat height source")
    maps, _ = encode(np.full((64, 64), .5), np.full((64, 64, 3), .5))
    if not np.all(maps["normal"] == np.array([128, 128, 255])) or not np.all(maps["occlusion"] == 255):
        raise ValueError("Flat source does not encode neutral normal/unoccluded AO")
    print("Analytic self-check passed: +/-X, +/-Y, periodic flat height, neutral normal, AO, and ORM packing.")


def statistics(maps, gain):
    normal = maps["normal"].astype(float) / 255 * 2 - 1
    tilt = np.degrees(np.arctan2(np.linalg.norm(normal[..., :2], axis=-1), normal[..., 2]))
    result = {"normal_gain": gain, "normal_tilt_degrees": {
        "median": float(np.median(tilt)), "p90": float(np.percentile(tilt, 90)), "max": float(tilt.max())}}
    for name in ("roughness", "occlusion", "metallic"):
        values = maps[name].astype(float) / 255
        result[name] = {"min": float(values.min()), "median": float(np.median(values)), "max": float(values.max())}
    # Report boundary changes alongside ordinary adjacent pixels; torus edges
    # are neighbours, not duplicate pixels, so equality is not a seam criterion.
    result["boundary_steps_u8"] = {}
    for name, pixels in maps.items():
        values = pixels.astype(float)
        edges = np.concatenate(((values[0] - values[-1]).ravel(), (values[:, 0] - values[:, -1]).ravel()))
        inside = np.concatenate((np.diff(values, axis=0).ravel(), np.diff(values, axis=1).ravel()))
        result["boundary_steps_u8"][name] = {"edge_p95": float(np.percentile(np.abs(edges), 95)),
                                                "interior_p95": float(np.percentile(np.abs(inside), 95))}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=DIRECTORY)
    parser.add_argument("--self-check", action="store_true")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--dry-run", action="store_true")
    modes.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if args.self_check:
        self_check()
        if not (args.dry_run or args.verify_only):
            return
    paths = {name: args.directory / (name + ".png") for name in ("albedo_source", "height_source")}
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        parser.error("Authored inputs are not ready: " + ", ".join(missing))
    with Image.open(paths["albedo_source"]) as source_albedo, Image.open(paths["height_source"]) as source_height:
        dimensions = source_albedo.size
        if source_height.size != dimensions or dimensions[0] != dimensions[1]:
            parser.error("Albedo and height must be aligned square sources with matching dimensions")
        albedo_pixels = np.asarray(source_albedo.convert("RGB").resize((SIZE, SIZE), Image.Resampling.LANCZOS))
        albedo = albedo_pixels.astype(float) / 255
        height = np.asarray(source_height.convert("L").resize((SIZE, SIZE), Image.Resampling.LANCZOS), dtype=float) / 255
    maps, gain = encode(height, albedo)
    maps["albedo"] = albedo_pixels
    if args.verify_only:
        for name, pixels in maps.items():
            path = args.directory / (name + ".png")
            with Image.open(path) as image:
                actual = np.asarray(image)
            if not np.array_equal(actual, pixels):
                raise ValueError(f"Existing map differs from the declared deterministic encoding: {path}")
    elif not args.dry_run:
        for name, pixels in maps.items():
            save_image(args.directory / (name + ".png"), pixels)
    print(json.dumps({"mode": "verify" if args.verify_only else "dry-run" if args.dry_run else "encode",
                      "directory": str(args.directory.resolve()), "source_dimensions": dimensions,
                      "output_dimensions": [SIZE, SIZE], "profile": PROFILE,
                      "source_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in paths.items()},
                      "statistics": statistics(maps, gain)}, indent=2))


if __name__ == "__main__":
    main()
