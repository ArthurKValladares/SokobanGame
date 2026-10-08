#!/usr/bin/env python3
"""Finalize Blender's shared-image GLB exports without changing wall geometry.

Blender 5.0 Keep Original textures may emit both an image URI and a placeholder
bufferView. glTF requires exactly one image source. Retain each original PNG URI,
remove its redundant image bufferView reference. Three very thin backing-triangle
tips also receive zero Mikk tangents because their float angle weight rounds to
zero. Repair only these records from agreeing, same-face authored frames.
Positions, normals, UVs, indices and all other binary data remain byte-identical.
Existing valid files are left unchanged.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from pbr_gltf import Document

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"


def repair_zero_tangents(document: Document, chunks: bytes) -> tuple[bytes, int]:
    """Fill isolated zero frames only when adjacent authored frames agree."""
    data = document.data
    output = bytearray(chunks)
    allowed = []
    repaired = 0
    for mesh in data.get("meshes", []):
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            if "TANGENT" not in attributes:
                continue
            tangents = document.accessor(attributes["TANGENT"]).astype(np.float64)
            bad = np.flatnonzero(np.linalg.norm(tangents[:, :3], axis=1) < 1e-12)
            if not len(bad):
                continue
            normals = document.accessor(attributes["NORMAL"]).astype(np.float64)
            indices = document.accessor(primitive["indices"]).reshape(-1, 3)
            accessor = data["accessors"][attributes["TANGENT"]]
            view = data["bufferViews"][accessor["bufferView"]]
            if (accessor["componentType"] != 5126 or accessor["type"] != "VEC4"
                    or view["buffer"] != 0 or "sparse" in accessor):
                raise ValueError("Tangent repair requires a direct float4 BIN accessor")
            size, kind = struct.unpack_from("<II", chunks)
            if kind != 0x004E4942 or size + 8 != len(chunks):
                raise ValueError("Tangent repair requires one complete BIN chunk")
            for vertex in bad:
                incident = indices[np.any(indices == vertex, axis=1)]
                neighbours = np.unique(incident)
                neighbours = neighbours[neighbours != vertex]
                usable = np.linalg.norm(tangents[neighbours, :3], axis=1) > .99
                neighbours = neighbours[usable]
                if not len(neighbours):
                    raise ValueError("Zero tangent has no usable adjacent frame")
                n = normals[vertex] / np.linalg.norm(normals[vertex])
                adjacent_n = normals[neighbours]
                adjacent_n /= np.linalg.norm(adjacent_n, axis=1)[:, None]
                frames = tangents[neighbours, :3]
                frames /= np.linalg.norm(frames, axis=1)[:, None]
                signs = tangents[neighbours, 3]
                if (np.min(adjacent_n @ n) < .999999
                        or np.min(frames @ frames[0]) < .999
                        or not np.all(signs == tangents[vertex, 3])
                        or tangents[vertex, 3] not in (-1, 1)):
                    raise ValueError("Adjacent authored frames do not agree; refusing tangent repair")
                t = frames.mean(axis=0)
                t -= n * np.dot(n, t)
                t /= np.linalg.norm(t)
                offset = (8 + view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
                          + int(vertex) * view.get("byteStride", 16))
                view_end = 8 + view.get("byteOffset", 0) + view["byteLength"]
                if offset + 16 > view_end:
                    raise ValueError("Tangent record falls outside its bufferView")
                struct.pack_into("<4f", output, offset, *t, tangents[vertex, 3])
                allowed.append((offset, offset + 16))
                repaired += 1
    changed = np.flatnonzero(np.frombuffer(chunks, dtype=np.uint8)
                             != np.frombuffer(output, dtype=np.uint8))
    if any(not any(start <= i < end for start, end in allowed) for i in changed):
        raise AssertionError("Binary data outside repaired tangent records changed")
    return bytes(output), repaired

def finalize(path: Path) -> dict:
    path = path.resolve()
    if not path.is_relative_to(ROOT):
        raise ValueError("GLB must be inside the project workspace")
    original = path.read_bytes()
    magic, version, total = struct.unpack_from("<4sII", original)
    if magic != b"glTF" or version != 2 or total != len(original):
        raise ValueError("Expected a complete glTF 2 binary")
    length, kind = struct.unpack_from("<II", original, 12)
    if kind != 0x4E4F534A:
        raise ValueError("First GLB chunk must be JSON")
    document = json.loads(original[20:20 + length])
    binary_chunks = original[20 + length:]
    binary_chunks, tangents_repaired = repair_zero_tangents(Document(path), binary_chunks)
    changes = 0
    textures = []
    for image in document.get("images", []):
        uri = image.get("uri")
        if uri is None:
            continue
        if any(char in uri for char in ("\\", ":", "%", "?", "#")):
            raise ValueError("Expected a relative PNG image URI")
        resource = (path.parent / uri).resolve()
        if not resource.is_relative_to(ASSETS) or not resource.is_file():
            raise ValueError(f"Missing/outside-assets image: {uri}")
        if resource.suffix.lower() != ".png":
            raise ValueError("Quarry marble runtime images must be PNG")
        if "bufferView" in image:
            del image["bufferView"]
            changes += 1
        textures.append(str(resource.relative_to(ASSETS)))
    if len(textures) != 3:
        raise ValueError("Expected shared albedo, normal and ORM image references")
    if changes or tangents_repaired:
        data = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        data += b" " * ((-len(data)) % 4)
        output = (struct.pack("<4sII", b"glTF", 2, 20 + len(data) + len(binary_chunks))
                  + struct.pack("<II", len(data), 0x4E4F534A) + data + binary_chunks)
        if output[20 + len(data):] != binary_chunks:
            raise AssertionError("Unexpected binary encoding change")
        path.write_bytes(output)
    return {"file": str(path.relative_to(ROOT)), "image_sources_normalized": changes,
            "zero_tangent_records_repaired": tangents_repaired,
            "shared_pngs": textures,
            "binary_chunks_sha256": hashlib.sha256(binary_chunks).hexdigest()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", nargs="*", type=Path)
    args = parser.parse_args()
    models = args.models or [ASSETS / "custom/models" / f"wall_stone_{i:02}.glb" for i in range(1, 9)]
    for model in models:
        print(json.dumps(finalize(model)))

if __name__ == "__main__":
    main()
