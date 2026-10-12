#!/usr/bin/env python3
"""Audit generated PBR assets and verify ownership of retained model assets."""
from __future__ import annotations

import argparse
import json
from collections import Counter
from urllib.parse import unquote

import numpy as np
from PIL import Image

from pbr_gltf import ASSETS, ROOT, Document
from pbr_art_ownership import generated_output, validate_ownership


def require(condition, message):
    if not condition:
        raise ValueError(message)


def faces(document, primitive):
    if "indices" in primitive:
        return document.accessor(primitive["indices"]).reshape(-1).astype(int)
    return np.arange(document.data["accessors"][primitive["attributes"]["POSITION"]]["count"])


def check_model(spec, path):
    source, target = Document(ASSETS / spec["source"]), Document(ASSETS / path)
    for key in ("nodes", "scenes", "scene", "skins", "animations"):
        require(source.data.get(key) == target.data.get(key), f"{spec['name']}: changed {key}")
    require(len(source.data["materials"]) == len(target.data["materials"]), f"{spec['name']}: changed material slots")
    require(len(source.data["meshes"]) == len(target.data["meshes"]), f"{spec['name']}: changed mesh count")
    count = 0
    for sm, tm in zip(source.data["meshes"], target.data["meshes"]):
        require(len(sm["primitives"]) == len(tm["primitives"]), "Changed primitive count")
        for sp, tp in zip(sm["primitives"], tm["primitives"]):
            sf, tf = faces(source, sp), faces(target, tp)
            require(len(sf) == len(tf), "Changed topology")
            require(sp.get("material") == tp.get("material"), "Changed primitive material slot")
            count += len(sf) // 3
            for key, accessor in sp["attributes"].items():
                if key in {"TEXCOORD_1", "TANGENT"}:
                    continue
                before = source.accessor(accessor, normalized=False)[sf]
                after = target.accessor(tp["attributes"][key], normalized=False)[tf]
                require(np.array_equal(before, after), f"{spec['name']}: changed triangle attribute {key}")
            for before_morph, after_morph in zip(sp.get("targets", []), tp.get("targets", [])):
                for key in before_morph:
                    require(np.array_equal(source.accessor(before_morph[key])[sf], target.accessor(after_morph[key])[tf]), "Changed morph target")
            uv = target.accessor(tp["attributes"]["TEXCOORD_1"])
            tangent = target.accessor(tp["attributes"]["TANGENT"])
            normal = target.accessor(tp["attributes"]["NORMAL"])
            require(np.isfinite(uv).all() and ((uv > 0) & (uv < 1)).all(), "Invalid detail UV")
            require(np.isfinite(tangent).all(), "Non-finite tangent")
            require(np.allclose(np.linalg.norm(tangent[:, :3], axis=1), 1, atol=1e-5), "Non-unit tangent")
            require(np.max(np.abs((tangent[:, :3] * normal).sum(1))) < 1e-5, "Tangent is outside the surface")
            require(np.isin(tangent[:, 3], (-1, 1)).all(), "Invalid tangent handedness")
            positions = target.accessor(tp["attributes"]["POSITION"])[tf].reshape(-1, 3, 3).astype(float)
            u = uv[tf].reshape(-1, 3, 2)
            e1, e2 = u[:, 1] - u[:, 0], u[:, 2] - u[:, 0]
            area = e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]
            geometric_area = np.linalg.norm(np.cross(positions[:, 1] - positions[:, 0], positions[:, 2] - positions[:, 0]), axis=1)
            # Imported heads contain coincident seam slivers (~1e-9 area),
            # below float32 UV precision. Exclude only those zero-area artifacts.
            require((np.abs(area[geometric_area > 1e-8]) > 1e-12).all(), f"{spec['name']}: collapsed detail UV on a nondegenerate triangle")
            # All vertices of a triangle must sample the same material cell.
            cells = np.floor(u * 4).astype(int)
            require((cells[:, 0] == cells[:, 1]).all() and (cells[:, 0] == cells[:, 2]).all(), "Triangle crosses surface cells")
            material = target.data["materials"][tp.get("material", 0)]
            require("metallicRoughnessTexture" in material["pbrMetallicRoughness"], "Missing packed material map")
            require(material["pbrMetallicRoughness"]["metallicFactor"] == 1, "Metal map multiplied by zero")
            require(material["pbrMetallicRoughness"]["roughnessFactor"] == 1, "Roughness map double-scaled")
            if spec.get("terrain_material"):
                require(material["normalTexture"].get("texCoord", 0) == 0, "Wall normal lost albedo alignment")
                require(material["pbrMetallicRoughness"]["metallicRoughnessTexture"].get("texCoord", 0) == 0, "Wall ORM lost albedo alignment")
                normal_texture = target.data["textures"][material["normalTexture"]["index"]]
                image_uri = target.data["images"][normal_texture["source"]]["uri"]
                require(image_uri == "../" + spec["terrain_material"] + "_normal.png", "Wall uses generic atlas normal")
                # Check the actual UV0 differential basis, including mirrored
                # faces; orthogonality alone cannot catch a wrong UV basis.
                uv0 = target.accessor(tp["attributes"]["TEXCOORD_0"])[tf].reshape(-1, 3, 2)
                du, dv = uv0[:, 1] - uv0[:, 0], uv0[:, 2] - uv0[:, 0]
                det = du[:, 0] * dv[:, 1] - du[:, 1] * dv[:, 0]
                valid = (np.abs(det) > 1e-10) & (geometric_area > 1e-8)
                edge1 = positions[valid, 1] - positions[valid, 0]
                edge2 = positions[valid, 2] - positions[valid, 0]
                tu = (edge1 * dv[valid, 1, None] - edge2 * du[valid, 1, None]) / det[valid, None]
                tv = (edge2 * du[valid, 0, None] - edge1 * dv[valid, 0, None]) / det[valid, None]
                n = normal[tf].reshape(-1, 3, 3)[valid].astype(float)
                n /= np.linalg.norm(n, axis=2, keepdims=True)
                t = tu[:, None] - n * (n * tu[:, None]).sum(2, keepdims=True)
                t /= np.linalg.norm(t, axis=2, keepdims=True)
                authored = tangent[tf].reshape(-1, 3, 4)[valid]
                require(np.allclose(t, authored[:, :, :3], atol=1e-4), "Wall tangent does not follow UV0")
                sign = np.where((np.cross(n, t) * tv[:, None]).sum(2) < 0, -1, 1)
                require(np.array_equal(sign, authored[:, :, 3]), "Wall tangent handedness does not follow UV0")
            if "emissiveTexture" in material:
                emission = np.asarray(target.image(material["emissiveTexture"]["index"]))
                centers = u.mean(1)
                samples = emission[np.minimum((centers[:, 1] * len(emission)).astype(int), len(emission) - 1), np.minimum((centers[:, 0] * len(emission)).astype(int), len(emission) - 1)]
                lit = samples.max(1) > 0
                require(((cells[lit, 0, 0] == 3) & (cells[lit, 0, 1] == 3)).all(), "Emission leaked beyond displays")
            if spec.get("engine_tint"):
                require("baseColorTexture" not in material["pbrMetallicRoughness"], "Gameplay tint lost")
    # Geometry-independent skin matrices and animation curves must retain bytes.
    for key in ("skins", "animations"):
        for item in source.data.get(key, []):
            accessors = [item["inverseBindMatrices"]] if key == "skins" and "inverseBindMatrices" in item else []
            if key == "animations":
                accessors += [s[k] for s in item["samplers"] for k in ("input", "output")]
            for a in accessors:
                require(np.array_equal(source.accessor(a, normalized=False), target.accessor(a, normalized=False)), "Changed skeleton or animation data")
    return count


def check_terrain(manifest):
    textures = {texture["name"]: texture for texture in manifest["textures"]}
    for name in ("GroundGrass", "GroundRock", "GroundRockSide"):
        for suffix in ("Normal", "Orm"):
            texture = textures.get(name + suffix)
            require(texture is not None, f"{name}: missing {suffix}")
            require(texture.get("colorSpace") == "linear", "Terrain data map is sRGB")
            require(texture.get("tiling") and texture.get("filter") == "linear", "Terrain data sampler does not match albedo")
            pixels = np.asarray(Image.open(ASSETS / texture["path"]).convert("RGB"))
            require(pixels.shape == (1024, 1024, 3), "Unexpected terrain map size")
            if suffix == "Normal":
                decoded = pixels.astype(float) / 127.5 - 1
                require(np.allclose(np.linalg.norm(decoded, axis=2), 1, atol=.007), "Terrain normal is not unit length")
                require((decoded[:, :, 2] > 0).all(), "Terrain normal points into surface")
            else:
                require((pixels[:, :, 2] == 0).all(), "Natural terrain became metallic")
                require(pixels[:, :, 1].min() >= 180, "Natural terrain became glossy")
                require(pixels[:, :, 0].min() >= 215, "Terrain cavity AO is too strong")
    # Every authored level decoration resolves to a reviewed registered model.
    model_names = {model["name"] for model in manifest["models"]}
    for screen in (ROOT / "levels").rglob("*.scr"):
        for line in screen.read_text().splitlines():
            if line.startswith("@decoration "):
                decoration = json.loads(line[len("@decoration "):])
                require(decoration.get("model") in model_names, f"Unreviewed level decoration in {screen}")
    print("PASS: all three terrain map pairs, linear repeat samplers, nonmetallic matte response and level decoration coverage.")


def check_retained_dependencies(path):
    """Check availability, without imposing generated PBR geometry on authored art."""
    asset = ASSETS / path
    require(asset.is_file(), f"Retained model is missing: {path}")
    document = Document(asset)
    for resource in (*document.data.get("buffers", []), *document.data.get("images", [])):
        uri = resource.get("uri")
        if uri and not uri.startswith("data:"):
            dependency = asset.parent / unquote(uri)
            require(dependency.is_file(), f"{path}: missing dependency {uri}")


def check_material_profiles(generated_specs):
    if not generated_specs:
        return
    inventory = json.loads((ASSETS / "custom/pbr/inventory.json").read_text())
    generated = {spec["name"]: spec for spec in generated_specs}
    rows = {}
    # The inventory may predate a retained-art migration. Only the current
    # generated names contribute evidence for their intended surface profiles.
    for row in inventory["models"]:
        name = row["name"]
        if name not in generated:
            continue
        require(name not in rows, f"{name}: duplicate generated inventory row")
        spec = generated[name]
        require(row.get("source") == spec["source"], f"{name}: inventory source is stale")
        require(row.get("output") == generated_output(spec), f"{name}: inventory output is stale")
        rows[name] = row
    require(set(rows) == set(generated),
            f"Generated inventory is missing models: {', '.join(sorted(set(generated) - set(rows)))}")
    responses = Counter()
    for row in rows.values():
        responses.update(row.get("surface_triangles", {}))
    require(all(responses[profile] > 0 for profile in ("skin", "cloth", "steel", "brass")),
            "Missing intended material separation in current generated models")
    for name in ("Decoration_monitor", "Decoration_gameconsole_handheld"):
        if name in generated:
            row = rows[name]
            require(0 < row["surface_triangles"].get("display", 0) < row["triangles"],
                    f"{name}: display mask missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-legacy", action="store_true",
                        help="Also audit opt-in legacy PBR variants after generating them.")
    args = parser.parse_args()
    config = json.loads((ROOT / "tools/pbr_art_pass.json").read_text())
    manifest = json.loads((ASSETS / "manifest.json").read_text())
    definitions = validate_ownership(config, manifest)
    check_terrain(manifest)
    generated_specs = []
    retained_paths = set()
    generated_attachment_slots = {(spec["model"], spec["slot"])
                                  for spec in config["attachments"]}
    triangles = retained = 0
    for spec in config["models"]:
        definition = definitions[spec["name"]]
        retained_paths.update(attachment["path"]
                              for slot, attachment in enumerate(definition.get("attachments", []))
                              if (spec["name"], slot) not in generated_attachment_slots)
        if spec["ownership"] == "retained":
            retained += 1
            retained_paths.add(definition["path"])
            continue
        require(definition["path"] == generated_output(spec),
                f"{spec['name']}: manifest does not publish the generated model")
        triangles += check_model(spec, definition["path"])
        generated_specs.append(spec)
    for path in sorted(retained_paths):
        check_retained_dependencies(path)
    for spec in config["attachments"]:
        attachment = definitions[spec["model"]]["attachments"][spec["slot"]]
        require(attachment["path"] == generated_output(spec),
                f"{spec['name']}: manifest does not publish the generated attachment")
        triangles += check_model(spec, generated_output(spec))
    check_material_profiles(generated_specs)
    print(f"PASS: {len(generated_specs)} generated model variants and {len(config['attachments'])} generated attachments; {triangles:,} triangles retain original geometry, UV0, skinning and animation.")
    print("PASS: generated detail UVs, tangent frames, material channels and display masks.")
    print(f"PASS: explicit ownership covers {len(definitions)} registered models; {retained} retained models, undeclared attachments and their dependencies exist (authored geometry not audited here).")
    if args.include_legacy:
        legacy_triangles = 0
        legacy_specs = config.get("legacy_models", [])
        for spec in legacy_specs:
            output = generated_output(spec)
            require((ASSETS / output).is_file(),
                    f"{spec['name']}: opt-in legacy variant is missing; generate with --include-legacy first")
            legacy_triangles += check_model(spec, output)
        print(f"PASS: {len(legacy_specs)} opt-in legacy variants; {legacy_triangles:,} triangles retain source geometry and generated material channels.")


if __name__ == "__main__":
    main()
