#!/usr/bin/env python3
"""Validate the current native ground kit using only tracked source evidence.

The gzip bundle is captured read-only from ground_modules.blend. Its source,
export and cap-table hashes bind that evidence to this checkout; geometry and
corner streams are independently checked rather than trusting those hashes.
Historical migration reports and ignored accepted-model libraries are unused.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import gzip
from itertools import product
import json
from pathlib import Path
import re
import struct
import sys

import numpy as np
from PIL import Image

import validate_ground_modules as native
import validate_ground_bottoms as rock
import validate_ground_shoulders as shoulder
import validate_regular_relief as relief
from ground_shoulder_coverage import audit_shoulder_coverage
from pbr_gltf import Document

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = 2e-5
# Saving/loading Blender's packed custom normals produces up to 6.54e-5
# component error in the current 225,273 exported triangles. Geometry and UV
# comparisons remain tighter; this is below the historical 2e-4 normal bound.
NORMAL_TOLERANCE = 1e-4
SHA256 = re.compile(r"[0-9a-f]{64}")
DEBRIS_ATTRIBUTE = "_GROUND_DEBRIS"
DEBRIS_MAXIMUM_OFFSET = np.asarray([.008, .008, .020])
DEBRIS_MOTION_RADIUS = float(np.linalg.norm(DEBRIS_MAXIMUM_OFFSET))


def require(condition, message):
    native.require(condition, message)


def asset_path(parent, relative, assets):
    # Match ContentPipeline's external glTF URI subset, including on Windows.
    require(isinstance(relative, str) and relative and not Path(relative).is_absolute()
            and not relative.startswith("/") and not any(char in relative for char in ("%", "\\", ":", "?", "#", "\0")),
            "Expected a supported local asset-relative resource")
    path = (parent / relative).resolve()
    require(path.is_relative_to(assets.resolve()) and path.is_file(),
            f"Missing or external current-ground resource: {path}")
    return path


def read_native_document(path):
    # Inspect buffer ownership before Document can follow external buffer URIs.
    raw = path.read_bytes()
    require(len(raw) >= 20 and raw[:4] == b"glTF", f"{path.name}: expected a binary native GLB")
    _, version, length = struct.unpack_from("<III", raw)
    require(version == 2 and length == len(raw), f"{path.name}: invalid native GLB header")
    metadata, binary, cursor = [], [], 12
    while cursor < length:
        require(cursor+8 <= length, f"{path.name}: truncated GLB chunk")
        size, kind = struct.unpack_from("<II", raw, cursor)
        end = cursor+8+size
        require(size % 4 == 0 and end <= length, f"{path.name}: invalid GLB chunk length")
        if kind == 0x4E4F534A:
            metadata.append(json.loads(raw[cursor+8:end]))
        elif kind == 0x004E4942:
            binary.append(size)
        cursor = end
    require(len(metadata) == len(binary) == 1, f"{path.name}: expected one JSON and embedded binary chunk")
    buffers = metadata[0].get("buffers", [])
    require(len(buffers) == 1 and isinstance(buffers[0], dict) and "uri" not in buffers[0]
            and type(buffers[0].get("byteLength")) is int and 0 < buffers[0]["byteLength"] <= binary[0]
            and binary[0]-buffers[0]["byteLength"] <= 3,
            f"{path.name}: native GLB must use one embedded geometry buffer")
    return Document(path)


def audit_contract(contract):
    require(contract.get("format") == 2 and contract.get("variantCount") == 10
            and contract.get("families") == [{"name": name, "mask": mask} for name, mask in rock.FAMILIES]
            and contract.get("nativeCapCount") == 250 and contract.get("squareNativeCapCount") == 60
            and contract.get("regularNativeBodyCount") == 120 and contract.get("bottomNativeBodyCount") == 120
            and contract.get("regularDesignRevision") == 3 and contract.get("regularReliefRevision") == 1
            and contract.get("bottomDesignRevision") == 5 and contract.get("shoulderTransitionRevision") == 1
            and contract.get("recessMaterialRevision") == 1
            and contract.get("floatingDebrisRevision") == 1
            and contract.get("floatingDebrisAttribute") == DEBRIS_ATTRIBUTE
            and contract.get("floatingDebrisMaximumOffset") == DEBRIS_MAXIMUM_OFFSET.tolist()
            and contract.get("recessMaterialBaseColorFactor") == relief.RECESS_BASE_COLOR,
            "Unexpected current native-ground identity/revision/material contract")
    for key, expected in (("regularReliefMaximumDepth", .0072), ("outerLipHeight", .91),
                          ("squareOuterLipHeight", 1), ("topHeight", 1), ("lipDepth", .09),
                          ("bodyRecessInset", .055), ("regularRemodelHeight", .78),
                          ("shoulderTransitionMinimumZ", .4), ("shoulderCollarRemovalMinimumZ", .78),
                          ("shoulderNearLipCoverageMinimum", .8), ("bottomMaximumDepth", 1),
                          ("bottomMaximumExposedSideOverhang", .04), ("bottomHiddenConnectionHeight", -.34)):
        value = contract.get(key)
        require(type(value) in (int, float) and np.isfinite(value) and abs(value-expected) < 1e-9,
                f"Unexpected current ground contract value: {key}")
    require(contract.get("shoulderRoofClearance") == "0.001 + 0.0006 * zero-based shoulder index",
            "Unexpected shoulder roof-clearance contract")


def source_debris_values(body, label):
    count = len(body["points"])
    attribute = body.get("pointAttributes", {}).get(DEBRIS_ATTRIBUTE)
    if attribute is None:
        return np.zeros(count)
    require(isinstance(attribute, dict) and attribute.get("domain") == "POINT"
            and attribute.get("dataType") == "FLOAT",
            f"{label}: native debris marker must be POINT FLOAT")
    values = np.asarray(attribute.get("values"), dtype=float)
    require(values.shape == (count,) and np.isfinite(values).all()
            and np.all((values >= 0) & (values <= 1)),
            f"{label}: invalid native debris marker values")
    return values


def audit_debris_groups(body, bottom, exposed, label):
    values = source_debris_values(body, label)
    groups = {name: np.asarray(indices, dtype=int) for name, indices in body.get("vertexGroups", {}).items()
              if re.fullmatch(r"FloatingRock\d+", name)}
    expected = np.zeros(len(values), dtype=bool)
    seeds = []
    for name, indices in groups.items():
        require(len(indices) > 0 and np.all((indices >= 0) & (indices < len(values))),
                f"{label}: invalid debris group vertices {name}")
        require(not expected[indices].any(), f"{label}: floating rocks share vertices")
        expected[indices] = True
        seed = values[indices[0]]
        require(seed > 0 and np.all(values[indices] == seed),
                f"{label}: debris marker must be constant and positive within {name}")
        seeds.append(seed)
    require(np.array_equal(values > 0, expected),
            f"{label}: debris marker affects fixed geometry or misses floating vertices")
    require(len(set(seeds)) == len(seeds), f"{label}: floating rocks share a debris seed")
    active = bottom and exposed != 0
    require(bool(groups) == active and body.get("properties", {}).get("FloatingDebrisRevision") == (1 if active else None),
            f"{label}: invalid floating debris revision/ownership")
    return {"animated_floating_rocks": len(groups), "debris_marker_matches_native_groups": True}


def export_debris_values(document, primitives, label):
    node = document.data["nodes"][0]
    rows = document.data["meshes"][node["mesh"]]["primitives"]
    require(len(rows) == len(primitives), f"{label}: ambiguous debris primitive order")
    output = []
    for row, primitive in zip(rows, primitives):
        index = row["attributes"].get(DEBRIS_ATTRIBUTE)
        if index is None:
            output.append(np.zeros(len(primitive.points)))
            continue
        accessor = document.data["accessors"][index]
        require(accessor.get("componentType") == 5126 and accessor.get("type") == "SCALAR"
                and accessor.get("normalized", False) is False,
                f"{label}: exported debris marker must be unnormalized FLOAT SCALAR")
        values = document.accessor(index)
        require(values.shape == (len(primitive.points), 1) and np.isfinite(values).all()
                and np.all((values >= 0) & (values <= 1)), f"{label}: invalid exported debris marker values")
        output.append(values[:, 0])
    return output


def model_name(identity):
    bottom, square, shape, variant = identity
    return f"ground_{'bottom_' if bottom else ''}{'square_' if square else ''}{shape}_{variant+1:02d}.glb"


def audit_manifest(manifest, identities):
    models = manifest.get("models")
    require(isinstance(models, list), "Manifest models must be a list")
    by_name = defaultdict(list)
    for model in models:
        require(isinstance(model, dict) and isinstance(model.get("name"), str), "Invalid manifest model record")
        by_name[model["name"]].append(model)
    for identity in identities:
        bottom, square, shape, variant = identity
        name = ("GroundRock" + ("Bottom" if bottom else "") + ("Square" if square else "")
                + ("" if shape == "island" else shape.title()) + f"{variant+1:02d}")
        require(len(by_name[name]) == 1, f"{name}: missing or duplicate runtime native-ground model")
        model = by_name[name][0]
        require(model.get("path") == "custom/models/ground_modules/"+model_name(identity)
                and model.get("geometry", "static") == "static"
                and model.get("preserveSourceScale") is True
                and model.get("rotateHalfTurn", False) is False
                and model.get("attachments", []) == []
                and "material" not in model,
                f"{name}: manifest path/scale/static orientation or authored PBR contract differs")
    return {"runtime_native_models": len(identities), "authored_ground_runtime_contract": True}


def load_evidence(path, contract_path, contract, root):
    with gzip.open(path, "rt", encoding="utf-8") as stream:
        bundle = json.load(stream)
    require(isinstance(bundle, dict) and bundle.get("format") == 1, "Expected current source evidence format 1")
    source = bundle.get("source", {})
    assets = root / "assets"
    source_path = asset_path(contract_path.parent, source.get("path"), assets)
    contracted_source = asset_path(contract_path.parent, contract.get("source"), assets)
    require(source_path == contracted_source and source_path.suffix.lower() == ".blend",
            "Current source evidence names a different native Blender source")
    require(isinstance(source.get("sha256"), str) and SHA256.fullmatch(source["sha256"])
            and native.digest(source_path) == source["sha256"],
            "Native Blender source changed; recapture current source evidence")
    hashes = bundle.get("capTableSha256", {})
    expected_tables = {contract["capSurfaces"], contract["squareCapSurfaces"]}
    require(set(hashes) == expected_tables, "Current source evidence has incomplete cap-table bindings")
    for name in expected_tables:
        table = asset_path(contract_path.parent, name, assets)
        require(isinstance(hashes[name], str) and SHA256.fullmatch(hashes[name])
                and native.digest(table) == hashes[name], f"Native cap table changed since source capture: {name}")
    rows = bundle.get("models")
    require(isinstance(rows, list), "Current source model evidence must be a list")
    indexed = {}
    masks = dict(rock.FAMILIES)
    for row in rows:
        require(isinstance(row, dict) and type(row.get("bottom")) is bool and type(row.get("squareTop")) is bool
                and row.get("shape") in masks and type(row.get("variant")) is int
                and 0 <= row["variant"] < 10 and type(row.get("exposedSideMask")) is int
                and row["exposedSideMask"] == masks[row["shape"]], "Invalid current source model identity")
        identity = (row["bottom"], row["squareTop"], row["shape"], row["variant"])
        require(identity not in indexed, f"Duplicate current source model identity: {identity}")
        require(isinstance(row.get("exportSha256"), str) and SHA256.fullmatch(row["exportSha256"]),
                f"Missing current export binding: {identity}")
        require(isinstance(row.get("body"), dict) and (row["bottom"] or isinstance(row.get("reference"), dict)),
                f"Missing editable current/frozen source mesh: {identity}")
        indexed[identity] = row
    expected = {(bottom, square, shape, variant) for bottom in (False, True) for square in (False, True)
                for shape, _ in rock.FAMILIES for variant in range(10)}
    require(set(indexed) == expected, "Current source evidence must cover all 240 distinct native bodies")
    require(isinstance(bundle.get("caps"), list) and len(bundle["caps"]) == 310,
            "Current source evidence must contain all 310 editable caps")
    return bundle, indexed


def source_corner_records(body, label):
    points = np.asarray(body["points"], dtype=float)
    polygons = np.asarray(body["polygons"])
    require(points.ndim == 2 and points.shape[1] == 3 and np.isfinite(points).all()
            and polygons.ndim == 2 and polygons.shape[1] == 3
            and np.issubdtype(polygons.dtype, np.integer) and polygons.min() >= 0 and polygons.max() < len(points),
            f"{label}: invalid source triangles")
    count = len(polygons)*3
    normals = np.asarray(body.get("cornerNormals"), dtype=float)
    names = body.get("uvLayerNames")
    layers = body.get("uvLayers", {})
    materials = body.get("faceMaterials")
    require(normals.shape == (count, 3) and np.isfinite(normals).all()
            and np.allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-4, rtol=0),
            f"{label}: invalid native corner normals")
    require(isinstance(names, list) and len(names) >= 1 and len(names) == len(set(names))
            and set(names) == set(layers), f"{label}: missing/ambiguous source UV layer order")
    streams = {}
    for index, name in enumerate(names):
        values = np.asarray(layers[name], dtype=float)
        require(values.shape == (count, 2) and np.isfinite(values).all(), f"{label}: invalid native UV layer {name}")
        values = values.copy()
        values[:, 1] = 1-values[:, 1]  # Blender UV origin -> glTF UV origin.
        streams[f"TEXCOORD_{index}"] = values.reshape(-1, 3, 2)
    require(isinstance(materials, list) and len(materials) == len(polygons)
            and all(isinstance(name, str) and name for name in materials), f"{label}: invalid source face materials")
    debris = source_debris_values(body, label)
    return [{"points": points[face], "normals": normals.reshape(-1, 3, 3)[index],
             "uv": {name: values[index] for name, values in streams.items()}, "material": materials[index],
             "debris": debris[face]}
            for index, face in enumerate(polygons)]


def export_corner_records(primitives, streams, debris=None):
    if debris is None:
        debris = [np.zeros(len(primitive.points)) for primitive in primitives]
    return [{"points": primitive.points[indices], "normals": primitive.normals[indices],
             "uv": {name: values[indices] for name, values in coordinates.items()},
             "material": primitive.material.get("name", ""), "debris": seeds[indices]}
            for primitive, coordinates, seeds in zip(primitives, streams, debris) for indices in primitive.triangles]


def match_corner_records(actual, expected, label):
    """Match oriented triangles without relying on export vertex/face order."""
    require(len(actual) == len(expected), f"{label}: source/export triangle count differs")
    buckets = defaultdict(set)
    for index, record in enumerate(expected):
        for point in record["points"]:
            buckets[tuple(np.floor(point/TOLERANCE).astype(int))].add(index)
    remaining = set(range(len(expected)))
    for record in actual:
        key = np.floor(record["points"][0]/TOLERANCE).astype(int)
        candidates = set()
        for offset in product((-1, 0, 1), repeat=3):
            candidates.update(buckets.get(tuple(key+offset), ()))
        found = None
        for index in sorted(candidates & remaining):
            other = expected[index]
            if record["material"] != other["material"] or set(record["uv"]) != set(other["uv"]):
                continue
            for rotation in range(3):
                if (np.max(np.abs(record["points"]-np.roll(other["points"], rotation, axis=0))) <= TOLERANCE
                        and np.max(np.abs(record["normals"]-np.roll(other["normals"], rotation, axis=0))) <= NORMAL_TOLERANCE
                        and np.max(np.abs(record["debris"]-np.roll(other["debris"], rotation, axis=0))) <= 1e-7
                        and all(np.max(np.abs(values-np.roll(other["uv"][name], rotation, axis=0))) <= TOLERANCE
                                for name, values in record["uv"].items())):
                    found = index
                    break
            if found is not None:
                break
        require(found is not None, f"{label}: source/export triangle position, winding, normal, UV, material or debris marker differs")
        remaining.remove(found)
    require(not remaining, f"{label}: source triangles were not all exported")


def audit_relief_corner_streams(body, reference, label):
    current = source_corner_records(body, label)
    old = source_corner_records(reference, "Frozen "+label)
    indexed = defaultdict(list)
    for record in current:
        indexed[relief.oriented_key(record["points"])].append(record)
    parents, children = set(), set()
    old_points, points = np.asarray(reference["points"]), np.asarray(body["points"])
    for change in body.get("reliefFaces", []):
        vertices, center = change["vertices"], change["center"]
        parent = old[change["parent"]]
        parents.add(relief.oriented_key(parent["points"]))
        order = [next(index for index, point in enumerate(parent["points"])
                      if np.array_equal(point, old_points[vertex])) for vertex in vertices]
        center_uv = {name: np.asarray(change["barycentric"])@values[order] for name, values in parent["uv"].items()}
        for corner in range(3):
            triangle = points[[vertices[corner], vertices[(corner+1) % 3], center]]
            key = relief.oriented_key(triangle)
            children.add(key)
            require(len(indexed[key]) == 1, f"{label}: missing or repeated native relief child")
            child = indexed[key][0]
            require(child["material"] == parent["material"] and set(child["uv"]) == set(parent["uv"]),
                    f"{label}: native relief child material or UV chart differs")
            cross = np.cross(child["points"][1]-child["points"][0], child["points"][2]-child["points"][0])
            normal = cross/np.linalg.norm(cross)
            require(np.max(np.abs(child["normals"]-normal)) <= NORMAL_TOLERANCE,
                    f"{label}: native relief child normals are not flat facet normals")
            for index, point in enumerate(child["points"]):
                is_center = np.array_equal(point, points[center])
                parent_corner = None if is_center else next(
                    (j for j, old_point in enumerate(parent["points"]) if np.array_equal(point, old_point)), None)
                require(is_center or parent_corner is not None, f"{label}: relief child has an unexpected boundary corner")
                for name, values in child["uv"].items():
                    expected = center_uv[name] if is_center else parent["uv"][name][parent_corner]
                    require(np.max(np.abs(values[index]-expected)) <= TOLERANCE,
                            f"{label}: relief boundary/center UV differs from its parent interpolation")
    match_corner_records([record for record in current if relief.oriented_key(record["points"]) not in children],
                         [record for record in old if relief.oriented_key(record["points"]) not in parents],
                         label+" unchanged relief corners")
    return {"unchanged_native_corner_streams_match": True, "relief_parent_uv_interpolation_matches": True}


def material_signature(document, material, assets):
    value = deepcopy(material)
    value.setdefault("alphaMode", "OPAQUE")
    value.setdefault("alphaCutoff", .5)
    value.setdefault("doubleSided", False)
    value.setdefault("emissiveFactor", [0, 0, 0])
    pbr = value.setdefault("pbrMetallicRoughness", {})
    pbr.setdefault("baseColorFactor", [1, 1, 1, 1])
    pbr.setdefault("metallicFactor", 1)
    pbr.setdefault("roughnessFactor", 1)
    for owner, name in ((value.get("pbrMetallicRoughness", {}), "baseColorTexture"),
                        (value.get("pbrMetallicRoughness", {}), "metallicRoughnessTexture"),
                        (value, "normalTexture"), (value, "occlusionTexture"), (value, "emissiveTexture")):
        if name not in owner:
            continue
        info = owner[name]
        info.setdefault("texCoord", 0)
        if name == "normalTexture":
            info.setdefault("scale", 1)
        if name == "occlusionTexture":
            info.setdefault("strength", 1)
        texture = deepcopy(document.data["textures"][info["index"]])
        image = document.data["images"][texture["source"]]
        require("uri" in image and "bufferView" not in image, "Current ground PBR map is not an external finalized image")
        resource = asset_path(document.path.parent, image["uri"], assets)
        sampler = deepcopy(document.data.get("samplers", [])[texture["sampler"]]) if "sampler" in texture else {}
        sampler.setdefault("wrapS", 10497)
        sampler.setdefault("wrapT", 10497)
        texture["sampler"], texture["source"] = sampler, resource.relative_to(assets.resolve()).as_posix()
        info["index"] = texture
    return value


def semantic_equal(actual, expected):
    # Blender exports float32 shader values and omits glTF defaults; source
    # reference GLBs retain explicit defaults and double-precision JSON values.
    if type(actual) in (float, int) and type(expected) in (float, int):
        return bool(np.isfinite(actual) and np.isfinite(expected) and abs(actual-expected) <= TOLERANCE)
    if isinstance(actual, dict) and isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(semantic_equal(actual[key], expected[key]) for key in actual)
    if isinstance(actual, list) and isinstance(expected, list):
        return len(actual) == len(expected) and all(semantic_equal(a, b) for a, b in zip(actual, expected))
    return type(actual) is type(expected) and actual == expected


def audit_materials(document, original, contract, assets, verified_images=None):
    verified_images = set() if verified_images is None else verified_images
    originals = {material["name"]: material_signature(original, material, assets)
                 for material in original.data["materials"]}
    seen = set()
    for material in document.data["materials"]:
        name = material.get("name")
        require(name not in seen, f"{document.path.name}: duplicate material name")
        seen.add(name)
        if name == "GroundModuleRecess":
            pbr = material.get("pbrMetallicRoughness", {})
            require(pbr.get("baseColorFactor") == contract["recessMaterialBaseColorFactor"]
                    and not any(key in pbr for key in ("baseColorTexture", "metallicRoughnessTexture"))
                    and not any(key in material for key in ("normalTexture", "occlusionTexture", "emissiveTexture"))
                    and pbr.get("metallicFactor", 1) == 0
                    and abs(pbr.get("roughnessFactor", -1)-.95) <= TOLERANCE
                    and material.get("doubleSided") is True and material.get("alphaMode", "OPAQUE") == "OPAQUE"
                    and material.get("emissiveFactor", [0, 0, 0]) == [0, 0, 0]
                    and not material.get("extensions"),
                    f"{document.path.name}: recess material differs from its current sandstone contract")
        else:
            # Blender suffixes duplicate material names when importing the ten
            # style references; their semantic parameters still belong to the
            # corresponding original style's named sandstone plate.
            original_name = re.sub(r"\.\d{3}$", "", name) if isinstance(name, str) else name
            signature = material_signature(document, material, assets)
            signature["name"] = original_name
            require(original_name in originals and semantic_equal(signature, originals[original_name]),
                    f"{document.path.name}: original PBR material, map or sampler differs")
            pbr = material.get("pbrMetallicRoughness", {})
            require(all(spec is not None for spec in (pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture"),
                                                      material.get("normalTexture"), material.get("occlusionTexture"))),
                    f"{document.path.name}: rock lacks complete albedo/normal/ORM/AO bindings")
    for image in document.data.get("images", []):
        require("uri" in image and "bufferView" not in image, f"{document.path.name}: unfinalized PBR image")
        resource = asset_path(document.path.parent, image["uri"], assets)
        require(resource.suffix.lower() == ".png", f"{document.path.name}: PBR image is not PNG")
        if resource not in verified_images:
            with Image.open(resource) as decoded:
                decoded.verify()
            verified_images.add(resource)
    return {"current_pbr_materials": len(seen), "decoded_shared_pngs": len(verified_images)}


def rooted_masses(masses, core, label):
    roots = {name for name, mass in masses.items() if rock.core_intersection(mass, core)}
    graph = {name: set() for name in masses}
    names = list(masses)
    for index, name in enumerate(names):
        for other in names[index+1:]:
            if rock.convex_gap(masses[name], masses[other]) < -TOLERANCE:
                graph[name].add(other)
                graph[other].add(name)
    reached, pending = set(), list(roots)
    while pending:
        name = pending.pop()
        if name not in reached:
            reached.add(name)
            pending.extend(graph[name]-reached)
    require(reached == set(masses), f"{label}: boulder mass is not rooted in its native core")


def audit_debris_motion_envelope(floating, masses, roles, exposed, label):
    for name, value in floating.items():
        require(not rock.core_intersection(value, roles["IslandCore"])
                and not rock.point_inside_core(value["points"].mean(0), roles["IslandCore"]),
                f"{label}: floating rock intersects its core")
        fixed_distance = rock.mesh_surface_distance(value["faces"], roles["IslandCore"])
        for other_name, other in {**masses, **floating}.items():
            if other_name != name:
                require(rock.convex_gap(value, other) > 0, f"{label}: floating rocks intersect another mass")
                distance = rock.mesh_surface_distance(value["faces"], other["faces"])
                if other_name in floating:
                    require(distance >= .04 + 2*DEBRIS_MOTION_RADIUS-TOLERANCE,
                            f"{label}: animated floating rocks lose their visible surface gap")
                else:
                    fixed_distance = min(fixed_distance, distance)
        # Shoulders are fixed too. A translation by a vector inside the stated
        # envelope can reduce surface distance by at most its Euclidean norm.
        for other_name, faces in roles.items():
            if re.fullmatch(r"ShoulderRock\d+", other_name):
                fixed_distance = min(fixed_distance, rock.mesh_surface_distance(value["faces"], faces))
        require(fixed_distance >= .04 + DEBRIS_MOTION_RADIUS-TOLERANCE,
                f"{label}: animated floating rock loses its visible surface gap from fixed geometry")
        low, high = np.asarray(value["minimum"]), np.asarray(value["maximum"])
        minimum = np.asarray([-.04 if exposed & 8 else 0, -.04 if exposed & 1 else 0, -1])
        maximum = np.asarray([1.04 if exposed & 2 else 1, 1.04 if exposed & 4 else 1, .777])
        require(np.all(low-DEBRIS_MAXIMUM_OFFSET >= minimum-TOLERANCE)
                and np.all(high+DEBRIS_MAXIMUM_OFFSET <= maximum+TOLERANCE),
                f"{label}: animated floating rock escapes its body envelope")


def audit_bottom_geometry(body, roles, exposed, lip, label):
    points = np.asarray(body["points"])
    require(points[:, 2].min() < -.4, f"{label}: floating bottom has no broad rock depth")
    require(all(name in roles for name in ("IslandCore", "IslandUnderside", "IslandRockWalls")),
            f"{label}: missing editable bottom core/floor/wall roles")
    core_faces = set(body["faceGroups"]["IslandCore"])
    require(set(body["faceGroups"]["IslandUnderside"]) <= core_faces
            and set(body["faceGroups"]["IslandRockWalls"]) <= core_faces,
            f"{label}: bottom floor or wall ownership escapes its core")
    _, boundary, area = rock.floor_disk(roles["IslandUnderside"], label)
    for side in range(4):
        if not exposed & (1 << side):
            rock.hidden_floor_trace(boundary, side, label)
    rock.audit_core_floor_attachment(body, boundary, exposed, label)
    masses = {name: rock.convex_rock(faces, f"{label} {name}") for name, faces in roles.items()
              if re.fullmatch(r"BoulderMass\d+", name)}
    floating = {name: rock.convex_rock(faces, f"{label} {name}") for name, faces in roles.items()
                if re.fullmatch(r"FloatingRock\d+", name)}
    require(masses and (1 <= len(floating) <= 3 if exposed else not floating),
            f"{label}: incorrect bottom attached/floating rock topology")
    require(all(value["maximum"][2] <= .777+TOLERANCE for value in [*masses.values(), *floating.values()]),
            f"{label}: lower bottom rocks enter the shoulder band")
    rooted_masses(masses, roles["IslandCore"], label)
    sizes = [float((value["maximum"]-value["minimum"])[:2].max()) for value in floating.values()]
    require(not sizes or max(sizes) >= .12, f"{label}: floating rocks are too small")
    require(len(sizes) < 2 or max(sizes)/min(sizes) >= 1.15, f"{label}: floating rocks lack size variation")
    audit_debris_motion_envelope(floating, masses, roles, exposed, label)
    shoulders = shoulder.audit_shoulder_geometry(roles, True, exposed, lip, label)
    return boundary, {"floor_projected_area": area, "boulder_masses": len(masses),
                      "floating_rocks": len(floating), **shoulders}


def audit_body(path, row, contract, original, assets, verified_images):
    label = path.name
    require(native.digest(path) == row["exportSha256"], f"{label}: export changed since current source capture")
    document = read_native_document(path)
    primitives, streams = native.read_primitives(document), rock.texture_coordinates(document)
    body, bottom, square = row["body"], row["bottom"], row["squareTop"]
    exposed, lip = row["exposedSideMask"], 1 if square else .91
    debris = export_debris_values(document, primitives, label)
    match_corner_records(export_corner_records(primitives, streams, debris), source_corner_records(body, label), label)
    debris_report = audit_debris_groups(body, bottom, exposed, label)
    properties = body["properties"]
    require(document.data["nodes"][0].get("extras", {}) == properties,
            f"{label}: exported properties differ from the editable source")
    require(properties.get("GroundModuleVariant") == row["variant"]
            and properties.get("GroundModuleShape") == row["shape"]
            and properties.get("ExposedSideMask") == exposed
            and abs(properties.get("NativeLipHeight", -1)-lip) < TOLERANCE
            and properties.get("BottomRevision" if bottom else "RegularDesignRevision") == (5 if bottom else 3)
            and properties.get("ShoulderTransitionRevision") == 1
            and abs(properties.get("ShoulderTransitionMinimumZ", -1)-.4) < TOLERANCE,
            f"{label}: invalid current identity/revision properties")
    roles = shoulder.source_roles(body, bottom, label)
    points = np.asarray(body["points"])
    allowance = .04 if bottom else 0
    minimum = np.asarray([-allowance if exposed & 8 else 0, -allowance if exposed & 1 else 0])
    maximum = np.asarray([1+allowance if exposed & 2 else 1, 1+allowance if exposed & 4 else 1])
    require(np.all(points[:, :2] >= minimum-TOLERANCE) and np.all(points[:, :2] <= maximum+TOLERANCE)
            and points[:, 2].min() >= (-1 if bottom else 0)-TOLERANCE and points[:, 2].max() <= lip+TOLERANCE,
            f"{label}: body escapes its current exposed/hidden-side envelope")
    core = roles["IslandCore" if bottom else "RegularCore"]
    lip_faces = core[np.all(np.abs(core[:, :, 2]-lip) < TOLERANCE, axis=1)]
    require(shoulder.triangle_counter(roles.get("CapLipSeal", [])) == shoulder.triangle_counter(lip_faces),
            f"{label}: CapLipSeal does not own the complete horizontal seal")
    if bottom:
        require(not body.get("reliefFaces", []) and not properties.get("RegularReliefRevision"),
                f"{label}: bottom received regular-only relief")
        boundary, geometry = audit_bottom_geometry(body, roles, exposed, lip, label)
    else:
        reference = row["reference"]
        roles, geometry = relief.audit_source_refinement(body, reference, False, exposed, lip, label)
        reference_roles = shoulder.source_roles(reference, False, "Frozen "+label)
        geometry.update(audit_relief_corner_streams(body, reference, label))
        geometry["closed_rock_roles"] = relief.audit_closed_roles(roles, reference_roles, label)
        # Relief is intentionally concave. Test rooting/clearance against the
        # frozen outside shells, then measure actual current shoulder coverage.
        shoulder.audit_shoulder_geometry(reference_roles, False, exposed, lip, label)
        masses = {name: rock.convex_rock(faces, f"Frozen {label} {name}") for name, faces in reference_roles.items()
                  if re.fullmatch(r"BoulderMass\d+", name)}
        require(bool(masses) if exposed else not masses, f"{label}: incorrect regular boulder topology")
        require(all(value["minimum"][2] >= .02-TOLERANCE and value["maximum"][2] <= .777+TOLERANCE
                    for value in masses.values()), f"{label}: regular boulders leave their lower-band clearance")
        rooted_masses(masses, reference_roles.get("RecessBacking", []), label)
        native.body_bottom(primitives, label)
        require(properties.get("RegularReliefRevision") == 1
                and properties.get("RegularReliefMaximumDepth") == .0072
                and json.loads(properties.get("RegularReliefFaces", "null")) == body.get("reliefFaces", [])
                and properties.get("RegularBoulderMassCount") == len(masses), f"{label}: invalid regular relief properties")
        shoulders = [faces for name, faces in roles.items() if re.fullmatch(r"ShoulderRock\d+", name)]
        geometry["near_lip_coverage"] = audit_shoulder_coverage(np.concatenate(shoulders) if shoulders else [], exposed, lip, label)
        boundary = None
    require(properties.get("ShoulderRockCount") == sum(bool(re.fullmatch(r"ShoulderRock\d+", name)) for name in roles),
            f"{label}: incorrect shoulder count metadata")
    materials = audit_materials(document, original, contract, assets, verified_images)
    return primitives, boundary, {"file": label, "bottom": bottom, "square_top": square,
                                   "source_corner_streams_match": True, **materials, **geometry, **debris_report}


def audit_caps_source(bundle_caps, chamfered, square, label="Current caps"):
    indexed = {}
    for row in bundle_caps:
        require(isinstance(row, dict) and isinstance(row.get("object"), str) and row["object"] not in indexed,
                f"{label}: invalid or duplicate source cap object")
        indexed[row["object"]] = row
    tables = [(cap, False, key) for key, cap in chamfered.items()] + [(cap, True, key) for key, cap in square.items()]
    require(set(indexed) == {cap["object"] for cap, _, _ in tables}, f"{label}: incomplete native cap object coverage")
    for cap, is_square, (variant, shape, concave) in tables:
        source = indexed[cap["object"]]
        points = np.asarray(source.get("points"), dtype=float)
        require(points.shape == cap["points"].shape and np.isfinite(points).all()
                and np.allclose(points, cap["points"], atol=TOLERANCE, rtol=0)
                and source.get("polygons") == cap["faces"], f"{cap['object']}: cap table differs from its native source geometry")
        properties = source.get("properties", {})
        require(properties.get("GroundModuleVariant") == variant
                and properties.get("ExposedSideMask") == dict(rock.FAMILIES)[list(dict(rock.FAMILIES))[shape]]
                and properties.get("ConcaveCorners") == concave
                and bool(properties.get("SquareTop", False)) == is_square,
                f"{cap['object']}: invalid native cap identity properties")
        corners = sum(len(face) for face in cap["faces"])
        normals = np.asarray(source.get("cornerNormals"), dtype=float)
        require(normals.shape == (corners, 3) and np.isfinite(normals).all()
                and np.allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-4, rtol=0),
                f"{cap['object']}: invalid cap native corner normals")
        start = 0
        for face, normal in zip(cap["faces"], cap["normals"]):
            require(np.max(np.abs(normals[start:start+len(face)]-normal)) <= TOLERANCE,
                    f"{cap['object']}: source cap corner normals differ from its runtime face normals")
            start += len(face)
        names, layers = source.get("uvLayerNames"), source.get("uvLayers", {})
        require(isinstance(names, list) and len(names) == len(set(names)) and set(names) == set(layers),
                f"{cap['object']}: ambiguous cap UV layer order")
        for name in names:
            uv = np.asarray(layers[name], dtype=float)
            require(uv.shape == (corners, 2) and np.isfinite(uv).all(), f"{cap['object']}: invalid cap UV layer")
        materials = source.get("faceMaterials")
        require(isinstance(materials, list) and len(materials) == len(cap["faces"])
                and all(name == "GroundModuleCap" for name in materials),
                f"{cap['object']}: cap does not use the authored GroundModuleCap material")
    return {"native_caps_crosschecked": len(tables), "source_geometry_and_identity_match_tables": True}


def audit(root=ROOT, *, models=None, contract_path=None, source_audit=None):
    root = Path(root).resolve()
    assets = root / "assets"
    contract_path = Path(contract_path or assets/"custom/source/ground_modules/contract.json").resolve()
    models = Path(models or assets/"custom/models/ground_modules").resolve()
    source_audit = Path(source_audit or contract_path.parent/"current_source_audit.json.gz").resolve()
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    audit_contract(contract)
    bundle, rows = load_evidence(source_audit, contract_path, contract, root)
    manifest = audit_manifest(json.loads((assets/"manifest.json").read_text(encoding="utf-8")), rows)
    require({path.name for path in models.glob("*.glb")} == {model_name(identity) for identity in rows},
            "Current export directory does not contain exactly the 240 native body identities")
    originals = {variant: read_native_document(assets/f"custom/pbr/models/GroundRock{variant+1:02d}.glb") for variant in range(10)}
    loaded, bottoms, reports, verified_images = {False: {}, True: {}}, {}, [], set()
    for identity, row in sorted(rows.items()):
        bottom, square, shape, variant = identity
        primitives, boundary, report = audit_body(models/model_name(identity), row, contract, originals[variant], assets, verified_images)
        if bottom:
            bottoms[(square, shape, variant)] = {"floor_boundary": boundary, "primitives": primitives,
                                                   "regular_primitives": loaded[square][(shape, variant)]}
        else:
            loaded[square][(shape, variant)] = primitives
        reports.append(report)
    caps, cap_report = native.read_native_caps(contract_path.parent/contract["capSurfaces"], contract)
    square_caps, square_report = native.read_square_caps(contract_path.parent/contract["squareCapSurfaces"], contract)
    source_caps = audit_caps_source(bundle["caps"], caps, square_caps)
    return {"status": "PASS", "validation_mode": "current", "native_models": len(rows), "native_caps": 310,
            "source_sha256": bundle["source"]["sha256"], "source_export_corner_streams_match": True,
            "historical_artifacts_required": False, "runtime_manifest": manifest,
            "source_corner_position_uv_tolerance": TOLERANCE, "source_corner_normal_tolerance": NORMAL_TOLERANCE,
            "chamfered_caps": cap_report, "square_caps": square_report,
            "source_caps": source_caps, "caps_and_joins": native.audit_caps_v2(caps, contract),
            "body_cap_closure": native.audit_body_cap_closure_v2(loaded[False], caps, contract),
            "layer_contacts": native.audit_square_contacts(loaded[False], loaded[True], square_caps, contract),
            "bottom_joins": rock.audit_rotated_joins(bottoms), "models": reports}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--models", type=Path)
    parser.add_argument("--contract", type=Path)
    parser.add_argument("--source-audit", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = audit(args.root, models=args.models, contract_path=args.contract, source_audit=args.source_audit)
        if args.output is not None:
            native.write_json(args.output, result)
        print(json.dumps({key: value for key, value in result.items() if key != "models"}))
    except (AssertionError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL current native ground: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
