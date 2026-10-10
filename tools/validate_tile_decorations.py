#!/usr/bin/env python3
"""Read-only audit of the Blender tile-decoration catalog and its 48 GLBs.

Run with Python 3.10 or later; no third-party modules are required. Geometry
is checked in Blender coordinates (glTF x,y,z maps back to x,-z,y). Node
transforms must be identity because the game's static loader ignores them.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = ROOT / "assets/custom/source/tile_decorations/catalog.json"
DEFAULT_MODELS = ROOT / "assets/custom/models/tile_decorations"
CONFIGURATIONS = {"edge": 1, "corner": 3, "strip": 5, "end": 11}
STYLES = ("pebbles", "grass", "moss")
COMPONENTS = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
              5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
          "MAT2": 4, "MAT3": 9, "MAT4": 16}
TOLERANCE = 2e-5


def require(condition, message):
    if not condition:
        raise ValueError(message)


def indexed(items, index, label):
    require(type(index) is int and 0 <= index < len(items), f"Invalid {label} index: {index}")
    return items[index]


def finite_vector(value, size, label):
    require(isinstance(value, (list, tuple)) and len(value) == size,
            f"{label}: expected {size} components")
    require(all(isinstance(x, (int, float)) and math.isfinite(x) for x in value),
            f"{label}: nonfinite component")
    return value


def close_vector(actual, expected, tolerance=TOLERANCE):
    return len(actual) == len(expected) and all(abs(a-b) <= tolerance for a, b in zip(actual, expected))


class Glb:
    def __init__(self, path):
        self.path = path
        payload = path.read_bytes()
        require(len(payload) >= 20, "Truncated GLB header")
        magic, version, size = struct.unpack_from("<4sII", payload)
        require(magic == b"glTF" and version == 2 and size == len(payload), "Invalid GLB header")
        chunks = []
        offset = 12
        while offset < len(payload):
            require(offset+8 <= len(payload), "Truncated GLB chunk header")
            length, kind = struct.unpack_from("<II", payload, offset)
            offset += 8
            require(length % 4 == 0 and offset+length <= len(payload), "Invalid GLB chunk extent")
            chunks.append((kind, payload[offset:offset+length]))
            offset += length
        require(len(chunks) == 2 and chunks[0][0] == 0x4e4f534a and chunks[1][0] == 0x004e4942,
                "Expected exactly a JSON chunk and an embedded BIN chunk")
        self.data = json.loads(chunks[0][1].decode("utf-8").rstrip(" \x00"))
        self.binary = chunks[1][1]
        require(self.data.get("asset", {}).get("version") == "2.0", "Expected glTF 2.0")
        buffers = self.data.get("buffers", [])
        require(len(buffers) == 1 and "uri" not in buffers[0], "Expected one embedded GLB buffer")
        byte_length = buffers[0].get("byteLength")
        require(type(byte_length) is int and 0 < byte_length <= len(self.binary)
                and len(self.binary)-byte_length <= 3, "Invalid embedded buffer length")
        self.binary = self.binary[:byte_length]
        for i in range(len(self.data.get("bufferViews", []))):
            self.view(i)

    def view(self, index):
        view = indexed(self.data.get("bufferViews", []), index, "bufferView")
        start, length = view.get("byteOffset", 0), view.get("byteLength")
        require(view.get("buffer") == 0 and type(start) is int and type(length) is int
                and start >= 0 and length > 0 and start+length <= len(self.binary),
                "Buffer view extends outside embedded buffer")
        return view, memoryview(self.binary)[start:start+length]

    def accessor(self, index):
        item = indexed(self.data.get("accessors", []), index, "accessor")
        require("sparse" not in item, "Sparse accessors are unsupported by this asset contract")
        component = item.get("componentType")
        require(component in COMPONENTS and item.get("type") in WIDTHS, "Invalid accessor format")
        fmt, size = COMPONENTS[component]
        width = WIDTHS[item["type"]]
        count = item.get("count")
        start = item.get("byteOffset", 0)
        view, payload = self.view(item.get("bufferView"))
        stride = view.get("byteStride", size*width)
        require(type(count) is int and count > 0 and type(start) is int and start >= 0
                and type(stride) is int and stride >= size*width and stride % size == 0
                and start % size == 0 and start+(count-1)*stride+size*width <= len(payload),
                "Accessor extends outside buffer view or has invalid stride")
        values = [struct.unpack_from("<"+fmt*width, payload, start+i*stride) for i in range(count)]
        require(all(math.isfinite(x) for row in values for x in row), "Nonfinite accessor component")
        for key in ("min", "max"):
            if key in item:
                expected = finite_vector(item[key], width, f"Accessor {key}")
                actual = [min(row[j] for row in values) if key == "min" else max(row[j] for row in values)
                          for j in range(width)]
                require(close_vector(actual, expected), f"Accessor {key} disagrees with binary data")
        return item, values


def png_dimensions(payload):
    require(payload[:8] == b"\x89PNG\r\n\x1a\n", "Embedded palette is not a PNG")
    offset, dimensions, compressed, ended = 8, None, bytearray(), False
    while offset < len(payload):
        require(offset+12 <= len(payload), "Truncated PNG chunk")
        length = struct.unpack_from(">I", payload, offset)[0]
        kind = bytes(payload[offset+4:offset+8])
        require(offset+length+12 <= len(payload), "PNG chunk exceeds image payload")
        data = payload[offset+8:offset+8+length]
        crc = struct.unpack_from(">I", payload, offset+8+length)[0]
        require(zlib.crc32(kind+data) & 0xffffffff == crc, "PNG chunk CRC mismatch")
        if kind == b"IHDR":
            require(dimensions is None and length == 13 and offset == 8, "Invalid PNG IHDR")
            w, h, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data)
            require(0 < w <= 8192 and 0 < h <= 8192 and compression == 0
                    and filtering == 0 and interlace in (0, 1), "Invalid PNG format or dimensions")
            channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
            valid_depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8),
                            4: (8, 16), 6: (8, 16)}
            require(channels is not None and depth in valid_depths[color], "Invalid PNG color format")
            dimensions = (w, h, depth, channels, interlace)
        elif kind == b"IDAT":
            compressed.extend(data)
        elif kind == b"IEND":
            require(length == 0, "Invalid PNG IEND")
            ended = True
            offset += length+12
            break
        offset += length+12
    require(dimensions is not None and compressed and ended and offset == len(payload), "Incomplete PNG")
    raw = zlib.decompress(compressed)
    w, h, depth, channels, interlace = dimensions
    if interlace == 0:
        row_bytes = (w*depth*channels+7)//8
        require(len(raw) == h*(row_bytes+1), "PNG decompressed data size mismatch")
        require(all(raw[y*(row_bytes+1)] <= 4 for y in range(h)), "Invalid PNG filter byte")
    return [w, h]


def audit_materials(glb):
    data = glb.data
    images, textures = data.get("images", []), data.get("textures", [])
    require(images and textures, "Missing embedded palette texture")
    sizes = []
    for image in images:
        require("uri" not in image and image.get("mimeType") == "image/png", "Expected embedded PNG image")
        _, payload = glb.view(image.get("bufferView"))
        sizes.append(png_dimensions(bytes(payload)))
    for texture in textures:
        indexed(images, texture.get("source"), "image")
        if "sampler" in texture:
            sampler = indexed(data.get("samplers", []), texture["sampler"], "sampler")
            require(sampler.get("wrapS", 10497) in (33071, 33648, 10497)
                    and sampler.get("wrapT", 10497) in (33071, 33648, 10497), "Invalid texture wrapping")
            require(sampler.get("magFilter", 9729) in (9728, 9729)
                    and sampler.get("minFilter", 9987) in (9728, 9729, 9984, 9985, 9986, 9987),
                    "Invalid texture filtering")
    materials = data.get("materials", [])
    require(materials, "Missing PBR material")
    for material in materials:
        pbr = material.get("pbrMetallicRoughness", {})
        factor = finite_vector(pbr.get("baseColorFactor", [1, 1, 1, 1]), 4, "Base color factor")
        require(all(0 <= x <= 1 for x in factor), "Base color factor outside 0..1")
        for key, default in (("metallicFactor", 1), ("roughnessFactor", 1)):
            value = pbr.get(key, default)
            require(isinstance(value, (int, float)) and math.isfinite(value) and 0 <= value <= 1,
                    f"Invalid PBR {key}")
        require(material.get("alphaMode", "OPAQUE") in ("OPAQUE", "MASK", "BLEND"), "Invalid alpha mode")
        require("baseColorTexture" in pbr, "PBR material lacks palette base-color texture")
        specs = [pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture"),
                 material.get("normalTexture"), material.get("occlusionTexture"), material.get("emissiveTexture")]
        for spec in filter(None, specs):
            indexed(textures, spec.get("index"), "texture")
            require(type(spec.get("texCoord", 0)) is int and spec.get("texCoord", 0) >= 0,
                    "Invalid texture coordinate set")
    return sizes


def audit_nodes(glb):
    data = glb.data
    nodes, meshes = data.get("nodes", []), data.get("meshes", [])
    require(nodes and meshes, "Missing mesh nodes")
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    for node in nodes:
        for key, expected in (("translation", [0, 0, 0]), ("rotation", [0, 0, 0, 1]),
                              ("scale", [1, 1, 1]), ("matrix", identity)):
            value = finite_vector(node.get(key, expected), len(expected), f"Node {key}")
            require(close_vector(value, expected, 1e-7), f"Nonidentity node {key}")
        if "mesh" in node:
            indexed(meshes, node["mesh"], "mesh")
        for child in node.get("children", []):
            indexed(nodes, child, "child node")
    scene = indexed(data.get("scenes", []), data.get("scene", 0), "scene")
    require(scene.get("nodes"), "Empty default scene")
    reached, visiting = set(), set()

    def visit(index):
        indexed(nodes, index, "scene node")
        require(index not in visiting and index not in reached, "Cycle or repeated node in scene graph")
        visiting.add(index)
        for child in nodes[index].get("children", []):
            visit(child)
        visiting.remove(index)
        reached.add(index)

    for index in scene["nodes"]:
        visit(index)
    require(len(reached) == len(nodes), "Default scene omits exported nodes")
    require({node["mesh"] for node in nodes if "mesh" in node} == set(range(len(meshes))),
            "Default scene omits exported meshes")


def audit_model(path, entry):
    glb = Glb(path)
    audit_nodes(glb)
    image_sizes = audit_materials(glb)
    points, vertex_count, triangle_count = [], 0, 0
    for mesh in glb.data["meshes"]:
        require(mesh.get("primitives"), "Empty mesh")
        for primitive in mesh["primitives"]:
            require(primitive.get("mode", 4) == 4, "Expected indexed triangle-list primitive")
            attributes = primitive.get("attributes", {})
            require(all(key in attributes for key in ("POSITION", "NORMAL", "TEXCOORD_0")),
                    "Missing POSITION, NORMAL or TEXCOORD_0")
            position_item, positions = glb.accessor(attributes["POSITION"])
            normal_item, normals = glb.accessor(attributes["NORMAL"])
            uv_item, uvs = glb.accessor(attributes["TEXCOORD_0"])
            require(position_item["type"] == normal_item["type"] == "VEC3"
                    and position_item["componentType"] == normal_item["componentType"] == 5126
                    and uv_item["type"] == "VEC2" and uv_item["componentType"] == 5126,
                    "Expected float positions, normals and UV0")
            require(len(positions) == len(normals) == len(uvs), "Vertex attribute count mismatch")
            require(all(abs(sum(x*x for x in n)-1) <= 2e-3 for n in normals), "Nonunit normal")
            for key, accessor in attributes.items():
                _, rows = glb.accessor(accessor)
                require(len(rows) == len(positions), f"{key} vertex count mismatch")
            material = indexed(glb.data["materials"], primitive.get("material"), "material")
            pbr = material.get("pbrMetallicRoughness", {})
            for spec in filter(None, [pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture"),
                                     material.get("normalTexture"), material.get("occlusionTexture"),
                                     material.get("emissiveTexture")]):
                require(f'TEXCOORD_{spec.get("texCoord", 0)}' in attributes, "Material references missing UV set")
            require("normalTexture" not in material or "TANGENT" in attributes,
                    "Normal-mapped material lacks tangent stream")
            index_item, indices = glb.accessor(primitive.get("indices"))
            require(index_item["type"] == "SCALAR" and index_item["componentType"] in (5121, 5123, 5125)
                    and len(indices) % 3 == 0, "Invalid triangle indices")
            indices = [row[0] for row in indices]
            require(all(0 <= i < len(positions) for i in indices), "Triangle index outside vertex stream")
            if entry["configuration"] == "strip":
                require(all(abs(z) >= 0.20-TOLERANCE for _, _, z in positions),
                        "Opposite-edge decoration has vertices inside the empty center band")
            for offset in range(0, len(indices), 3):
                a, b, c = [positions[i] for i in indices[offset:offset+3]]
                if entry["configuration"] == "strip":
                    # glTF Z is negative Blender/game Y. All three vertices
                    # must stay in one band; an edge-to-edge triangle would
                    # cover the middle even if its vertices were outside it.
                    ys = [-point[2] for point in (a, b, c)]
                    require(max(ys) <= -0.20+TOLERANCE or min(ys) >= 0.20-TOLERANCE,
                            "Opposite-edge decoration has a triangle crossing the empty center band")
                u, v = [b[j]-a[j] for j in range(3)], [c[j]-a[j] for j in range(3)]
                cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
                require(sum(x*x for x in cross) > 1e-22, "Degenerate geometry triangle")
            points.extend((x, -z, y) for x, y, z in positions)
            vertex_count += len(positions)
            triangle_count += len(indices)//3
    lower = [min(p[i] for p in points) for i in range(3)]
    upper = [max(p[i] for p in points) for i in range(3)]
    require(all(lower[i] >= -0.5-TOLERANCE and upper[i] <= 0.5+TOLERANCE for i in (0, 1)),
            f"Decoration exceeds one-tile footprint: {lower}, {upper}")
    # Layout masks describe actual border contact, not just which part of the
    # tile contains the cluster. Check every requested edge independently.
    edge_extents = ((lower[1], -0.5, "north"), (upper[0], 0.5, "east"),
                    (upper[1], 0.5, "south"), (lower[0], -0.5, "west"))
    for edge, (actual, target, name) in enumerate(edge_extents):
        if entry["edge_mask"] & (1 << edge):
            require(abs(actual-target) <= TOLERANCE,
                    f"Decoration does not touch requested {name} edge: {actual}")
    require(lower[2] >= -0.03-TOLERANCE and 0 < upper[2] < 0.3,
            f"Decoration height exceeds intended surface range: {lower[2]}..{upper[2]}")
    if entry["style"] == "moss":
        require(upper[2] <= 0.13+TOLERANCE, "Moss must remain a low cushion against the tile")
    bounds = entry.get("bounds")
    if isinstance(bounds, dict):
        expected_min = bounds.get("min", bounds.get("minimum"))
        expected_max = bounds.get("max", bounds.get("maximum"))
    else:
        require(isinstance(bounds, list) and len(bounds) == 2, "Catalog bounds missing")
        expected_min, expected_max = bounds
    finite_vector(expected_min, 3, "Catalog minimum bounds")
    finite_vector(expected_max, 3, "Catalog maximum bounds")
    require(close_vector(lower, expected_min) and close_vector(upper, expected_max),
            "Catalog bounds disagree with exported mesh")
    require(entry.get("triangles") == triangle_count, "Catalog triangle count disagrees with exported mesh")
    require(entry.get("vertex_count") == vertex_count, "Catalog vertex count disagrees with exported mesh")
    if entry["configuration"] == "strip":
        foci = entry.get("focal_points")
        require(isinstance(foci, list) and len(foci) == 2,
                "Opposite-edge decoration requires separate north and south focal points")
        for focus in foci:
            finite_vector(focus, 3, "Opposite-edge focal point")
        require(foci[0][1] <= -0.20 and foci[1][1] >= 0.20,
                "Opposite-edge focal points must lie in their perimeter bands")
        require(close_vector(entry.get("focal_point", []), foci[0]),
                "Primary focal point must match the north-edge focus")
    # Sorting unique points rejects variants that only differ in vertex order.
    canonical = sorted({tuple(round(x, 7) for x in p) for p in points})
    fingerprint = hashlib.sha256(json.dumps(canonical, separators=(",", ":")).encode()).hexdigest()
    return {"id": entry["id"], "triangles": triangle_count, "vertex_count": vertex_count,
            "bounds": {"min": lower, "max": upper}, "embedded_palette_sizes": image_sizes,
            "geometry_sha256": fingerprint}


def audit(catalog_path, models_path):
    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))
    entries = catalog.get("assets") if isinstance(catalog, dict) else catalog
    require(isinstance(entries, list) and len(entries) == 48, "Expected exactly 48 catalog assets")
    expected = {(style, config, variant) for style in STYLES for config in CONFIGURATIONS for variant in range(1, 5)}
    seen, paths, identifiers, reports, errors, warnings = set(), set(), set(), [], [], []
    for entry in entries:
        label = entry.get("id", "<unnamed>") if isinstance(entry, dict) else "<invalid>"
        try:
            require(isinstance(entry, dict), "Catalog entry must be an object")
            key = (entry.get("style"), entry.get("configuration"), entry.get("variant"))
            require(key in expected and type(key[2]) is int and key not in seen, "Invalid or duplicate style/configuration/variant")
            seen.add(key)
            style, config, variant = key
            stem = f"tile_{style}_{config}_{variant:02}"
            require(entry.get("id") == stem and stem not in identifiers, f"Expected unique ID {stem}")
            identifiers.add(stem)
            require(entry.get("edge_mask") == CONFIGURATIONS[config], "Canonical edge mask disagrees with configuration")
            relative = Path(entry.get("model", ""))
            require(not relative.is_absolute() and relative.suffix.lower() == ".glb", "Model must be a project-relative GLB path")
            path = (ROOT/relative).resolve()
            require(path.is_relative_to(models_path) and path.stem == stem and path not in paths,
                    "Model path is outside kit or mismatches asset ID")
            paths.add(path)
            require(path.is_file(), f"Missing model: {relative}")
            report = audit_model(path, entry)
            reports.append(report)
            if report["triangles"] > 5000:
                warnings.append(f"{stem}: {report['triangles']} triangles exceeds preferred 5000 budget")
        except (ValueError, KeyError, TypeError, IndexError, OSError, struct.error, zlib.error) as error:
            errors.append(f"{label}: {error}")
    require(seen == expected, f"Catalog configurations missing: {sorted(expected-seen)}")
    actual_files = {p.resolve() for p in models_path.glob("*.glb")}
    if actual_files != paths:
        errors.append("Catalog/model file mismatch: " + ", ".join(str(p.relative_to(ROOT)) for p in sorted(actual_files ^ paths)))
    fingerprints = {report["id"]: report["geometry_sha256"] for report in reports}
    for style in STYLES:
        for config in CONFIGURATIONS:
            values = [fingerprints.get(f"tile_{style}_{config}_{variant:02}") for variant in range(1, 5)]
            if None not in values and len(set(values)) != 4:
                errors.append(f"{style}/{config}: variants have identical geometry")
    return {"status": "FAIL" if errors else "PASS", "assets": len(reports),
            "configurations": CONFIGURATIONS, "variants_per_configuration": 4, "styles": list(STYLES),
            "total_triangles": sum(item["triangles"] for item in reports),
            "max_triangles": max((item["triangles"] for item in reports), default=0),
            "warnings": warnings, "errors": errors, "models": reports}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--models", type=Path, default=DEFAULT_MODELS)
    parser.add_argument("--output", type=Path, help="Optional detailed JSON report")
    args = parser.parse_args()
    try:
        report = audit(args.catalog.resolve(), args.models.resolve())
    except (ValueError, KeyError, TypeError, OSError) as error:
        report = {"status": "FAIL", "errors": [str(error)]}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "models"}, indent=2))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
