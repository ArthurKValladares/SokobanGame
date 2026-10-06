"""Small glTF data helpers shared by the PBR authoring and audit tools."""
from __future__ import annotations

import base64
import io
import json
import struct
from pathlib import Path
from urllib.parse import unquote

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
COMPONENTS = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Document:
    def __init__(self, path: Path):
        self.path = path
        raw = path.read_bytes()
        binary = b""
        if raw[:4] == b"glTF":
            magic, version, length = struct.unpack_from("<III", raw)
            assert version == 2 and length == len(raw), path
            cursor = 12
            while cursor < length:
                size, kind = struct.unpack_from("<II", raw, cursor)
                chunk = raw[cursor + 8:cursor + 8 + size]
                if kind == 0x4E4F534A:
                    self.data = json.loads(chunk)
                elif kind == 0x004E4942:
                    binary = chunk
                cursor += 8 + size
        else:
            self.data = json.loads(raw)
        self.buffers = [self.uri_bytes(b["uri"]) if "uri" in b else binary
                        for b in self.data.get("buffers", [])]

    def uri_bytes(self, uri):
        if uri.startswith("data:"):
            return base64.b64decode(uri.split(",", 1)[1])
        return (self.path.parent / unquote(uri)).read_bytes()

    def view_bytes(self, index):
        view = self.data["bufferViews"][index]
        start = view.get("byteOffset", 0)
        return self.buffers[view["buffer"]][start:start + view["byteLength"]]

    def accessor(self, index, normalized=True):
        a = self.data["accessors"][index]
        if "sparse" in a:
            raise ValueError(f"Sparse accessor requires explicit support: {self.path}")
        dtype = np.dtype(COMPONENTS[a["componentType"]])
        width = WIDTHS[a["type"]]
        view = self.data["bufferViews"][a["bufferView"]]
        offset = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = view.get("byteStride", dtype.itemsize * width)
        result = np.ndarray((a["count"], width), dtype=dtype,
                            buffer=self.buffers[view["buffer"]], offset=offset,
                            strides=(stride, dtype.itemsize)).copy()
        if normalized and a.get("normalized", False) and a["componentType"] != 5126:
            limit = np.iinfo(dtype).max
            result = np.maximum(result.astype(float) / limit, -1)
        return result

    def image(self, texture_index):
        texture = self.data["textures"][texture_index]
        source = self.data["images"][texture["source"]]
        raw = self.uri_bytes(source["uri"]) if "uri" in source else self.view_bytes(source["bufferView"])
        return Image.open(io.BytesIO(raw)).convert("RGB")


def palette_sample(image, uvs):
    pixels = np.asarray(image)
    # glTF (0,0) is the top-left image texel; do not vertically flip a palette.
    x = np.clip((uvs[:, 0] * image.width).astype(int), 0, image.width - 1)
    y = np.clip((uvs[:, 1] * image.height).astype(int), 0, image.height - 1)
    return pixels[y, x]


def write_glb(path, data, binary):
    data["buffers"] = [{"byteLength": len(binary)}]
    encoded = json.dumps(data, separators=(",", ":"), ensure_ascii=True).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary += b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    result = struct.pack("<III", 0x46546C67, 2, total)
    result += struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    result += struct.pack("<II", len(binary), 0x004E4942) + binary
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != result:
        path.write_bytes(result)
