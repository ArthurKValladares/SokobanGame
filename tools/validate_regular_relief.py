#!/usr/bin/env python3
"""Audit sparse inward relief on accepted regular ground rock faces.

This is a separate revision audit. Historical shoulder/bottom validators and
their reference reports remain unchanged. Every original rock edge survives;
only explicit triangle interiors may receive shallow, inward three-face fans.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import copy
import json
from pathlib import Path
import re
import struct

import numpy as np

import validate_ground_modules as native
import validate_ground_bottoms as rock
import validate_regular_ground as regular
import validate_ground_shoulders as shoulder
from ground_shoulder_coverage import audit_shoulder_coverage
from pbr_gltf import Document

ROOT = native.ROOT
OUT = ROOT / "out/ground-modules"
ACCEPTED = OUT / "pre-regular-relief-20261011"
MAXIMUM_DEPTH = .0072
POSITION_TOLERANCE = 2e-7
UV_TOLERANCE = 2e-7
ROLE = re.compile(r"(?:BoulderMass|ShoulderRock)\d+")
RECESS_BASE_COLOR = [.61, .48, .28, 1]
RECESS_COLOR_TOLERANCE = 1e-6


def oriented_key(values):
    values = tuple(tuple(float(value) for value in point) for point in values)
    return min(values[offset:]+values[:offset] for offset in range(3))


def index_key(values):
    values = tuple(int(value) for value in values)
    return min(values[offset:]+values[:offset] for offset in range(3))


def triangle_records(primitives, streams):
    output = defaultdict(list)
    for primitive, coordinates in zip(primitives, streams):
        for indices in primitive.triangles:
            output[oriented_key(primitive.points[indices])].append({
                "points": primitive.points[indices], "normals": primitive.normals[indices],
                "material": primitive.material.get("name", ""),
                "uv": {name: values[indices] for name, values in coordinates.items()}})
    return output


def audit_source_refinement(body, accepted, bottom, exposed, lip, label):
    """Prove exactly the declared triangle-interior refinement and nothing else."""
    current_roles = shoulder.source_roles(body, bottom, label)
    old_roles = shoulder.source_roles(accepted, bottom, "Accepted "+label)
    points, old_points = np.asarray(body["points"]), np.asarray(accepted["points"])
    polygons, old_polygons = np.asarray(body["polygons"]), np.asarray(accepted["polygons"])
    changes = body.get("reliefFaces", [])
    native.require(isinstance(changes, list), f"{label}: reliefFaces must be a list")
    if bottom:
        native.require(not changes, f"{label}: bottom ground cannot receive regular relief")
        for name in ("points", "polygons", "vertices", "faces", "vertexGroups", "faceGroups", "bounds", "properties"):
            native.require(body[name] == accepted[name], f"{label}: protected bottom native {name} changed")
        return current_roles, {"selected_faces": 0, "bottom_source_unchanged": True}
    native.require(len(points) >= len(old_points) and np.array_equal(points[:len(old_points)], old_points),
                   f"{label}: original vertices or tile silhouette changed")
    native.require(len(points) == len(old_points)+len(changes), f"{label}: undeclared added or removed vertices")
    native.require(set(current_roles) == set(old_roles), f"{label}: native editable rock roles changed")
    for name, old_indices in accepted["vertexGroups"].items():
        actual = set(body["vertexGroups"][name])
        native.require(actual & set(range(len(old_points))) == set(old_indices),
                       f"{label}: original {name} vertex ownership changed")
    native.require(np.array_equal(points.min(0), old_points.min(0)) and np.array_equal(points.max(0), old_points.max(0)),
                   f"{label}: original overall bounds changed")
    native.require(np.all(points[:, :2] >= -native.TOLERANCE) and np.all(points[:, :2] <= 1+native.TOLERANCE)
                   and points[:, 2].min() >= -native.TOLERANCE and points[:, 2].max() <= lip+native.TOLERANCE,
                   f"{label}: regular relief escapes its unit/lip footprint")
    selected, centers, depths, slopes, expected = set(), set(), [], [], Counter(index_key(p) for p in old_polygons)
    expected_roles = {name: Counter(index_key(old_polygons[index]) for index in indices)
                      for name, indices in accepted["faceGroups"].items()}
    for item in changes:
        parent, center, role = item.get("parent"), item.get("center"), item.get("role", "")
        native.require(type(parent) is int and 0 <= parent < len(old_polygons) and parent not in selected,
                       f"{label}: invalid or repeated relief parent")
        native.require(type(center) is int and len(old_points) <= center < len(points) and center not in centers,
                       f"{label}: invalid or repeated relief center")
        native.require(ROLE.fullmatch(role) and parent in accepted["faceGroups"].get(role, []),
                       f"{label}: relief parent is outside its accepted rock role")
        vertices = item.get("vertices")
        native.require(vertices == old_polygons[parent].tolist(), f"{label}: relief parent vertices or winding changed")
        bary = np.asarray(item.get("barycentric", []), dtype=float)
        depth = item.get("depth", -1)
        native.require(bary.shape == (3,) and np.isfinite(bary).all() and np.all(bary > .001)
                       and abs(float(bary.sum())-1) <= 1e-9,
                       f"{label}: relief barycentric center is not strictly inside its parent")
        native.require(isinstance(depth, (float, int)) and np.isfinite(depth) and 0 < depth <= MAXIMUM_DEPTH,
                       f"{label}: relief exceeds the agreed shallow inward depth")
        face = old_points[vertices]
        cross = np.cross(face[1]-face[0], face[2]-face[0]); normal = cross/np.linalg.norm(cross)
        outward = np.asarray([-normal[1], normal[0], normal[1], -normal[0]])
        native.require(any(exposed & (1 << side) and outward[side] > .85 for side in range(4)),
                       f"{label}: relief changes a hidden, top or non-cardinal rock face")
        expected_center = bary@face-depth*normal
        native.require(np.max(np.abs(points[center]-expected_center)) <= POSITION_TOLERANCE,
                       f"{label}: new center moves laterally or outward instead of its declared inward relief")
        actual_depth = float((bary@face-points[center])@normal)
        native.require(0 < actual_depth <= MAXIMUM_DEPTH+POSITION_TOLERANCE,
                       f"{label}: actual relief depth is outside its shallow inward limit")
        altitude = min(np.linalg.norm(np.cross(bary@face-a, b-a))/np.linalg.norm(b-a)
                       for a, b in zip(face, np.roll(face, -1, axis=0)))
        native.require(actual_depth <= altitude*.06+POSITION_TOLERANCE,
                       f"{label}: relief facet slope exceeds the subtle six-percent limit")
        original_role = old_roles[role]
        cross = np.cross(original_role[:, 1]-original_role[:, 0], original_role[:, 2]-original_role[:, 0])
        plane_normals = cross/np.linalg.norm(cross, axis=1)[:, None]
        native.require(np.all(np.einsum("ij,ij->i", points[center]-original_role[:, 0], plane_normals) <= native.TOLERANCE),
                       f"{label}: new center leaves its accepted rock envelope")
        children = [(vertices[index], vertices[(index+1) % 3], center) for index in range(3)]
        expected[index_key(vertices)] -= 1
        expected.update(index_key(child) for child in children)
        for name, faces in expected_roles.items():
            if parent in accepted["faceGroups"][name]:
                faces[index_key(vertices)] -= 1
                faces.update(index_key(child) for child in children)
                native.require(center in body["vertexGroups"][name], f"{label}: center loses editable {name} ownership")
            else:
                native.require(center not in body["vertexGroups"][name], f"{label}: center acquires unrelated {name} ownership")
        selected.add(parent); centers.add(center); depths.append(actual_depth); slopes.append(actual_depth/altitude)
    native.require(centers == set(range(len(old_points), len(points))), f"{label}: undeclared interior points exist")
    native.require(+expected == Counter(index_key(p) for p in polygons),
                   f"{label}: child triangle coverage, winding or unchanged topology differs")
    for name, faces in expected_roles.items():
        native.require(+faces == Counter(index_key(polygons[index]) for index in body["faceGroups"][name]),
                       f"{label}: refined {name} face ownership changed")
    native.require(bool(changes) if exposed else not changes and len(polygons) == 2,
                   f"{label}: exposed/interior regular relief configuration differs")
    return current_roles, {"selected_faces": len(changes), "added_interior_vertices": len(changes),
                           "added_triangles": 2*len(changes), "all_original_vertices_edges_and_bounds_identical": True,
                           "exact_parent_triangle_fans_preserve_projected_coverage": True,
                           "minimum_relief_depth": min(depths) if depths else None,
                           "maximum_relief_depth": max(depths) if depths else None,
                           "maximum_new_facet_slope": max(slopes) if slopes else None,
                           "rms_relief_depth": float(np.sqrt(np.mean(np.square(depths)))) if depths else None}


def audit_export_refinement(primitives, old, streams, old_streams, body, accepted, label):
    points, old_points = np.asarray(body["points"]), np.asarray(accepted["points"])
    old_polygons = np.asarray(accepted["polygons"])
    changes = body.get("reliefFaces", [])
    records, old_records = triangle_records(primitives, streams), triangle_records(old, old_streams)
    actual_faces = Counter({key: len(value) for key, value in records.items()})
    native.require(actual_faces == Counter(oriented_key(points[p]) for p in body["polygons"]),
                   f"{label}: exported triangles differ from editable source")
    parent_keys, child_keys = set(), set()
    for item in changes:
        vertices, center = item["vertices"], item["center"]
        parent_key = oriented_key(old_points[old_polygons[item["parent"]]])
        parent_keys.add(parent_key)
        native.require(len(old_records.get(parent_key, [])) == 1, f"{label}: ambiguous accepted parent export")
        parent = old_records[parent_key][0]
        lookup = {tuple(point): index for index, point in enumerate(parent["points"])}
        order = [lookup[tuple(old_points[index])] for index in vertices]
        expected_uv = {name: np.asarray(item["barycentric"])@values[order] for name, values in parent["uv"].items()}
        for index in range(3):
            child = [vertices[index], vertices[(index+1) % 3], center]
            key = oriented_key(points[child]); child_keys.add(key)
            native.require(len(records.get(key, [])) == 1, f"{label}: missing or repeated relief child export")
            actual = records[key][0]
            native.require(actual["material"] == parent["material"] and set(actual["uv"]) == set(parent["uv"]),
                           f"{label}: relief child material or UV chart changed")
            cross = np.cross(actual["points"][1]-actual["points"][0], actual["points"][2]-actual["points"][0])
            normal = cross/np.linalg.norm(cross)
            native.require(np.max(np.abs(actual["normals"]-normal)) < 2e-4,
                           f"{label}: relief child normals do not describe its flat rocky facet")
            for corner, point in enumerate(actual["points"]):
                is_center = np.array_equal(point, points[center])
                native.require(is_center or tuple(point) in lookup, f"{label}: child has an unexpected parent corner")
                for name, values in actual["uv"].items():
                    if is_center:
                        native.require(np.max(np.abs(values[corner]-expected_uv[name])) <= UV_TOLERANCE,
                                       f"{label}: relief center UV is not parent barycentric interpolation")
                    else:
                        native.require(np.array_equal(values[corner], parent["uv"][name][lookup[tuple(point)]]),
                                       f"{label}: original relief-boundary UV corner changed")
    actual = shoulder.face_signatures(primitives, streams, lambda face, _: oriented_key(face) not in child_keys)
    expected = shoulder.face_signatures(old, old_streams, lambda face, _: oriented_key(face) not in parent_keys)
    native.require(actual == expected, f"{label}: unchanged face positions/normals/material/all UV streams changed")
    return {"unchanged_export_corner_streams_identical": True, "relief_boundary_uvs_identical": True,
            "new_center_all_uv_charts_are_parent_barycentric_interpolation": True,
            "flat_child_normal_and_tangent_frames_valid": True, "source_export_triangle_ownership_identical": True,
            "unchanged_export_triangles": sum(expected.values())}


def audit_closed_roles(roles, old_roles, label):
    reports = {}
    for name, faces in roles.items():
        if not ROLE.fullmatch(name):
            continue
        components = rock.components_for_faces(faces)
        native.require(len(components) == 1, f"{label} {name}: disconnected relief rock")
        rock.require_closed_rock(components[0], f"{label} {name}")
        area = np.linalg.norm(np.cross(faces[:, 1]-faces[:, 0], faces[:, 2]-faces[:, 0]), axis=1)/2
        native.require(np.all(area > 1e-11), f"{label} {name}: degenerate relief facets")
        volume = float(np.einsum("ij,ij->i", faces[:, 0], np.cross(faces[:, 1], faces[:, 2])).sum()/6)
        original = old_roles[name]
        old_volume = float(np.einsum("ij,ij->i", original[:, 0], np.cross(original[:, 1], original[:, 2])).sum()/6)
        native.require(volume > 1e-6 and .95*old_volume <= volume <= old_volume+1e-7,
                       f"{label} {name}: shallow relief changes too much outward rock volume")
        reports[name] = {"facets": len(faces), "outward_volume": volume, "accepted_volume": old_volume,
                         "retained_volume_fraction": volume/old_volume, "closed_consistently_wound": True}
    return reports


def accepted_recess_color(data, accepted, recess_factor, label):
    """Undo only the explicitly contracted backing color for exact comparison."""
    result = copy.deepcopy(data)
    if recess_factor is None:
        return result
    original = {material.get("name"): material for material in accepted["materials"]}
    for material in result["materials"]:
        if material.get("name") != "GroundModuleRecess":
            continue
        factor = material.get("pbrMetallicRoughness", {}).get("baseColorFactor")
        native.require(isinstance(factor, list) and len(factor) == 4
                       and all(type(value) in (int, float) and np.isfinite(value)
                               and abs(value-expected) <= RECESS_COLOR_TOLERANCE
                               for value, expected in zip(factor, recess_factor)),
                       f"{label}: recess base color differs from the explicit sandstone contract")
        native.require("GroundModuleRecess" in original,
                       f"{label}: unexpected new recess material")
        old_pbr = original["GroundModuleRecess"].get("pbrMetallicRoughness", {})
        if "baseColorFactor" in old_pbr:
            material["pbrMetallicRoughness"]["baseColorFactor"] = copy.deepcopy(old_pbr["baseColorFactor"])
        else:
            material["pbrMetallicRoughness"].pop("baseColorFactor")
    return result


def non_json_glb_chunks(path):
    """Retain every binary and extension chunk, including padding bytes."""
    raw = path.read_bytes()
    native.require(len(raw) >= 12, f"{path.name}: truncated GLB header")
    magic, version, length = struct.unpack_from("<III", raw)
    native.require(magic == 0x46546C67 and version == 2 and length == len(raw),
                   f"{path.name}: invalid GLB header")
    chunks, cursor, json_chunks = [], 12, 0
    while cursor < length:
        native.require(cursor+8 <= length, f"{path.name}: truncated GLB chunk header")
        size, kind = struct.unpack_from("<II", raw, cursor)
        end = cursor+8+size
        native.require(size % 4 == 0 and end <= length, f"{path.name}: invalid GLB chunk length")
        if kind == 0x4E4F534A:
            json_chunks += 1
        else:
            chunks.append((kind, raw[cursor+8:end]))
        cursor = end
    native.require(json_chunks == 1, f"{path.name}: expected exactly one GLB JSON chunk")
    return chunks


def audit_bottom_material_revision(document, accepted, recess_factor, label):
    native.require(accepted_recess_color(document.data, accepted.data, recess_factor, label) == accepted.data,
                   f"{label}: protected bottom JSON changed beyond the recess base color")
    native.require(non_json_glb_chunks(document.path) == non_json_glb_chunks(accepted.path),
                   f"{label}: protected bottom binary or extension chunk changed")
    return {"bottom_geometry_and_other_gltf_metadata_identical": True,
            "bottom_non_json_glb_chunks_byte_identical": True}


def audit_materials(document, accepted, label, recess_factor=None):
    actual = {m.get("name"): shoulder.material_signature(document, m) for m in document.data["materials"]}
    old = {m.get("name"): shoulder.material_signature(accepted, m) for m in accepted.data["materials"]}
    if recess_factor is not None:
        actual = accepted_recess_color({"materials": list(actual.values())},
                                       {"materials": list(old.values())}, recess_factor, label)
        actual = {material.get("name"): material for material in actual["materials"]}
    native.require(actual == old and len(actual) == len(document.data["materials"]) == len(accepted.data["materials"]),
                   f"{label}: original material table, PBR maps or sampler state changed")
    for item in document.data.get("images", []):
        native.require("uri" in item and "bufferView" not in item, f"{label}: unfinalized relief image")
        path = native.resource_path(document.path.parent, item["uri"])
        native.require(path.is_relative_to(native.ASSETS) and path.suffix.lower() == ".png", f"{label}: invalid PBR map")
    return {"all_other_original_materials_texture_maps_and_samplers_identical": True,
            "recess_base_color_factor": recess_factor}


def audit_reference_protection(before, protected, models):
    """Protect historical proof and every actual shared image dependency."""
    history = before["historical_baseline_and_validator_proof"]
    hashes = {**history["baseline_files_sha256"], **history["historical_validator_files_sha256"]}
    for name, expected in hashes.items():
        path = ROOT/name if name.startswith("tools/") else OUT/name
        native.require(native.digest(path) == expected, f"Historical reference or validator changed: {name}")
    original = json.loads((OUT/"original-ground-hashes.json").read_text(encoding="utf-8"))
    dependencies = set()
    for path in models.glob("*.glb"):
        for item in Document(path).data.get("images", []):
            native.require("uri" in item and "bufferView" not in item, f"{path.name}: unfinalized shared image")
            dependencies.add(native.resource_path(path.parent, item["uri"]).relative_to(ROOT).as_posix())
    native.require(dependencies <= set(original) | set(protected), "Actual shared PBR dependency lacks hash protection")
    for name in dependencies:
        native.require(native.digest(ROOT/name) == (protected.get(name) or original[name]),
                       f"Shared model image dependency changed: {name}")
    return {"status": "PASS", "historical_baseline_and_validator_hashes": len(hashes),
            "shared_model_image_dependencies": len(dependencies),
            "all_shared_model_image_dependencies_have_unchanged_hash_protection": True,
            "historical_baselines_and_validators_unchanged": True, "image_dependencies": sorted(dependencies)}


def audit_body(path, reference, row, accepted, recess_factor=None):
    bottom, square = row["bottom"], row["squareTop"]
    body, old_body = row["body"], accepted["body"]
    lip, exposed = (1 if square else .91), row["exposedSideMask"]
    roles, source_report = audit_source_refinement(body, old_body, bottom, exposed, lip, path.name)
    if bottom:
        if recess_factor is None:
            native.require(native.digest(path) == native.digest(reference), f"{path.name}: protected bottom GLB changed")
            export_report = {"bottom_glb_byte_identical": True}
        else:
            export_report = audit_bottom_material_revision(Document(path), Document(reference), recess_factor, path.name)
        return None, {"file": path.relative_to(ROOT).as_posix(), "sha256": native.digest(path), "bottom": True,
                      **source_report, **export_report}
    properties = body["properties"]
    native.require(properties.get("RegularDesignRevision") == 3 and properties.get("RegularReliefRevision") == 1
                   and properties.get("RegularReliefMaximumDepth") == MAXIMUM_DEPTH
                   and json.loads(properties.get("RegularReliefFaces", "null")) == body.get("reliefFaces", [])
                   and properties.get("ShoulderTransitionRevision") == 1
                   and properties.get("GroundModuleVariant") == row["variant"]
                   and properties.get("GroundModuleShape") == row["shape"]
                   and properties.get("ExposedSideMask") == exposed, f"{path.name}: incorrect regular relief metadata")
    for name, value in old_body["properties"].items():
        if name != "RegularDesignRevision":
            native.require(properties.get(name) == value, f"{path.name}: accepted {name} metadata changed")
    document, old_document = Document(path), Document(reference)
    old_document.path = path
    primitives, old = native.read_primitives(document), native.read_primitives(old_document)
    streams, old_streams = rock.texture_coordinates(document), rock.texture_coordinates(old_document)
    export_report = audit_export_refinement(primitives, old, streams, old_streams, body, old_body, path.name)
    materials = audit_materials(document, old_document, path.name, recess_factor)
    retained = regular.audit_retained_geometry(primitives, old, streams, old_streams, path.name)
    old_roles = shoulder.source_roles(old_body, False, "Accepted "+path.name)
    closed = audit_closed_roles(roles, old_roles, path.name)
    shoulders = [faces for name, faces in roles.items() if name.startswith("ShoulderRock")]
    coverage = audit_shoulder_coverage(np.concatenate(shoulders) if shoulders else [], exposed, lip, path.name)
    report = {"file": path.relative_to(ROOT).as_posix(), "sha256": native.digest(path), "bottom": False,
              "square_top": square, "shape": row["shape"], "variant": row["variant"]+1,
              **source_report, **export_report, **materials, **retained, "closed_rock_roles": closed,
              "near_lip_coverage": coverage}
    return primitives, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--models", type=Path, default=native.DEFAULT_MODELS)
    parser.add_argument("--source-audit", type=Path, default=OUT/"regular-relief-source-audit.json")
    parser.add_argument("--accepted-root", type=Path, default=ACCEPTED)
    parser.add_argument("--contract", type=Path, default=native.DEFAULT_CONTRACT)
    parser.add_argument("--output", type=Path, default=OUT/"regular-relief-validation.json")
    parser.add_argument("--native-source-audit", type=Path, default=native.DEFAULT_SOURCE_AUDIT)
    parser.add_argument("--square-source-audit", type=Path, default=native.DEFAULT_SQUARE_SOURCE_AUDIT)
    args = parser.parse_args()
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    recess_factor = contract.get("recessMaterialBaseColorFactor")
    native.require(("recessMaterialBaseColorFactor" not in contract and "recessMaterialRevision" not in contract)
                   or (recess_factor == RECESS_BASE_COLOR and contract.get("recessMaterialRevision") == 1),
                   "Unexpected explicit recess material revision/color contract")
    native.require(contract.get("regularDesignRevision") == 3 and contract.get("regularReliefRevision") == 1
                   and contract.get("bottomDesignRevision") == 5 and contract.get("shoulderTransitionRevision") == 1
                   and contract.get("regularReliefMaximumDepth") == MAXIMUM_DEPTH,
                   "Unexpected explicit regular-relief/accepted-bottom contract")
    before = json.loads((OUT/"shoulder-transition-validation.json").read_text(encoding="utf-8"))
    native.require(before.get("status") == "PASS" and before.get("regular_design_revision") == 2
                   and before.get("bottom_revision") == 5, "Missing accepted shoulder-transition proof")
    source_path = args.accepted_root/"source-audit.json"
    native.require(native.digest(source_path) == before["source_snapshot_sha256"], "Frozen accepted native snapshot changed")
    old_rows = json.loads(source_path.read_text(encoding="utf-8"))
    rows = json.loads(args.source_audit.read_text(encoding="utf-8"))
    key = lambda row: (bool(row["bottom"]), bool(row["squareTop"]), row["shape"], row["variant"])
    accepted, current = {key(row): row for row in old_rows}, {key(row): row for row in rows}
    native.require(len(old_rows) == len(rows) == len(accepted) == len(current) == 240 and accepted.keys() == current.keys(),
                   "Incomplete regular-relief source kit")
    old_hashes = {Path(row["file"]).name: row["sha256"] for row in before["models"]}
    native.require(len(old_hashes) == 240, "Incomplete accepted GLB hash proof")
    protected_path = args.accepted_root/"protected-hashes.json"
    protected = json.loads(protected_path.read_text(encoding="utf-8"))
    bottom_names = {f"assets/custom/models/ground_modules/{name}" for name in old_hashes if name.startswith("ground_bottom_")}
    original = json.loads((OUT/"original-ground-hashes.json").read_text(encoding="utf-8"))
    for name, expected in original.items():
        native.require(native.digest(ROOT/name) == expected, f"Historical legacy/PBR file changed: {name}")
    minimum = set(bottom_names)
    minimum.update((args.contract.parent/contract[name]).relative_to(ROOT).as_posix() for name in ("capSurfaces", "squareCapSurfaces"))
    native.require(minimum <= set(protected), "Protected reference omits accepted bottoms, caps or PBR files")
    for name, expected in protected.items():
        if name in bottom_names:
            native.require(expected == old_hashes[Path(name).name], f"Protected bottom reference was rewritten: {name}")
        if name not in bottom_names or recess_factor is None:
            native.require(native.digest(ROOT/name) == expected, f"Protected file changed: {name}")
        # Revised bottoms are checked against the frozen accepted reference
        # below: only the declared color may differ, with all binary bytes fixed.
    reference_protection = audit_reference_protection(before, protected, args.models)
    loaded, reports = {False: {}, True: {}}, []
    for identity, row in current.items():
        bottom, square, family, variant = identity
        native.require(row["exposedSideMask"] == dict(rock.FAMILIES)[family] and 0 <= variant < 10,
                       "Invalid regular-relief family/style metadata")
        name = f"ground_{'bottom_' if bottom else ''}{'square_' if square else ''}{family}_{variant+1:02d}.glb"
        path, reference = args.models/name, args.accepted_root/"models"/name
        native.require(native.digest(reference) == old_hashes[name], f"Frozen accepted GLB changed: {name}")
        primitives, report = audit_body(path, reference, row, accepted[identity], recess_factor)
        if not bottom:
            loaded[square][(family, variant)] = primitives
        reports.append(report)
        print(f"PASS regular relief body {name}", flush=True)
    caps, cap_report = native.read_native_caps(args.contract.parent/contract["capSurfaces"], contract)
    square_caps, square_report = native.read_square_caps(args.contract.parent/contract["squareCapSurfaces"], contract)
    joins = native.audit_caps_v2(caps, contract)
    closure = native.audit_body_cap_closure_v2(loaded[False], caps, contract)
    contacts = native.audit_square_contacts(loaded[False], loaded[True], square_caps, contract)
    native_source = native.audit_native_source_v2(args.native_source_audit, args.contract, contract, loaded[False], caps)
    square_contract = {**contract, "outerLipHeight": 1, "lipDepth": 0, "preservationLipDepth": contract["lipDepth"]}
    square_source = native.audit_native_source_v2(args.square_source_audit, args.contract, square_contract, loaded[True], square_caps)
    selected = [report for report in reports if report.get("selected_faces")]
    result = {"status": "PASS", "regular_design_revision": 3, "regular_relief_revision": 1, "bottom_revision": 5,
              "recess_material_revision": contract.get("recessMaterialRevision"), "recess_base_color_factor": recess_factor,
              "native_models": 240, "modified_regular_models": len(selected), "unchanged_bottom_geometry_models": 120,
              "unchanged_regular_interior_models": 20, "selected_rock_faces": sum(r["selected_faces"] for r in reports),
              "maximum_relief_depth": max(r["maximum_relief_depth"] for r in selected),
              "source_snapshot_sha256": native.digest(args.source_audit), "accepted_source_snapshot_sha256": native.digest(source_path),
              "protected_reference_sha256": native.digest(protected_path), "protected_files": len(protected),
              "historical_legacy_and_shared_pbr_files": len(original),
              "historical_baseline_and_pbr_protection": reference_protection,
              "native_caps": cap_report, "square_native_caps": square_report, "caps_and_joins": joins,
              "body_cap_closure": closure, "layer_contacts": contacts, "native_source": native_source,
              "square_native_source": square_source, "models": reports}
    native.write_json(args.output, result)
    print(json.dumps({k: v for k, v in result.items() if k != "models"}))


if __name__ == "__main__":
    main()
