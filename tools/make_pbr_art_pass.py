#!/usr/bin/env python3
"""Author game-ready PBR variants from the 64-model manifest inventory.

Requires Pillow and numpy. Run from any directory. Vendor and procedural source
models remain intact. UV0, skinning, morphs, transforms and animation are retained;
UV1 and explicit tangent frames carry a shared surface atlas. Material assignment
is art-directed in pbr_art_pass.json, rather than inferred from color brightness.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

from pbr_gltf import ASSETS, ROOT, Document, palette_sample, write_glb

CONFIG = ROOT / "tools/pbr_art_pass.json"
OUTPUT = ASSETS / "custom/pbr"
CELL = 256
GUTTER = 24
SIZE = CELL * 4


def save_text(path, text):
    raw = text.encode("utf-8")
    if not path.exists() or path.read_bytes() != raw:
        temporary = path.with_suffix(path.suffix + ".art-pass.tmp")
        temporary.write_bytes(raw)
        temporary.replace(path)


def save_image(path, pixels):
    import io
    encoded = io.BytesIO()
    Image.fromarray(pixels).save(encoded, format="PNG", optimize=True)
    raw = encoded.getvalue()
    if not path.exists() or path.read_bytes() != raw:
        path.write_bytes(raw)


def make_maps(config):
    source = Image.open(OUTPUT / "surface_height_source.png").convert("L")
    normal = np.zeros((SIZE, SIZE, 3), dtype=np.uint8)
    orm = np.zeros_like(normal)
    emissive = np.zeros_like(normal)
    for index, profile in enumerate(config["profiles"]):
        x, y = index % 4, index // 4
        # Crop inside each generated panel to exclude its boundary. Add edge
        # dilation AFTER encoding derivatives: padding must never become a ridge.
        box = ((x + .035) * source.width / 4, (y + .035) * source.height / 4,
               (x + .965) * source.width / 4, (y + .965) * source.height / 4)
        tile = source.crop(box).resize((CELL - 2 * GUTTER,) * 2, Image.Resampling.LANCZOS)
        height = np.asarray(tile.filter(ImageFilter.GaussianBlur(.65)), dtype=float) / 255
        gy, gx = np.gradient(height)
        vectors = np.stack((-gx * profile["normal_gain"], -gy * profile["normal_gain"],
                            np.ones_like(height)), axis=-1)
        vectors /= np.linalg.norm(vectors, axis=-1, keepdims=True)
        n = np.rint((vectors * .5 + .5) * 255).astype(np.uint8)
        packed = np.empty_like(n)
        # R = conservative micro-cavity AO, G = perceptual roughness, B = metal.
        # This is not a geometry AO bake. Large-scale contact uses engine SSAO.
        h = np.clip((height - np.median(height)) * 2, -.5, .5)
        packed[..., 0] = np.rint((1 - profile["cavity"] * np.maximum(-h, 0) * 2) * 255)
        packed[..., 1] = np.rint(np.clip(profile["roughness"] + h * .06, .08, 1) * 255)
        packed[..., 2] = round(profile["metallic"] * 255)
        e = np.zeros_like(n)
        if profile["name"] == "display":
            e[:] = (110, 175, 200)
        region = np.s_[y * CELL:(y + 1) * CELL, x * CELL:(x + 1) * CELL]
        pad = ((GUTTER, GUTTER), (GUTTER, GUTTER), (0, 0))
        normal[region] = np.pad(n, pad, mode="edge")
        orm[region] = np.pad(packed, pad, mode="edge")
        emissive[region] = np.pad(e, pad, mode="edge")
        if profile["name"] == "rubber":
            # The conveyor belt's UV0 scrolls. Give it its own repeating maps
            # so scrolling cannot traverse unrelated cells in the shared atlas.
            # Flatten the edges of the height field before deriving this normal.
            window = np.sin(np.linspace(0, np.pi, len(height))) ** 2
            periodic = (height - np.median(height)) * window[:, None] * window[None, :]
            by, bx = np.gradient(periodic)
            b = np.stack((-bx * .8, -by * .8, np.ones_like(bx)), -1)
            b /= np.linalg.norm(b, axis=-1, keepdims=True)
            save_image(OUTPUT / "belt_normal.png", np.rint((b * .5 + .5) * 255).astype(np.uint8))
            save_image(OUTPUT / "belt_orm.png", np.full_like(n, (255, 237, 0)))
    save_image(OUTPUT / "surface_normal.png", normal)
    save_image(OUTPUT / "surface_orm.png", orm)
    save_image(OUTPUT / "surface_emissive.png", emissive)


def in_rect(uv, rect):
    return rect[0] <= uv[0] < rect[2] and rect[1] <= uv[1] < rect[3]


def surface_for(config, spec, material, mesh_name, uv, color, center, face_normal):
    rule = spec["surface_rule"]
    name = material.get("name", "")
    if name in config["material_overrides"]:
        return config["material_overrides"][name]
    if rule == "hero":
        if "Cape" in mesh_name or "Hat" in mesh_name and "Bear" not in mesh_name:
            return "cloth"
        if "BearHat" in mesh_name:
            return "hair"
        for rect, surface in config["hero_palette_regions"][spec["name"]]:
            if in_rect(uv, rect):
                return surface
        return "cloth"
    if rule in {p["name"] for p in config["profiles"]}:
        return rule
    if rule == "named-materials":
        raise ValueError(f"Unreviewed material {name} in {spec['name']}")
    if rule == "metal":
        return "brass" if color[0] > color[2] * 1.4 and color[1] > color[2] * 1.15 else "steel"
    if rule == "flag":
        return "paint" if center[1] < .75 else "cloth"
    if rule in {"furniture", "plant", "office-chair"}:
        if color[0] > color[2] * 1.35 and color[0] > color[1] * 1.12:
            return "ceramic" if rule == "plant" else "paint" if rule == "office-chair" else "wood"
        if rule == "plant" and color[1] > color[0] * 1.15:
            return "foliage"
        if rule == "office-chair":
            return "rubber" if color.mean() < 70 else "cloth"
        return "cloth" if color[2] > color[0] * 1.1 else "paper" if rule == "furniture" else "ceramic"
    if rule == "monitor":
        # Front panel in this model is the central -Z rectangle. Stand, casing
        # and rear faces share its palette, so color alone cannot identify it.
        return "display" if face_normal[2] < -.9 and center[1] > .45 and abs(center[0]) < .65 else "paint"
    if rule == "handheld":
        return "display" if face_normal[1] > .95 and abs(center[0]) < .29 and abs(center[2]) < .18 and center[1] >= .07 and color.mean() < 75 else "rubber" if color.mean() < 75 else "paint"
    if rule == "keyboard":
        return "rubber" if color.mean() < 80 else "paint"
    if rule == "arrow":
        return "wood" if color[0] > color[2] * 1.3 else "foliage" if color[1] > color[0] * 1.3 else "steel"
    if rule == "axe":
        return "wood" if color[0] > color[2] * 1.3 else "paint" if color[1] > color[0] * 1.3 else "steel"
    if rule == "spring":
        return "steel" if color.max() - color.min() < 25 else "paint"
    if rule == "conveyor":
        return "rubber" if name == "threads" else "steel" if color.max() - color.min() < 25 else "paint"
    if rule == "mechanism":
        return "steel" if name == "Plate" else "paint"
    raise ValueError(f"Unreviewed surface rule: {rule}")


def author_model(config, spec):
    source = Document(ASSETS / spec["source"])
    for material in source.data.get("materials", []):
        if any(key in material for key in ("normalTexture", "occlusionTexture", "emissiveTexture")) or "metallicRoughnessTexture" in material.get("pbrMetallicRoughness", {}):
            raise ValueError(f"Source has authored PBR maps; review them before reauthoring UVs: {spec['source']}")
    data = copy.deepcopy(source.data)
    output = OUTPUT / "models" / (spec["name"] + ".glb")
    binary = bytearray()
    offsets = []
    for buffer in source.buffers:
        binary.extend(b"\0" * (-len(binary) % 4))
        offsets.append(len(binary))
        binary.extend(buffer)
    for view in data.get("bufferViews", []):
        view["byteOffset"] = view.get("byteOffset", 0) + offsets[view["buffer"]]
        view["buffer"] = 0
    for image in data.get("images", []):
        if "uri" in image and not image["uri"].startswith("data:"):
            from urllib.parse import unquote
            image["uri"] = Path(os.path.relpath(source.path.parent / unquote(image["uri"]), output.parent)).as_posix()

    def append(values, template=None, kind=None, component=5126, target=34962):
        binary.extend(b"\0" * (-len(binary) % 4))
        start = len(binary)
        binary.extend(values.tobytes())
        data.setdefault("bufferViews", []).append({"buffer": 0, "byteOffset": start,
            "byteLength": values.nbytes, "target": target})
        a = copy.deepcopy(template) if template else {"componentType": component, "type": kind}
        a.pop("byteOffset", None)
        a.pop("sparse", None)
        a["bufferView"] = len(data["bufferViews"]) - 1
        a["count"] = len(values)
        # Original POSITION bounds remain valid: only vertex duplication occurs.
        data.setdefault("accessors", []).append(a)
        return len(data["accessors"]) - 1

    def texture(filename, repeat=False):
        data.setdefault("samplers", []).append({"magFilter": 9729, "minFilter": 9987,
            "wrapS": 10497 if repeat else 33071, "wrapT": 10497 if repeat else 33071})
        data.setdefault("images", []).append({"uri": "../" + filename, "mimeType": "image/png"})
        data.setdefault("textures", []).append({"source": len(data["images"]) - 1,
            "sampler": len(data["samplers"]) - 1})
        return len(data["textures"]) - 1

    normal_texture, orm_texture = texture("surface_normal.png"), texture("surface_orm.png")
    emission_texture = texture("surface_emissive.png")
    counts = Counter()
    original_vertices = output_vertices = triangles = 0
    material_profiles = {}
    profiles = {p["name"]: i for i, p in enumerate(config["profiles"])}
    for mesh in data.get("meshes", []):
        for primitive in mesh["primitives"]:
            if primitive.get("mode", 4) != 4:
                raise ValueError(f"Non-triangle primitive: {spec['name']}")
            attrs = primitive["attributes"]
            arrays = {k: source.accessor(a, normalized=False) for k, a in attrs.items()}
            pos = arrays["POSITION"].astype(float)
            norms = arrays["NORMAL"].astype(float)
            uvs = source.accessor(attrs["TEXCOORD_0"])
            indices = source.accessor(primitive["indices"]).reshape(-1).astype(int) if "indices" in primitive else np.arange(len(pos))
            faces = indices.reshape(-1, 3)
            original_vertices += len(pos)
            triangles += len(faces)
            material_index = primitive.get("material", 0)
            material = data["materials"][material_index]
            pbr = material.get("pbrMetallicRoughness", {})
            base = pbr.get("baseColorTexture")
            image = source.image(base["index"]) if base else None
            face_uvs = uvs[faces].mean(axis=1)
            colors = palette_sample(image, face_uvs) if image else np.tile(np.array(pbr.get("baseColorFactor", [1, 1, 1, 1])[:3]) * 255, (len(faces), 1))
            low, span = pos.min(axis=0), np.maximum(np.ptp(pos, axis=0), 1e-6)
            lookup, sources, detail_uvs, tangents, new_indices = {}, [], [], [], []
            belt = spec["surface_rule"] == "conveyor" and material.get("name") == "threads"
            uv0_detail = belt or bool(spec.get("terrain_material"))
            for face_index, face in enumerate(faces):
                edge1, edge2 = pos[face[1]] - pos[face[0]], pos[face[2]] - pos[face[0]]
                fn = np.cross(edge1, edge2)
                length = np.linalg.norm(fn)
                fn = fn / length if length > 1e-12 else norms[face].mean(axis=0)
                surface = surface_for(config, spec, material, mesh.get("name", ""), face_uvs[face_index], colors[face_index].astype(float), pos[face].mean(axis=0), fn)
                counts[surface] += 1
                material_profiles.setdefault(material_index, set()).add(surface)
                axis = int(np.argmax(np.abs(fn)))
                uaxis, vaxis = [(2, 1), (0, 2), (0, 1)][axis]
                tu, tv = np.eye(3)[uaxis], -np.eye(3)[vaxis]
                if uv0_detail:
                    delta1, delta2 = uvs[face[1]] - uvs[face[0]], uvs[face[2]] - uvs[face[0]]
                    determinant = delta1[0] * delta2[1] - delta1[1] * delta2[0]
                    if abs(determinant) > 1e-10:
                        tu = (edge1 * delta2[1] - edge2 * delta1[1]) / determinant
                        tv = (edge2 * delta1[0] - edge1 * delta2[0]) / determinant
                cell = profiles[surface]
                for vertex in face:
                    # Projection seams and material boundaries need split vertices;
                    # preserve every original attribute, including bone weights.
                    key = (int(vertex), surface, axis, bool(fn[axis] < 0), face_index if uv0_detail else 0)
                    if key not in lookup:
                        lookup[key] = len(sources)
                        sources.append(vertex)
                        local_uv = np.array([(pos[vertex, uaxis] - low[uaxis]) / span[uaxis],
                                             1 - (pos[vertex, vaxis] - low[vaxis]) / span[vaxis]])
                        detail_uvs.append((np.array([cell % 4, cell // 4]) * CELL + GUTTER + .5 + local_uv * (CELL - 2 * GUTTER - 1)) / SIZE)
                        n = norms[vertex] / max(np.linalg.norm(norms[vertex]), 1e-12)
                        t = tu - n * np.dot(tu, n)
                        if np.linalg.norm(t) < 1e-9:
                            t = np.cross(n, tv)
                        t /= max(np.linalg.norm(t), 1e-12)
                        tangents.append([*t, -1 if np.dot(np.cross(n, t), tv) < 0 else 1])
                    new_indices.append(lookup[key])
            ids = np.asarray(sources)
            output_vertices += len(ids)
            for key, values in arrays.items():
                if key not in {"TANGENT", "TEXCOORD_1"}:
                    attrs[key] = append(values[ids], source.data["accessors"][attrs[key]])
            attrs["TEXCOORD_1"] = append(np.array(detail_uvs, dtype="<f4"), kind="VEC2")
            attrs["TANGENT"] = append(np.array(tangents, dtype="<f4"), kind="VEC4")
            for morph in primitive.get("targets", []):
                for key, accessor in morph.items():
                    morph[key] = append(source.accessor(accessor, normalized=False)[ids], source.data["accessors"][accessor])
            primitive["indices"] = append(np.array(new_indices, dtype="<u4").reshape(-1, 1), kind="SCALAR", component=5125, target=34963)

    for index, material in enumerate(data.get("materials", [])):
        if index not in material_profiles:
            continue
        pbr = material.setdefault("pbrMetallicRoughness", {})
        # Wall and rock were explicitly untextured to preserve gameplay tint.
        # Remove only their color image; auto materials now permit PBR maps.
        if spec.get("engine_tint"):
            pbr.pop("baseColorTexture", None)
            pbr["baseColorFactor"] = [1, 1, 1, 1]
        belt = spec["surface_rule"] == "conveyor" and material.get("name") == "threads"
        nt, ot, uvset = normal_texture, orm_texture, 1
        if belt:
            nt, ot, uvset = texture("belt_normal.png", True), texture("belt_orm.png", True), 0
        elif spec.get("terrain_material"):
            stem = spec["terrain_material"]
            nt, ot, uvset = texture(stem + "_normal.png", True), texture(stem + "_orm.png", True), 0
        # Existing artist maps are authoritative. The current inventory has none.
        if "metallicRoughnessTexture" not in pbr:
            pbr.update(metallicFactor=1, roughnessFactor=1,
                       metallicRoughnessTexture={"index": ot, "texCoord": uvset})
        if material_profiles[index] != {"glass"}:
            material.setdefault("normalTexture", {"index": nt, "texCoord": uvset, "scale": 1})
        if material_profiles[index] & {"wood", "endgrain", "stone", "leather"}:
            material.setdefault("occlusionTexture", {"index": ot, "texCoord": uvset, "strength": .6})
        if "display" in material_profiles[index] and "emissiveTexture" not in material:
            material["emissiveTexture"] = {"index": emission_texture, "texCoord": 1}
            material["emissiveFactor"] = [.7, .7, .7]
    data.setdefault("asset", {})["generator"] = "Sokoban PBR art pass: tools/make_pbr_art_pass.py"
    write_glb(output, data, binary)
    return {"name": spec["name"], "source": spec["source"], "output": output.relative_to(ASSETS).as_posix(),
            "triangles": triangles, "source_vertices": original_vertices, "output_vertices": output_vertices,
            "surface_triangles": dict(sorted(counts.items()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-manifest", action="store_true", help="Point registered models to the authored variants.")
    args = parser.parse_args()
    config = json.loads(CONFIG.read_text())
    manifest_path = ASSETS / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    definitions = {m["name"]: m for m in manifest["models"]}
    make_maps(config)
    report = {"format": 1, "models": [], "attachments": []}
    for spec in config["models"]:
        if spec.get("skip"):
            report["models"].append({"name": spec["name"], "source": spec["source"], "retained": spec["skip"]})
            continue
        definition = definitions[spec["name"]]
        row = author_model(config, spec)
        report["models"].append(row)
        if args.update_manifest:
            definition["path"] = row["output"]
            if definition.get("material", {}).get("mode") == "none":
                definition.pop("material")
        print(f"{spec['name']}: {row['triangles']} triangles, {row['surface_triangles']}")
    for spec in config["attachments"]:
        row = author_model(config, spec)
        report["attachments"].append(row)
        if args.update_manifest:
            for definition in manifest["models"]:
                for attachment in definition.get("attachments", []):
                    if attachment["path"] in {spec["source"], row["output"]}:
                        attachment["path"] = row["output"]
    save_text(OUTPUT / "inventory.json", json.dumps(report, indent=2) + "\n")
    if args.update_manifest:
        save_text(manifest_path, json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
