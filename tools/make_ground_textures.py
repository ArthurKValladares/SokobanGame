#!/usr/bin/env python3
"""Generate the ground splatting textures.

Produces PNGs in `assets/custom/textures/`:

- `ground_grass.png` — the base ground layer. Tiles seamlessly.
- `ground_rock.png`  — the detail layer blended on top. Tiles seamlessly.
- `ground_splat.png` — the shared blend weight map, used by any screen without
  one of its own.
- `ground_splat_level<N>_screen<M>.png` — one blend map per screen, so every
  screen gets its own grass/rock layout.

The two material layers tile; the splat maps deliberately do not. Each splat
map covers exactly one screen's board at SPLAT_TEXELS_PER_TILE texels per tile,
which is what makes it paintable in the level editor: a repeating map would
echo every brush stroke across the board.

The per-screen maps are discovered from `levels/level<N>/screen<M>.scr`, not
hard-coded: after adding or removing a screen, re-run this script and add or
remove the matching `GroundSplatMap<N>_<M>` manifest entry. A screen with no map
of its own falls back to the shared `ground_splat.png`, so a missing file
degrades to the old look rather than to untextured ground.

The material layers must tile seamlessly, because the world-grid UVs used by
`shaders/ground_splat.frag.glsl` wrap across tile boundaries: any seam would
appear as a hard grid line across the board.

Everything is generated from value noise with a fixed seed, so re-running the
script reproduces the exact same bytes (no diff churn). Only the standard
library is used — PNGs are encoded by hand with `zlib` — so this runs on a
bare Python install without Pillow/numpy.

Tweakable knobs live in the CONSTANTS block below:
- TEXTURE_SIZE / SPLAT_SIZE: resolution of the layers and the blend map.
- *_COLOR_*: the two endpoint colors each layer interpolates between.
- *_OCTAVES / *_BASE_FREQUENCY: noise detail. More octaves = finer grain.
- SPLAT_*: control the blend map's patch scale and how hard the transition is
  (SPLAT_CONTRAST 1.0 = smooth gradient, higher = crisper rock patches).

Run from the repository root:  python tools/make_ground_textures.py

Existing splat maps are never overwritten: once a map exists it may have been
painted in the level editor, and generated and painted bytes are
indistinguishable. Pass --force to regenerate them anyway (destroying painted
work), and --prune to delete maps whose screen no longer exists.
Outputs are committed assets; re-run only when a knob changes or a screen is
added, then rebuild so the content pipeline re-stages them.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import random
import re
import struct
import sys
import zlib

# --- Tweakable constants ----------------------------------------------------

TEXTURE_SIZE = 256
# Size of the shared fallback map, which belongs to no board.
SPLAT_SIZE = 256
# Per-screen maps are exactly this many texels per board tile. The shader
# recovers the board size by dividing the map's own dimensions by this, so
# GROUND_SPLAT_TEXELS_PER_TILE in shaders/ground_splat.frag.glsl must match.
# Raising it gives finer brush detail at 4x the bytes per doubling.
SPLAT_TEXELS_PER_TILE = 32

# Grass: dark base to lighter blade highlights.
GRASS_COLOR_LOW = (46, 82, 38)
GRASS_COLOR_HIGH = (104, 148, 66)
GRASS_OCTAVES = 5
GRASS_BASE_FREQUENCY = 8
GRASS_SPECKLE = 0.10

# Rock: cool grey with brighter chipped highlights.
ROCK_COLOR_LOW = (78, 76, 74)
ROCK_COLOR_HIGH = (150, 148, 145)
ROCK_OCTAVES = 6
ROCK_BASE_FREQUENCY = 6
ROCK_SPECKLE = 0.14

# Splat map: large soft patches of rock over grass.
SPLAT_BASE_FREQUENCY = 3
SPLAT_OCTAVES = 4
SPLAT_CONTRAST = 1.9
SPLAT_BIAS = -0.04

# Each screen's map is generated from
# SPLAT_SEED + LEVEL_SEED_STRIDE * (level + 1) + screen + 1, so every screen
# looks different from every other and from the shared fallback while staying
# reproducible. The stride is larger than any plausible screen count, which is
# what keeps two different (level, screen) pairs off the same seed. Changing
# either constant re-rolls every screen's layout.
LEVEL_SEED_STRIDE = 1000

SEED = 20240607
SPLAT_SEED = SEED + 2027

OUTPUT_DIRECTORY = pathlib.Path("assets/custom/textures")
LEVEL_DIRECTORY = pathlib.Path("levels")
MANIFEST_PATH = pathlib.Path("assets/manifest.json")


# --- Tiling value noise -----------------------------------------------------


def _lattice(frequency: int, seed: int) -> list[list[float]]:
    """Random values on a frequency x frequency torus (wraps by construction)."""
    generator = random.Random(seed)
    return [
        [generator.random() for _ in range(frequency)]
        for _ in range(frequency)
    ]


def _smoothstep(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def _sample_lattice(grid: list[list[float]], u: float, v: float) -> float:
    """Bilinear sample with smoothstep easing; wraps at the lattice edges."""
    size = len(grid)
    x = u * size
    y = v * size
    x0 = int(math.floor(x)) % size
    y0 = int(math.floor(y)) % size
    x1 = (x0 + 1) % size
    y1 = (y0 + 1) % size
    fx = _smoothstep(x - math.floor(x))
    fy = _smoothstep(y - math.floor(y))
    top = grid[y0][x0] * (1.0 - fx) + grid[y0][x1] * fx
    bottom = grid[y1][x0] * (1.0 - fx) + grid[y1][x1] * fx
    return top * (1.0 - fy) + bottom * fy


def tiling_fbm(
    size: int,
    octaves: int,
    base_frequency: int,
    seed: int,
) -> list[list[float]]:
    """Fractal value noise on a torus, normalized to 0..1."""
    grids = [
        _lattice(base_frequency * (2**octave), seed + octave * 7919)
        for octave in range(octaves)
    ]
    field = [[0.0] * size for _ in range(size)]
    total_amplitude = 0.0
    amplitude = 1.0
    for octave in range(octaves):
        grid = grids[octave]
        for y in range(size):
            v = y / size
            row = field[y]
            for x in range(size):
                row[x] += amplitude * _sample_lattice(grid, x / size, v)
        total_amplitude += amplitude
        amplitude *= 0.5

    lowest = min(min(row) for row in field)
    highest = max(max(row) for row in field)
    span = max(highest - lowest, 1e-6)
    return [[(value - lowest) / span for value in row] for row in field]


def _mix(low: tuple[int, int, int], high: tuple[int, int, int], t: float):
    t = min(max(t, 0.0), 1.0)
    return tuple(
        int(round(low[channel] + (high[channel] - low[channel]) * t))
        for channel in range(3)
    )


# --- PNG encoding (stdlib only) ---------------------------------------------


def write_png(
    path: pathlib.Path,
    rows: list[bytes],
    width: int,
    height: int,
    grayscale: bool = False,
) -> None:
    """Writes an 8-bit PNG. `rows` holds `height` rows of `channels*width` bytes.

    Splat maps are written greyscale so they match what the in-game painter
    saves (`src/engine/render/PngWriter.cpp`); a map that has been painted and
    one that has only been generated should not differ in format.
    """
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter type 0 (None) keeps the encoder trivial
        raw.extend(row)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    # Colour type 0 = greyscale, 2 = RGB.
    header = struct.pack(
        ">IIBBBBB", width, height, 8, 0 if grayscale else 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


# --- Layer generation -------------------------------------------------------


def generate_material(
    size: int,
    octaves: int,
    base_frequency: int,
    color_low: tuple[int, int, int],
    color_high: tuple[int, int, int],
    speckle: float,
    seed: int,
) -> list[bytes]:
    field = tiling_fbm(size, octaves, base_frequency, seed)
    generator = random.Random(seed ^ 0x5EED)
    rows: list[bytes] = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            value = field[y][x]
            # Per-texel speckle adds grain without breaking tiling (it is
            # independent of position continuity across the seam).
            value += (generator.random() - 0.5) * speckle
            row.extend(_mix(color_low, color_high, value))
        rows.append(bytes(row))
    return rows


def generate_splat(width: int, height: int, seed: int) -> list[bytes]:
    # Splat maps no longer tile - each one covers its board exactly - but the
    # noise lattice is still generated on a square torus and sampled into the
    # board's aspect, which keeps patch size uniform on non-square boards.
    lattice = max(width, height)
    field = tiling_fbm(lattice, SPLAT_OCTAVES, SPLAT_BASE_FREQUENCY, seed)
    rows: list[bytes] = []
    for y in range(height):
        row = bytearray()
        source_y = y * lattice // max(height, 1)
        for x in range(width):
            source_x = x * lattice // max(width, 1)
            # Center, apply contrast, recenter: pushes the mix toward clean
            # grass or clean rock while keeping soft transition bands.
            centered = (
                field[source_y][source_x] + SPLAT_BIAS - 0.5
            ) * SPLAT_CONTRAST + 0.5
            # One channel: a weight map has nothing to say in green and blue,
            # and the shader only reads red (greyscale decodes to r=g=b).
            row.append(int(round(min(max(centered, 0.0), 1.0) * 255)))
        rows.append(bytes(row))
    return rows


def _numbered_children(
    root: pathlib.Path, prefix: str, suffix: str, directories: bool
) -> list[int]:
    """Indices of `<prefix><N><suffix>` entries under `root`, sorted ascending."""
    if not root.is_dir():
        return []
    indices: list[int] = []
    for entry in root.iterdir():
        if entry.is_dir() != directories:
            continue
        name = entry.name
        if not name.startswith(prefix) or not name.endswith(suffix):
            continue
        middle = name[len(prefix):len(name) - len(suffix)] if suffix else name[len(prefix):]
        if middle.isdigit():
            indices.append(int(middle))
    return sorted(indices)


def discover_screens(root: pathlib.Path) -> list[tuple[int, int]]:
    """`(level, screen)` pairs found under `levels/level<N>/screen<M>.scr`.

    Derived from the tree rather than a constant, so adding a level or a screen
    and re-running the script is enough. `levels/Deleted` and anything else not
    matching the naming convention is ignored.
    """
    screens: list[tuple[int, int]] = []
    for level in _numbered_children(root, "level", "", directories=True):
        for screen in _numbered_children(
            root / f"level{level}", "screen", ".scr", directories=False
        ):
            screens.append((level, screen))
    return screens


def board_size(path: pathlib.Path) -> tuple[int, int]:
    """`(width, height)` of a `.scr` board, in tiles.

    Mirrors `Level::parseDefinition`: `@layer`/`@water` directives and empty
    lines are skipped, every other line is a row, and the board is as wide as
    its longest row and as tall as its tallest layer. Rows of spaces are real
    rows - only truly empty lines separate layers.
    """
    width = 0
    height = 0
    rows_in_layer = 0
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("@water"):
            continue
        if raw.startswith("@layer"):
            height = max(height, rows_in_layer)
            rows_in_layer = 0
            continue
        if not raw:
            continue
        rows_in_layer += 1
        width = max(width, len(raw))
    height = max(height, rows_in_layer)
    if width == 0 or height == 0:
        raise ValueError(f"{path}: could not determine board size")
    return width, height


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate the ground splatting textures.")
    parser.add_argument(
        "--force",
        action="store_true",
        help="Regenerate splat maps that already exist. WITHOUT this flag "
             "existing maps are left alone, because they may have been "
             "painted in the level editor and regenerating destroys that work.",
    )
    parser.add_argument(
        "--prune",
        action="store_true",
        help="Delete maps whose screen no longer exists. Off by default: a "
             "screen removed by accident would otherwise take its painted map "
             "with it.",
    )
    arguments = parser.parse_args(argv)

    # The two material layers are pure functions of the constants above - there
    # is nothing user-authored in them - so they are always rewritten.
    grass = generate_material(
        TEXTURE_SIZE,
        GRASS_OCTAVES,
        GRASS_BASE_FREQUENCY,
        GRASS_COLOR_LOW,
        GRASS_COLOR_HIGH,
        GRASS_SPECKLE,
        SEED,
    )
    write_png(OUTPUT_DIRECTORY / "ground_grass.png", grass, TEXTURE_SIZE, TEXTURE_SIZE)

    rock = generate_material(
        TEXTURE_SIZE,
        ROCK_OCTAVES,
        ROCK_BASE_FREQUENCY,
        ROCK_COLOR_LOW,
        ROCK_COLOR_HIGH,
        ROCK_SPECKLE,
        SEED + 1013,
    )
    write_png(OUTPUT_DIRECTORY / "ground_rock.png", rock, TEXTURE_SIZE, TEXTURE_SIZE)

    written = ["ground_grass.png", "ground_rock.png"]
    kept = 0

    # The shared map is the fallback for any screen without one of its own, and
    # the only map the level editor previews when the document is not a screen.
    # It has no board, so it uses the default square size; the shader derives
    # coverage from the map's dimensions either way.
    shared = OUTPUT_DIRECTORY / "ground_splat.png"
    if arguments.force or not shared.is_file():
        splat = generate_splat(SPLAT_SIZE, SPLAT_SIZE, SPLAT_SEED)
        write_png(shared, splat, SPLAT_SIZE, SPLAT_SIZE, grayscale=True)
    else:
        kept += 1
    written.append("ground_splat.png")

    screens = discover_screens(LEVEL_DIRECTORY)
    if not screens:
        print(
            f"warning: no level<N>/screen<M>.scr files under {LEVEL_DIRECTORY}; "
            "only the shared splat map was written",
            file=sys.stderr,
        )
    for level, screen in screens:
        name = f"ground_splat_level{level}_screen{screen}.png"
        written.append(name)
        destination = OUTPUT_DIRECTORY / name

        # Existing maps are left alone. Once a map exists it may have been
        # painted in the level editor, and there is no way to tell painted
        # bytes from generated ones - so the safe default is to never
        # regenerate, and to make destroying that work an explicit --force.
        if destination.is_file() and not arguments.force:
            kept += 1
            continue

        source = LEVEL_DIRECTORY / f"level{level}" / f"screen{screen}.scr"
        tiles_wide, tiles_high = board_size(source)
        # The shader divides the map's own dimensions by this constant to
        # recover the board size, so the two must agree exactly.
        width = tiles_wide * SPLAT_TEXELS_PER_TILE
        height = tiles_high * SPLAT_TEXELS_PER_TILE
        screen_splat = generate_splat(
            width,
            height,
            SPLAT_SEED + LEVEL_SEED_STRIDE * (level + 1) + screen + 1,
        )
        write_png(destination, screen_splat, width, height, grayscale=True)
        print(f"  wrote {name}: {tiles_wide}x{tiles_high} tiles -> {width}x{height}px")

    if kept:
        print(f"kept {kept} existing splat map(s); pass --force to regenerate "
              "(this discards anything painted in the level editor)")

    # Screens come and go. A map whose screen is gone is orphaned, but deleting
    # it by default would throw away painted work the moment a screen is
    # removed by accident, so say so and let --prune do it.
    stale = sorted(
        path for path in OUTPUT_DIRECTORY.glob("ground_splat_level*.png")
        if path.name not in written)
    for path in stale:
        if arguments.prune:
            path.unlink()
            print(f"pruned {path} (no such screen)")
        else:
            print(f"note: {path} has no matching screen; pass --prune to delete",
                  file=sys.stderr)

    for name in written:
        path = OUTPUT_DIRECTORY / name
        print(f"{path}: {path.stat().st_size} bytes")

    return check_manifest(screens) | check_texel_density()


def check_texel_density() -> int:
    """Fails if the shader or the canvas disagree about texels per tile.

    Three places encode this number and all three must match: the shader
    divides a map's dimensions by it to recover the board size, the canvas
    converts world tiles to texels with it, and this script sizes the files
    with it. A mismatch is silent and disastrous - every brush stroke lands
    offset and rescaled - so it is checked here, where the maps are written.
    """
    sources = {
        "shaders/ground_splat.frag.glsl":
            r"GROUND_SPLAT_TEXELS_PER_TILE\s*=\s*([0-9]+)",
        "src/engine/SplatCanvas.hpp":
            r"texelsPerTile\s*=\s*([0-9]+)",
    }
    failed = 0
    for path, pattern in sources.items():
        file = pathlib.Path(path)
        if not file.is_file():
            print(f"warning: {path} not found; skipped density check",
                  file=sys.stderr)
            continue
        match = re.search(pattern, file.read_text(encoding="utf-8"))
        if not match:
            print(f"warning: could not read texels-per-tile from {path}",
                  file=sys.stderr)
            failed = 1
            continue
        if int(match.group(1)) != SPLAT_TEXELS_PER_TILE:
            print(
                f"error: {path} uses {match.group(1)} texels per tile but this "
                f"script uses {SPLAT_TEXELS_PER_TILE}; strokes would land "
                "offset. Make them agree and re-run.",
                file=sys.stderr,
            )
            failed = 1
    return failed


def check_manifest(screens: list[tuple[int, int]]) -> int:
    """Report screens whose manifest entry is missing, and stale entries.

    Writing the PNG is only half the job: without a `GroundSplatMap<N>_<M>`
    texture in the manifest the screen silently falls back to the shared map,
    which still renders and so is easy to miss. A stale entry is louder (the
    content pipeline fails on the missing path) but is worth reporting here
    too, where the fix is obvious.
    """
    if not MANIFEST_PATH.is_file():
        print(f"warning: {MANIFEST_PATH} not found; skipped manifest check",
              file=sys.stderr)
        return 0

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    declared = {
        texture["name"]
        for texture in manifest.get("textures", [])
        if re.fullmatch(r"GroundSplatMap\d+_\d+", texture["name"])
    }
    expected = {f"GroundSplatMap{level}_{screen}" for level, screen in screens}

    missing = sorted(expected - declared)
    stale = sorted(declared - expected)
    for name in missing:
        print(f"warning: {MANIFEST_PATH} has no texture named {name}; "
              "that screen will fall back to the shared splat map",
              file=sys.stderr)
    for name in stale:
        print(f"warning: {MANIFEST_PATH} declares {name}, but no such screen "
              "exists; remove it or the content pipeline will fail",
              file=sys.stderr)
    if missing or stale:
        return 1
    print(f"manifest: {len(expected)} per-screen splat maps declared")
    return 0


if __name__ == "__main__":
    sys.exit(main())
