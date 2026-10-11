#!/usr/bin/env python3
"""Audit explicitly authored chunky regular ground without relaxing legacy checks.

Called by validate_ground_modules.py --regular-revision chunky-v1. Frozen regular
exports establish the immutable top band, backing and flat base; a separate
pre-authoring baseline protects accepted island bottoms, caps and PBR maps.
"""
from __future__ import annotations

from collections import Counter
import copy
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image

import validate_ground_modules as native
import validate_ground_bottoms as geometry
from pbr_gltf import Document

REMODEL_HEIGHT = .78
ROCK_MINIMUM_Z = .02
ROCK_MAXIMUM_Z = .777
REVISION = 1
TOLERANCE = native.TOLERANCE


def source_roles(body, label, exposed=None):
    points = np.asarray(body["points"], dtype=float)
    polygons = np.asarray(body["polygons"], dtype=np.int64)
    groups = dict(body["faceGroups"])
    vertex_groups = dict(body["vertexGroups"])
    # An interior is the original two base faces and has no backing walls to
    # select. Blender does not serialize an unused role in that case.
    if exposed == 0 and "RecessBacking" not in groups:
        groups["RecessBacking"], vertex_groups["RecessBacking"] = [], []
    native.require(points.shape == (body["vertices"], 3) and len(polygons) == body["faces"]
                   and polygons.ndim == 2 and polygons.shape[1] == 3 and np.isfinite(points).all()
                   and np.all(polygons >= 0) and np.all(polygons < len(points)),
                   f"{label}: invalid triangulated native source geometry")
    native.require(all(name in groups for name in ("RegularCore", "RecessBacking", "FlatBase")),
                   f"{label}: missing editable regular core/backing/base ownership")
    native.require(all(len(indices) == len(set(indices)) and all(0 <= index < len(polygons) for index in indices)
                       for indices in groups.values()), f"{label}: invalid regular source face indices")
    core = set(groups["RegularCore"])
    native.require(set(groups["RecessBacking"]) <= core and set(groups["FlatBase"]) <= core
                   and not set(groups["RecessBacking"]) & set(groups["FlatBase"]),
                   f"{label}: backing/base ownership escapes or overlaps its core")
    masses = [name for name in groups if re.fullmatch(r"BoulderMass\d+", name)]
    coverage = Counter(index for name in ["RegularCore"]+masses for index in groups[name])
    native.require(set(coverage) == set(range(len(polygons))) and all(count == 1 for count in coverage.values()),
                   f"{label}: missing or overlapping primary regular roles")
    native.require(not any(name.startswith("FloatingRock") for name in groups),
                   f"{label}: regular ground contains detached floating-rock roles")
    for name, indices in groups.items():
        native.require(name in vertex_groups
                       and {int(vertex) for index in indices for vertex in polygons[index]}
                       <= set(vertex_groups[name]), f"{label}: editable {name} selection misses its faces")
    native.require(np.allclose(points.min(0), body["bounds"]["min"], atol=TOLERANCE, rtol=0)
                   and np.allclose(points.max(0), body["bounds"]["max"], atol=TOLERANCE, rtol=0),
                   f"{label}: native source bounds differ from its geometry")
    return {name: points[polygons[indices]] for name, indices in groups.items()}


def retained_signatures(primitives, streams, kind):
    """Exact oriented retained face positions, normals, material and every UV."""
    result = Counter()
    for primitive, coordinates in zip(primitives, streams):
        for triangle in primitive.triangles:
            face = primitive.points[triangle]
            selected = primitive.material.get("name") == "GroundModuleRecess" if kind == "backing" else np.all(np.abs(face[:, 2]) < TOLERANCE)
            if not selected:
                continue
            corners = [(tuple(float(value) for value in primitive.points[index]),
                        tuple(float(value) for value in primitive.normals[index]),
                        tuple((name, tuple(float(value) for value in values[index])) for name, values in sorted(coordinates.items())))
                       for index in triangle]
            canonical = min(tuple(corners[offset:]+corners[:offset]) for offset in range(3))
            result[(primitive.material.get("name", ""), canonical)] += 1
    return result


def audit_retained_geometry(primitives, reference_primitives, streams, reference_streams, label):
    for kind in ("backing", "base"):
        current, original = retained_signatures(primitives, streams, kind), retained_signatures(reference_primitives, reference_streams, kind)
        native.require(current == original, f"{label}: original {kind} faces/positions/normals/material/UV changed")
        if kind == "base":
            native.require(sum(current.values()) == 2, f"{label}: regular ground lost its exact two base triangles")
    # Reuse the original full-unit downward disk validation, unchanged.
    native.body_bottom(primitives, label)
    return {"backing_faces_normals_material_and_all_uv_identical": True,
            "original_two_base_faces_normals_material_and_all_uv_identical": True}


def audit_regular_geometry(primitives, body, exposed, lip_height, label):
    roles = source_roles(body, label, exposed)
    faces = np.concatenate([primitive.points[primitive.triangles] for primitive in primitives])
    points = faces.reshape(-1, 3)
    native.require(Counter(geometry.triangle_key(face) for face in faces)
                   == Counter(geometry.triangle_key(face) for face in np.asarray(body["points"])[np.asarray(body["polygons"])]),
                   f"{label}: source/export regular triangle ownership differs")
    # Original top-band rock facets have small authored protrusions. Their
    # positions are independently preserved; all replacement lower geometry
    # must remain inside the square footprint.
    lower_points = points[points[:, 2] < REMODEL_HEIGHT-TOLERANCE]
    native.require(np.all(lower_points[:, :2] >= -TOLERANCE) and np.all(lower_points[:, :2] <= 1+TOLERANCE)
                   and points[:, 2].min() >= -TOLERANCE and points[:, 2].max() <= lip_height+TOLERANCE,
                   f"{label}: regular ground escapes its unit/lip envelope")
    flat = faces[np.all(np.abs(faces[:, :, 2]) < TOLERANCE, axis=1)]
    backing_panels = [p.points[p.triangles] for p in primitives if p.material.get("name") == "GroundModuleRecess"]
    backing = np.concatenate(backing_panels) if backing_panels else np.empty((0, 3, 3))
    for name, actual in (("FlatBase", flat), ("RecessBacking", backing)):
        native.require(Counter(geometry.triangle_key(face) for face in roles[name])
                       == Counter(geometry.triangle_key(face) for face in actual),
                       f"{label}: native {name} role differs from its actual exported faces")
    core_extra = sorted(set(body["faceGroups"]["RegularCore"])
                        -set(body["faceGroups"].get("RecessBacking", []))-set(body["faceGroups"]["FlatBase"]))
    core_faces = np.asarray(body["points"])[np.asarray(body["polygons"])[core_extra]]
    native.require(not len(core_faces) or np.all(core_faces[:, :, 2] >= REMODEL_HEIGHT-geometry.CUT_TOLERANCE),
                   f"{label}: old fractured lower plates remain in the regular core")
    masses = {name: geometry.convex_rock(role, f"{label} {name}") for name, role in roles.items()
              if re.fullmatch(r"BoulderMass\d+", name)}
    native.require(len(masses) > 0 if exposed else len(masses) == 0 and len(faces) == 2,
                   f"{label}: incorrect chunky regular/interior mass topology")
    for name, mass in masses.items():
        native.require(mass["minimum"][2] >= ROCK_MINIMUM_Z-TOLERANCE
                       and mass["maximum"][2] <= ROCK_MAXIMUM_Z+TOLERANCE,
                       f"{label} {name}: boulder leaves its protected base/top-band clearance")
    roots = {name for name, mass in masses.items() if geometry.core_intersection(mass, roles["RecessBacking"])}
    graph = {name: set() for name in masses}
    names = list(masses)
    for index, name in enumerate(names):
        for other in names[index+1:]:
            if geometry.convex_gap(masses[name], masses[other]) < -TOLERANCE:
                graph[name].add(other)
                graph[other].add(name)
    reached, pending = set(), list(roots)
    while pending:
        name = pending.pop()
        if name not in reached:
            reached.add(name)
            pending.extend(graph[name]-reached)
    native.require(reached == set(masses), f"{label}: regular boulder is not rooted in retained backing")
    properties = body["properties"]
    native.require(properties.get("RegularDesignRevision") == REVISION
                   and abs(properties.get("RegularRemodelHeight", -1)-REMODEL_HEIGHT) < TOLERANCE
                   and abs(properties.get("RegularRockMinimumZ", -1)-ROCK_MINIMUM_Z) < TOLERANCE
                   and abs(properties.get("RegularRockMaximumZ", -1)-ROCK_MAXIMUM_Z) < TOLERANCE
                   and properties.get("RegularBoulderMassCount") == len(masses),
                   f"{label}: native regular revision/rock metadata is missing or incorrect")
    return {"boulder_masses": len(masses), "directly_rooted_masses": len(roots), "all_masses_rooted": True,
            "source_positions_triangles_and_roles_match_export": True,
            "lower_plate_surfaces_replaced": True, "unit_xy_below_preserved_top_band_and_protected_height_envelope": True,
            "boulders": {name: {key: mass[key] for key in ("volume", "principal_axis_ratio", "facets", "median_facet_area")}
                         for name, mass in masses.items()}}


def material_signature(document, material):
    value = copy.deepcopy(material)
    for owner, name in ((value.get("pbrMetallicRoughness", {}), "baseColorTexture"),
                        (value.get("pbrMetallicRoughness", {}), "metallicRoughnessTexture"),
                        (value, "normalTexture"), (value, "occlusionTexture"), (value, "emissiveTexture")):
        if name in owner:
            info = owner[name]
            info["index"] = native.texture_path(document, info["index"]).relative_to(native.ASSETS).as_posix()
    return value


def require_original_material(document, material, originals, label):
    name = material.get("name")
    native.require(name in originals and material_signature(document, material) == originals[name],
                   f"{label}: original PBR material name or parameters changed")


def verify_protection_manifests(protected, originals, prekit, accepted_bottom, references, regular_names, cap_names):
    native.require(accepted_bottom.get("status") == "PASS"
                   and accepted_bottom.get("native_source", {}).get("bottom_revision") == 4,
                   "Accepted bottom protection must reference the passing V4 audit")
    native.require(set(prekit) == set(regular_names) | set(cap_names)
                   and references == {name: prekit[name] for name in regular_names},
                   "Frozen regular references do not match the preserved pre-authoring kit baseline")
    accepted = {row["file"]: row["sha256"] for row in accepted_bottom["models"]}
    native.require(len(accepted) == accepted_bottom["native_models"], "Incomplete or repeated accepted bottom protection records")
    expected = {**originals, **accepted, **{name: prekit[name] for name in cap_names}}
    native.require(protected == expected, "Protected hashes do not exactly cover accepted V4 bottoms, original caps and legacy/PBR files")
    return True


def audit_body(path, reference_path, body, exposed, lip_height):
    document, reference = Document(path), Document(reference_path)
    primitives, old = native.read_primitives(document), native.read_primitives(reference)
    # Frozen GLBs were copied from this authored directory. Their external
    # texture URIs are interpreted at that original location, without editing.
    reference.path = path
    streams, old_streams = geometry.texture_coordinates(document), geometry.texture_coordinates(reference)
    retention = audit_retained_geometry(primitives, old, streams, old_streams, path.name)
    band = geometry.audit_top_band(primitives, old, streams, old_streams, path.name) if exposed else {"interior_has_no_top_band": True}
    report = audit_regular_geometry(primitives, body, exposed, lip_height, path.name)
    originals = {material["name"]: material_signature(reference, material) for material in reference.data["materials"]}
    maps = [native.material_maps(reference, material) for material in reference.data["materials"]]
    for primitive in primitives:
        material = primitive.material
        require_original_material(document, material, originals, path.name)
        if material.get("name") != "GroundModuleRecess":
            current_maps = native.material_maps(document, material)
            native.require(set(current_maps) == {"albedo", "normal", "orm", "ao"} and current_maps in maps,
                           f"{path.name}: regular boulder/stone lacks original full rock PBR bindings")
    for image in document.data.get("images", []):
        native.require("uri" in image and "bufferView" not in image, "Embedded regular texture was not finalized")
        resource = native.resource_path(path.parent, image["uri"])
        native.require(resource.is_relative_to(native.ASSETS) and resource.suffix.lower() == ".png", "Invalid regular PBR dependency")
        with Image.open(resource) as decoded:
            decoded.verify()
    return primitives, {**retention, **report, "top_band": band, "file": path.relative_to(native.ROOT).as_posix(),
                        "sha256": native.digest(path), "reference_sha256": native.digest(reference_path),
                        "bytes": path.stat().st_size, "vertices": sum(len(p.points) for p in primitives),
                        "triangles": sum(len(p.triangles) for p in primitives), "min_face_normal_agreement": min(p.min_normal_dot for p in primitives)}


def audit_from_native_args(args, fixture=None):
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    native.require(contract["format"] == 2 and contract.get("regularDesignRevision") == REVISION
                   and contract["nativeCapCount"] == 250 and contract["squareNativeCapCount"] == 60
                   and contract["preserveOriginalXY"] is True, "Unexpected explicit chunky regular contract")
    original_count = native.verify_baseline(args.baseline)
    protected_count = native.verify_baseline(args.regular_protected_baseline)
    native.require(protected_count == 138, "Incomplete accepted-bottom/cap/PBR protection baseline")
    references = json.loads(args.regular_reference_baseline.read_text(encoding="utf-8"))
    native.require(len(references) == 120, "Incomplete pre-regular reference hash baseline")
    regular_names = {(args.models / f"ground_{'square_' if square else ''}{family['name']}_{variant+1:02}.glb")
                     .relative_to(native.ROOT).as_posix()
                     for square in (False, True) for family in contract["families"] for variant in range(10)}
    cap_names = [(args.contract.parent / contract[key]).relative_to(native.ROOT).as_posix()
                 for key in ("capSurfaces", "squareCapSurfaces")]
    verify_protection_manifests(json.loads(args.regular_protected_baseline.read_text()),
                                json.loads(args.baseline.read_text()), json.loads(args.pre_regular_kit_baseline.read_text()),
                                json.loads(args.accepted_bottom_audit.read_text()), references, regular_names, cap_names)
    rows = json.loads(args.regular_source_audit.read_text(encoding="utf-8"))
    sources = {(bool(row["squareTop"]), row["shape"], int(row["variant"])): row for row in rows}
    native.require(len(rows) == len(sources) == 120, "Incomplete native chunky regular source snapshot")
    loaded, square_loaded, models, square_models = {}, {}, [], []
    for square in (False, True):
        lip = contract["squareOuterLipHeight"] if square else contract["outerLipHeight"]
        for family in contract["families"]:
            for variant in range(10):
                path = args.models / f"ground_{'square_' if square else ''}{family['name']}_{variant+1:02}.glb"
                reference = args.pre_regular_models / path.name
                native.require(native.digest(reference) == references[path.relative_to(native.ROOT).as_posix()],
                               f"Frozen pre-regular reference changed: {path.name}")
                row = sources[(square, family["name"], variant)]
                native.require(row["exposedSideMask"] == family["mask"], "Native regular exposure mask differs")
                primitives, report = audit_body(path, reference, row["body"], family["mask"], lip)
                print(f"PASS regular body {path.name}", flush=True)
                target = square_loaded if square else loaded
                target[(family["name"], variant)] = primitives
                report.update(variant=variant+1, canonical_exposed_mask=family["mask"], square_top=square, shape=family["name"])
                (square_models if square else models).append(report)
    cap_path = args.contract.parent / contract["capSurfaces"]
    caps, native_caps = native.read_native_caps(cap_path, contract)
    topology = native.audit_caps_v2(caps, contract)
    closure = native.audit_body_cap_closure_v2(loaded, caps, contract)
    square_cap_path = args.contract.parent / contract["squareCapSurfaces"]
    square_caps, native_square_caps = native.read_square_caps(square_cap_path, contract)
    contacts = native.audit_square_contacts(loaded, square_loaded, square_caps, contract)
    native.require(args.source_audit.is_file() and args.square_source_audit.is_file(), "Native body/top source snapshots missing")
    source = native.audit_native_source_v2(args.source_audit, args.contract, contract, loaded, caps)
    square_contract = {**contract, "lipDepth": 0, "outerLipHeight": contract["squareOuterLipHeight"], "preservationLipDepth": contract["lipDepth"]}
    square_source = native.audit_native_source_v2(args.square_source_audit, args.contract, square_contract, square_loaded, square_caps)
    result = {"status": "PASS", "regular_design_revision": REVISION, "native_models": 120,
              "preserved_original_model_and_map_hashes": original_count, "preserved_v4_bottom_cap_legacy_and_pbr_files": protected_count,
              "frozen_regular_reference_files": len(references), "native_caps": native_caps, "caps_and_joins": topology,
              "body_cap_closure": closure, "native_source": source,
              "regular_source_snapshot_sha256": native.digest(args.regular_source_audit),
              "cap_surfaces_sha256": native.digest(cap_path), "interior_bodies_have_only_original_two_base_triangles": True,
              "models": models, "square_modules": {"native_models": 60, "native_caps": native_square_caps,
                  "cap_surfaces_sha256": native.digest(square_cap_path), "layer_contacts": contacts,
                  "native_source": square_source, "models": square_models}}
    if fixture:
        result["fixture"] = fixture
    # The approved regular-reference baseline is emitted only after every
    # retention, protected-file, native-source, cap and join proof passes.
    if args.approve_bottom_baseline:
        approved = {report["file"]: report["sha256"] for report in models+square_models}
        approved.update({cap.relative_to(native.ROOT).as_posix(): native.digest(cap) for cap in (cap_path, square_cap_path)})
        native.require(len(approved) == 122, "Incomplete approved regular/cap reference baseline")
        approved_path = args.approve_bottom_baseline.absolute()
        native.write_json(approved_path, approved)
        result["approved_bottom_reference_baseline"] = {"file": approved_path.relative_to(native.ROOT).as_posix(),
                                                       "sha256": native.digest(approved_path), "files": 122}
    native.write_json(args.output, result)
    summary = {key: value for key, value in result.items() if key not in ("models", "square_modules")}
    summary["square_modules"] = {key: value for key, value in result["square_modules"].items() if key != "models"}
    print(json.dumps(summary))
    return result
