#!/usr/bin/env python3
"""Validate PBR assets against source geometry, animation and material intent."""
from __future__ import annotations

import json
from collections import Counter

import numpy as np
from PIL import Image

from pbr_gltf import ASSETS, ROOT, Document


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


def main():
    config = json.loads((ROOT / "tools/pbr_art_pass.json").read_text())
    manifest = json.loads((ASSETS / "manifest.json").read_text())
    definitions = {m["name"]: m for m in manifest["models"]}
    check_terrain(manifest)
    require(set(definitions) == {s["name"] for s in config["models"]}, "Manifest inventory needs an art review update")
    count = triangles = 0
    for spec in config["models"]:
        if spec.get("skip"):
            require(definitions[spec["name"]]["path"] == spec["source"], "Reference rig changed")
            continue
        triangles += check_model(spec, definitions[spec["name"]]["path"])
        count += 1
    for spec in config["attachments"]:
        triangles += check_model(spec, f"custom/pbr/models/{spec['name']}.glb")
    inventory = json.loads((ASSETS / "custom/pbr/inventory.json").read_text())
    responses = Counter()
    for row in inventory["models"]:
        responses.update(row.get("surface_triangles", {}))
    require(responses["skin"] > 0 and responses["cloth"] > 0 and responses["steel"] > 0 and responses["brass"] > 0, "Missing intended material separation")
    for name in ("Decoration_monitor", "Decoration_gameconsole_handheld"):
        row = next(r for r in inventory["models"] if r["name"] == name)
        require(0 < row["surface_triangles"].get("display", 0) < row["triangles"], f"{name}: display mask missing")
    print(f"PASS: {count} model variants, {len(config['attachments'])} attachment; {triangles:,} triangles retain original geometry, UV0, skinning and animation.")
    print("PASS: detail UVs, tangent frames, material channels, display masks and manifest coverage.")


if __name__ == "__main__":
    main()
