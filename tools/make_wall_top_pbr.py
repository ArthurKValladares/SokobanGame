"""Encode only new wall-top data maps; retain accepted albedo artwork byte-for-byte.

Relief comes from the aligned, authored ImageGen height sources. The periodic
height math matches tools/make_terrain_pbr.py. This is texture-data encoding,
not an edit to the painted albedo or height artwork.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / 'tools'))
from make_terrain_pbr import periodic_height, blur
from make_pbr_art_pass import save_image

PROFILES = {
    'wall_slate_rock': {'gain': 12.0, 'roughness': .88, 'variation': .05, 'aoDepth': .12},
    'wall_moss': {'gain': 10.0, 'roughness': .94, 'variation': .035, 'aoDepth': .10},
}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

def encode(stem, profile):
    source = ROOT / 'assets/custom/textures' / (stem + '.png')
    height_source = ROOT / 'assets/custom/pbr' / (stem + '_height_source.png')
    original_hash = digest(source)
    original_height_hash = digest(height_source)
    image = Image.open(source).convert('RGB')
    height_image = Image.open(height_source).convert('L')
    width, height_pixels = image.size
    assert width == height_pixels and width >= 256 and width & (width - 1) == 0, (
        f'{source.name}: accepted albedo must be square and power-of-two; got {image.size}')
    assert height_image.size == image.size, f'{stem}: albedo and authored height dimensions differ'
    height = np.asarray(height_image, dtype=float) / 255.0
    height = blur(periodic_height(height), 1.0)
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
    normal = np.stack((-dx * profile['gain'], -dy * profile['gain'], np.ones_like(height)), axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    encoded_normal = np.rint((normal * .5 + .5) * 255).astype(np.uint8)
    recess = np.maximum(blur(height, 8) - height, 0)
    recess /= max(float(np.percentile(recess, 99)), .001)
    cavity = np.clip(recess, 0, 1)
    grain = np.clip((height - blur(height, 3)) * 12, -1, 1)
    orm = np.stack((1 - profile['aoDepth'] * cavity,
                    np.clip(profile['roughness'] + profile['variation'] * cavity + profile['variation'] * grain, .72, .99),
                    np.zeros_like(height)), axis=-1)
    encoded_orm = np.rint(orm * 255).astype(np.uint8)
    output = ROOT / 'assets/custom/pbr'
    paths = [output / (stem + '_normal.png'), output / (stem + '_orm.png')]
    for path, pixels in zip(paths, [encoded_normal, encoded_orm]):
        save_image(path, pixels)
    assert digest(source) == original_hash, source.name + ': albedo changed'
    assert digest(height_source) == original_height_hash, height_source.name + ': authored height changed'
    angles = np.rad2deg(np.arccos(np.clip(normal[..., 2], -1, 1)))
    return {'stem': stem, 'size': list(image.size), 'profile': profile,
            'albedoSha256': original_hash, 'albedoPreserved': True,
            'heightSourceSha256': original_height_hash, 'heightSourcePreserved': True,
            'normalAnglePercentiles': np.percentile(angles, [50, 90, 99, 100]).tolist(),
            'roughnessRange': [float(orm[..., 1].min()), float(orm[..., 1].max())],
            'aoRange': [float(orm[..., 0].min()), float(orm[..., 0].max())],
            'metallicRange': [0, 0],
            'maps': [{'path': str(path.relative_to(ROOT)), 'sha256': digest(path)} for path in paths]}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path,
                        default=ROOT / 'out/cliff-wall-models/wall-top-pbr-encoding.json')
    args = parser.parse_args()
    rows = [encode(stem, profile) for stem, profile in PROFILES.items()]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({'status': 'PASS', 'materials': rows}, indent=2)+'\n', encoding='utf-8')
    for row in rows:
        print(f"{row['stem']}: {row['size'][0]}x{row['size'][1]} normal/ORM encoded; albedo unchanged; p90 normal tilt {row['normalAnglePercentiles'][1]:.2f}deg")

if __name__ == '__main__':
    main()
