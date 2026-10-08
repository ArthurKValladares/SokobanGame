#!/usr/bin/env python3
"""Normalize native Blender cliff GLB image metadata without changing geometry.

Keep Original textures in Blender 5 may emit both a relative image URI and a
redundant bufferView. Retain the validated PNG URI and remove only that image
field. Binary chunks remain byte-identical unless --repair-zero-tangents is
explicitly requested; that option reuses the conservative same-face repair
from the existing wall exporter and changes only isolated empty float4 frames.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
LIBRARY = ASSETS / "custom/models/cliff_walls"
TYPES = ("island", "end", "strip", "corner", "edge", "interior")


def parse_glb(blob: bytes) -> tuple[dict, bytes]:
    if len(blob) < 20:
        raise ValueError("Truncated GLB")
    magic, version, total = struct.unpack_from("<4sII", blob)
    if magic != b"glTF" or version != 2 or total != len(blob):
        raise ValueError("Expected a complete glTF 2 binary")
    length, kind = struct.unpack_from("<II", blob, 12)
    if kind != 0x4E4F534A or length % 4 or 20 + length > len(blob):
        raise ValueError("First GLB chunk must contain aligned JSON")
    document = json.loads(blob[20:20 + length])
    chunks = blob[20 + length:]
    offset = 0
    while offset < len(chunks):
        if offset + 8 > len(chunks):
            raise ValueError("Truncated GLB chunk header")
        size, chunk_kind = struct.unpack_from("<II", chunks, offset)
        if size % 4 or offset + 8 + size > len(chunks):
            raise ValueError("Truncated or unaligned GLB chunk")
        if chunk_kind == 0x4E4F534A:
            raise ValueError("Unexpected second JSON chunk")
        offset += 8 + size
    return document, chunks


def finalize(path: Path, *, repair_tangents: bool = False,
             verify_only: bool = False) -> dict:
    path = path.resolve(strict=True)
    if not path.is_relative_to(ROOT) or path.suffix.lower() != ".glb":
        raise ValueError("Expected a GLB within the project workspace")
    original = path.read_bytes()
    document, original_chunks = parse_glb(original)
    chunks = original_chunks
    repaired = 0
    if repair_tangents:
        from finalize_wall_marble_gltf import repair_zero_tangents
        from pbr_gltf import Document
        chunks, repaired = repair_zero_tangents(Document(path), original_chunks)

    changes = 0
    pngs = set()
    for image in document.get("images", []):
        uri = image.get("uri")
        if uri is None:
            if "bufferView" not in image:
                raise ValueError("Image has no URI or bufferView")
            continue
        if (not isinstance(uri, str) or not uri or uri.startswith("/")
                or any(char in uri for char in ("\\", ":", "%", "?", "#"))):
            raise ValueError("Expected a relative PNG image URI")
        resource = (path.parent / uri).resolve(strict=True)
        if not resource.is_relative_to(ASSETS) or not resource.is_file():
            raise ValueError(f"Missing/outside-assets image: {uri}")
        if resource.suffix.lower() != ".png":
            raise ValueError(f"Expected a PNG dependency: {uri}")
        with Image.open(resource) as decoded:
            if decoded.format != "PNG":
                raise ValueError(f"Dependency is not PNG data: {uri}")
            decoded.verify()
        if "bufferView" in image:
            del image["bufferView"]
            changes += 1
        pngs.add(resource.relative_to(ASSETS).as_posix())

    if verify_only and (changes or repaired):
        raise ValueError(f"Unfinalized GLB: {changes} redundant image fields, "
                         f"{repaired} empty tangent records")
    if changes or repaired:
        encoded = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        encoded += b" " * ((-len(encoded)) % 4)
        output = (struct.pack("<4sII", b"glTF", 2, 20 + len(encoded) + len(chunks))
                  + struct.pack("<II", len(encoded), 0x4E4F534A) + encoded + chunks)
        parsed, preserved = parse_glb(output)
        if parsed != document or preserved != chunks:
            raise AssertionError("Unexpected GLB serialization change")
        if not repair_tangents and preserved != original_chunks:
            raise AssertionError("Binary payload changed during image normalization")
        temporary = path.with_suffix(".glb.finalize-tmp")
        temporary.write_bytes(output)
        temporary.replace(path)

    return {"file": path.relative_to(ROOT).as_posix(),
            "image_sources_normalized": changes,
            "zero_tangent_records_repaired": repaired,
            "png_dependencies": sorted(pngs),
            "binary_chunks_sha256_before": hashlib.sha256(original_chunks).hexdigest(),
            "binary_chunks_sha256_after": hashlib.sha256(chunks).hexdigest(),
            "binary_chunks_unchanged": chunks == original_chunks,
            "verified_only": verify_only}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", nargs="*", type=Path)
    parser.add_argument("--repair-zero-tangents", action="store_true")
    parser.add_argument("--verify", action="store_true",
                        help="Validate dependencies and require already finalized files")
    args = parser.parse_args()
    models = args.models or [LIBRARY / f"cliff_wall_{kind}_{variant}.glb"
                            for kind in TYPES for variant in ("a", "b")]
    if not args.models:
        models.append(LIBRARY / "cliff_wall_top.glb")
    for model in models:
        print(json.dumps(finalize(model, repair_tangents=args.repair_zero_tangents,
                                  verify_only=args.verify)))


if __name__ == "__main__":
    main()
