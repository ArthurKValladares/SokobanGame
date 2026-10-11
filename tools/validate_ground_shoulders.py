#!/usr/bin/env python3
"""Independently audit the explicit collar-free ground transition revision.

Legacy regular-v1 and bottom-v4 validators remain unchanged. This revision
protects every accepted boulder/floater, the retained core, lip seals, caps and
PBR, while allowing only the old outer collar to be replaced by new shoulders.
"""
from __future__ import annotations

import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image

import validate_ground_modules as native
import validate_ground_bottoms as rock
import validate_regular_ground as regular
from ground_shoulder_coverage import audit_shoulder_coverage
from pbr_gltf import Document

ROOT = native.ROOT
OUT = ROOT / "out/ground-modules"
ACCEPTED = OUT / "pre-shoulder-transition-20261011-175324"
MINIMUM_Z = .40
COLLAR_Z = .78
TOLERANCE = native.TOLERANCE
ROCK_ROLE = re.compile(r"(?:BoulderMass|FloatingRock)\d+")
SHOULDER_ROLE = re.compile(r"ShoulderRock\d+")


def triangle_counter(faces):
    return Counter(rock.triangle_key(face) for face in faces)


def exact_triangle_counter(faces):
    def key(face):
        corners = [tuple(float(value) for value in point) for point in face]
        return min(tuple(corners[offset:]+corners[:offset]) for offset in range(3))
    return Counter(key(face) for face in faces)


def audit_contract(contract):
    native.require(contract.get("format") == 2 and contract.get("nativeCapCount") == 250
                   and contract.get("squareNativeCapCount") == 60
                   and contract.get("regularDesignRevision") == 2 and contract.get("bottomDesignRevision") == 5
                   and contract.get("shoulderTransitionRevision") == 1
                   and contract.get("bottomPreserveUpperBandGeometry") is False,
                   "Unexpected explicit shoulder transition/cap revision contract")
    for name, expected in (("shoulderTransitionMinimumZ", MINIMUM_Z), ("shoulderCollarRemovalMinimumZ", COLLAR_Z),
                           ("shoulderNearLipCoverageMinimum", .8), ("outerLipHeight", .91),
                           ("squareOuterLipHeight", 1), ("bodyRecessInset", .055),
                           ("bottomMaximumExposedSideOverhang", .04), ("bottomHiddenConnectionHeight", -.34)):
        native.require(abs(contract.get(name, -100)-expected) < TOLERANCE,
                       f"Unexpected shoulder transition contract value: {name}")
    native.require(contract.get("shoulderRoofClearance") == "0.001 + 0.0006 * zero-based shoulder index",
                   "Unexpected shoulder roof-clearance formula")


def source_roles(body, bottom, label):
    points = np.asarray(body["points"], dtype=float)
    polygons = np.asarray(body["polygons"], dtype=np.int64)
    native.require(points.shape == (body["vertices"], 3) and np.isfinite(points).all()
                   and polygons.shape == (body["faces"], 3)
                   and np.all(polygons >= 0) and np.all(polygons < len(points)),
                   f"{label}: invalid native triangulated shoulder geometry")
    groups = body["faceGroups"]
    core = "IslandCore" if bottom else "RegularCore"
    native.require(core in groups, f"{label}: missing native core ownership")
    for name, indices in groups.items():
        native.require(len(indices) == len(set(indices)) and all(0 <= index < len(polygons) for index in indices),
                       f"{label}: invalid {name} face ownership")
        native.require(name in body["vertexGroups"]
                       and {int(vertex) for index in indices for vertex in polygons[index]}
                       <= set(body["vertexGroups"][name]), f"{label}: incomplete editable {name} selection")
    primary = [core]+[name for name in groups if ROCK_ROLE.fullmatch(name) or SHOULDER_ROLE.fullmatch(name)]
    coverage = Counter(index for name in primary for index in groups[name])
    native.require(set(coverage) == set(range(len(polygons))) and all(value == 1 for value in coverage.values()),
                   f"{label}: missing or overlapping primary shoulder roles")
    used = np.unique(polygons)
    native.require(np.allclose(points[used].min(0), body["bounds"]["min"], atol=TOLERANCE, rtol=0)
                   and np.allclose(points[used].max(0), body["bounds"]["max"], atol=TOLERANCE, rtol=0),
                   f"{label}: actual used geometry differs from declared native bounds")
    return {name: points[polygons[indices]] for name, indices in groups.items()}


def removable_collar_faces(body, old_primitives, lip_height, bottom):
    """Only old outer core faces fully above .78 may disappear.

    Recessed shared backings and horizontal lip support remain protected even
    if their triangles also lie wholly above the old cut height.
    """
    faces = np.asarray(body["points"])[np.asarray(body["polygons"])]
    core = body["faceGroups"]["IslandCore" if bottom else "RegularCore"]
    backing = {rock.triangle_key(face) for primitive in old_primitives
               if primitive.material.get("name") == "GroundModuleRecess"
               for face in primitive.points[primitive.triangles]}
    removed = {index for index in core if faces[index, :, 2].min() >= COLLAR_Z-TOLERANCE
               and not np.all(np.abs(faces[index, :, 2]-lip_height) < TOLERANCE)
               and rock.triangle_key(faces[index]) not in backing}
    return faces, set(core)-removed, removed


def face_signatures(primitives, streams, selected=None):
    """Exact oriented corner positions, normals, material and every UV set."""
    result = Counter()
    for primitive, coordinates in zip(primitives, streams):
        for indices in primitive.triangles:
            face = primitive.points[indices]
            if selected is not None and not selected(face, primitive.material):
                continue
            corners = [(tuple(float(value) for value in primitive.points[index]),
                        tuple(float(value) for value in primitive.normals[index]),
                        tuple((name, tuple(float(value) for value in values[index]))
                              for name, values in sorted(coordinates.items()))) for index in indices]
            canonical = min(tuple(corners[offset:]+corners[:offset]) for offset in range(3))
            result[(primitive.material.get("name", ""), canonical)] += 1
    return result


def material_signature(document, material):
    """Protect image bindings and sampler state as well as PBR parameters.

    Texture/sampler array indices may change when Blender exports the new
    shoulders. Resolve those indices without dropping wrap or filter settings.
    Unspecified filters remain unspecified; glTF supplies REPEAT wrap defaults.
    """
    value = copy.deepcopy(material)
    for owner, name in ((value.get("pbrMetallicRoughness", {}), "baseColorTexture"),
                        (value.get("pbrMetallicRoughness", {}), "metallicRoughnessTexture"),
                        (value, "normalTexture"), (value, "occlusionTexture"), (value, "emissiveTexture")):
        if name not in owner:
            continue
        info = owner[name]
        texture = copy.deepcopy(document.data["textures"][info["index"]])
        sampler = copy.deepcopy(document.data.get("samplers", [])[texture["sampler"]]) if "sampler" in texture else {}
        sampler.setdefault("wrapS", 10497)
        sampler.setdefault("wrapT", 10497)
        texture["sampler"] = sampler
        texture["source"] = native.texture_path(document, info["index"]).relative_to(native.ASSETS).as_posix()
        info["index"] = texture
    return value


def require_original_material(document, material, originals, label):
    name = material.get("name")
    native.require(name in originals and material_signature(document, material) == originals[name],
                   f"{label}: original PBR material, texture or sampler changed")


def audit_preservation(primitives, old, streams, old_streams, body, accepted, bottom, lip_height, label):
    current_roles, old_roles = source_roles(body, bottom, label), source_roles(accepted, bottom, f"Accepted {label}")
    core = "IslandCore" if bottom else "RegularCore"
    old_faces, retained, removed = removable_collar_faces(accepted, old, lip_height, bottom)
    native.require(exact_triangle_counter(current_roles[core]) == exact_triangle_counter(old_faces[sorted(retained)]),
                   f"{label}: protected core changed or old outer collar remains")
    old_names = {name for name in old_roles if ROCK_ROLE.fullmatch(name)}
    names = {name for name in current_roles if ROCK_ROLE.fullmatch(name)}
    native.require(names == old_names, f"{label}: accepted boulder/floater roles changed")
    for name in names:
        native.require(exact_triangle_counter(current_roles[name]) == exact_triangle_counter(old_roles[name]),
                       f"{label}: accepted {name} surfaces or topology changed")
    shoulder_faces = [faces for name, faces in current_roles.items() if SHOULDER_ROLE.fullmatch(name)]
    shoulder_faces = np.concatenate(shoulder_faces) if shoulder_faces else np.empty((0, 3, 3))
    shoulder_keys = set(triangle_counter(shoulder_faces))
    removed_keys = set(triangle_counter(old_faces[sorted(removed)]))
    protected_keys = set(triangle_counter(old_faces[sorted(retained)]))
    protected_keys.update(key for name in old_names for key in triangle_counter(old_roles[name]))
    native.require(not shoulder_keys & protected_keys and not removed_keys & protected_keys,
                   f"{label}: new shoulder duplicates a protected surface or collar ownership is ambiguous")
    actual_faces = np.concatenate([primitive.points[primitive.triangles] for primitive in primitives])
    native.require(triangle_counter(actual_faces)
                   == triangle_counter(np.asarray(body["points"])[np.asarray(body["polygons"])]),
                   f"{label}: exported triangles differ from editable native roles")
    expected = face_signatures(old, old_streams, lambda face, _: rock.triangle_key(face) not in removed_keys)
    actual = face_signatures(primitives, streams, lambda face, _: rock.triangle_key(face) not in shoulder_keys)
    native.require(actual == expected, f"{label}: protected positions/normals/material/all-UV corner streams changed")
    native.require(len(actual_faces) == sum(expected.values())+len(shoulder_faces),
                   f"{label}: extra geometry exists outside preserved surfaces and shoulders")
    return current_roles, {"removed_outer_collar_triangles": len(removed),
                           "protected_export_triangles": sum(expected.values()),
                           "all_accepted_rock_surfaces_topology_and_uv_preserved": True,
                           "retained_core_lip_seals_positions_normals_material_and_all_uv_identical": True,
                           "source_export_triangle_ownership_identical": True}


def audit_shoulder_geometry(roles, bottom, exposed, lip_height, label):
    shoulders = {name: rock.convex_rock(faces, f"{label} {name}")
                 for name, faces in roles.items() if SHOULDER_ROLE.fullmatch(name)}
    native.require(bool(shoulders) if exposed else not shoulders, f"{label}: incorrect exposed/interior shoulder topology")
    accepted = {name: rock.convex_rock(faces, f"{label} {name}") for name, faces in roles.items()
                if re.fullmatch(r"BoulderMass\d+", name)}
    core = roles["IslandCore" if bottom else "RegularCore"]
    for name, shoulder in shoulders.items():
        index = int(name[len("ShoulderRock"):])-1
        roof = lip_height-(.001+.0006*index)
        points = shoulder["points"]
        native.require(index >= 0 and points[:, 2].min() >= MINIMUM_Z-TOLERANCE
                       and abs(points[:, 2].max()-roof) < TOLERANCE,
                       f"{label} {name}: shoulder leaves its agreed vertical clearance")
        allowance = np.minimum(1, np.maximum(0, (lip_height-points[:, 2])/(lip_height-COLLAR_Z)))*(.04 if bottom else 0)
        minimum = np.column_stack((-allowance if exposed & 8 else np.zeros(len(points)),
                                   -allowance if exposed & 1 else np.zeros(len(points))))
        maximum = np.column_stack((1+allowance if exposed & 2 else np.ones(len(points)),
                                   1+allowance if exposed & 4 else np.ones(len(points))))
        native.require(np.all(points[:, :2] >= minimum-TOLERANCE) and np.all(points[:, :2] <= maximum+TOLERANCE),
                       f"{label} {name}: shoulder leaves the exposed-only tapered footprint")
        roof_points = points[points[:, 2] >= roof-TOLERANCE]
        native.require(np.all(roof_points[:, :2] >= -TOLERANCE) and np.all(roof_points[:, :2] <= 1+TOLERANCE),
                       f"{label} {name}: shoulder roof overhangs the exact cap footprint")
    roots = {name for name, shoulder in shoulders.items()
             if rock.core_intersection(shoulder, core)
             or any(rock.convex_gap(shoulder, original) < -TOLERANCE for original in accepted.values())}
    graph = {name: set() for name in shoulders}
    names = list(shoulders)
    for index, name in enumerate(names):
        for other in names[index+1:]:
            if rock.convex_gap(shoulders[name], shoulders[other]) < -TOLERANCE:
                graph[name].add(other)
                graph[other].add(name)
    reached, pending = set(), list(roots)
    while pending:
        name = pending.pop()
        if name not in reached:
            reached.add(name)
            pending.extend(graph[name]-reached)
    native.require(reached == set(shoulders), f"{label}: new shoulder is not rooted in accepted rock/core geometry")
    minimum_gap = None
    for name, faces in roles.items():
        if not re.fullmatch(r"FloatingRock\d+", name):
            continue
        floating = rock.convex_rock(faces, f"{label} {name}")
        for other, shoulder in shoulders.items():
            native.require(rock.convex_gap(floating, shoulder) > 0,
                           f"{label}: new shoulder intersects an accepted floating rock")
            gap = rock.mesh_surface_distance(faces, shoulder["faces"])
            native.require(gap >= rock.ROCK_GAP, f"{label}: new shoulder closes an accepted floating-rock gap")
            minimum_gap = gap if minimum_gap is None else min(minimum_gap, gap)
    faces = np.concatenate([value["faces"] for value in shoulders.values()]) if shoulders else []
    coverage = audit_shoulder_coverage(faces, exposed, lip_height, label, max_overhang=.04 if bottom else 0)
    return {"shoulder_rocks": len(shoulders), "directly_rooted_shoulders": len(roots), "all_shoulders_rooted": True,
            "minimum_new_shoulder_to_floater_gap": minimum_gap, "near_lip_coverage": coverage,
            "shoulders": {name: {key: value[key] for key in ("volume", "principal_axis_ratio", "facets")}
                          for name, value in shoulders.items()}}


def audit_body(path, reference, body, accepted, bottom, square, exposed, variant, family):
    lip = 1 if square else .91
    document, old_document = Document(path), Document(reference)
    primitives, old = native.read_primitives(document), native.read_primitives(old_document)
    old_document.path = path
    roles, preservation = audit_preservation(primitives, old, rock.texture_coordinates(document),
                                             rock.texture_coordinates(old_document), body, accepted, bottom, lip, path.name)
    core_faces = roles["IslandCore" if bottom else "RegularCore"]
    lip_faces = core_faces[np.all(np.abs(core_faces[:, :, 2]-lip) < TOLERANCE, axis=1)]
    native.require(triangle_counter(roles.get("CapLipSeal", [])) == triangle_counter(lip_faces),
                   f"{path.name}: editable CapLipSeal does not select the complete preserved horizontal seal")
    geometry = audit_shoulder_geometry(roles, bottom, exposed, lip, path.name)
    properties = body["properties"]
    native.require(properties.get("BottomRevision" if bottom else "RegularDesignRevision") == (5 if bottom else 2)
                   and properties.get("ShoulderTransitionRevision") == 1
                   and abs(properties.get("ShoulderTransitionMinimumZ", -1)-MINIMUM_Z) < TOLERANCE
                   and properties.get("ShoulderRockCount") == geometry["shoulder_rocks"]
                   and properties.get("GroundModuleVariant") == variant
                   and properties.get("GroundModuleShape") == family
                   and properties.get("ExposedSideMask") == exposed,
                   f"{path.name}: missing or incorrect explicit transition/source identity metadata")
    original_materials = {material["name"]: material_signature(old_document, material)
                          for material in old_document.data["materials"]}
    for primitive in primitives:
        require_original_material(document, primitive.material, original_materials, path.name)
        if primitive.material.get("name") != "GroundModuleRecess":
            native.require(set(native.material_maps(document, primitive.material)) == {"albedo", "normal", "orm", "ao"},
                           f"{path.name}: rock transition lacks full original PBR bindings")
    for item in document.data.get("images", []):
        native.require("uri" in item and "bufferView" not in item, f"{path.name}: unfinalized transition image")
        resource = native.resource_path(path.parent, item["uri"])
        native.require(resource.is_relative_to(native.ASSETS) and resource.suffix.lower() == ".png", "Invalid shared PBR dependency")
        with Image.open(resource) as decoded:
            decoded.verify()
    points = np.concatenate([primitive.points for primitive in primitives])
    native.require(points[:, 2].max() <= lip+TOLERANCE and points[:, 2].min() >= (-1 if bottom else 0)-TOLERANCE,
                   f"{path.name}: transition exceeds existing vertical runtime bounds")
    allowance = .04 if bottom else 0
    minimum = np.asarray([-allowance if exposed & 8 else 0, -allowance if exposed & 1 else 0])
    maximum = np.asarray([1+allowance if exposed & 2 else 1, 1+allowance if exposed & 4 else 1])
    native.require(np.all(points[:, :2] >= minimum-TOLERANCE) and np.all(points[:, :2] <= maximum+TOLERANCE),
                   f"{path.name}: revised complete body exceeds its exposed/hidden runtime footprint")
    if not bottom:
        regular.audit_retained_geometry(primitives, old, rock.texture_coordinates(document), rock.texture_coordinates(old_document), path.name)
    report = {**preservation, **geometry, "file": path.relative_to(ROOT).as_posix(), "sha256": native.digest(path),
              "accepted_sha256": native.digest(reference), "bottom": bottom, "square_top": square,
              "variant": variant+1, "shape": family, "exposed_mask": exposed, "bytes": path.stat().st_size}
    return primitives, roles, report


def load_accepted(accepted_root):
    baseline = json.loads((accepted_root/"baseline-sha256.json").read_text(encoding="utf-8"))
    native.require(baseline["status"] == "PASS" and baseline["bodyModels"] == 240
                   and baseline["sourceStableDuringCopy"] is True, "Invalid frozen accepted-kit backup")
    accepted_regular = json.loads((OUT/"regular-chunky-validation.json").read_text(encoding="utf-8"))
    accepted_bottom = json.loads((OUT/"bottom-v4-after-regular-validation.json").read_text(encoding="utf-8"))
    native.require(accepted_regular["status"] == accepted_bottom["status"] == "PASS", "Accepted reports did not pass")
    expected = {row["file"]: row["sha256"] for row in accepted_regular["models"]
                +accepted_regular["square_modules"]["models"]+accepted_bottom["models"]}
    model_files = {Path(row["source"]).relative_to(ROOT).as_posix(): row for row in baseline["files"]
                   if Path(row["source"]).suffix.lower() == ".glb"}
    native.require(set(model_files) == set(expected) and len(expected) == 240, "Incomplete frozen accepted model coverage")
    for name, row in model_files.items():
        native.require(native.digest(accepted_root/row["backup"]) == row["sha256"] == expected[name],
                       f"Frozen accepted model changed: {name}")
    for row in baseline["files"]:
        native.require(native.digest(accepted_root/row["backup"]) == row["sha256"],
                       f"Frozen accepted source/model backup changed: {row['backup']}")
    snapshots = {}
    for name, expected_hash, bottom in (("regular-chunky-source-audit.json", accepted_regular["regular_source_snapshot_sha256"], False),
                                       ("bottom-source-audit.json", accepted_bottom["native_source"]["source_snapshot_sha256"], True)):
        path = accepted_root/name
        native.require(native.digest(path) == expected_hash, f"Frozen native role snapshot changed: {name}")
        rows = json.loads(path.read_text(encoding="utf-8"))
        sources = {(bottom, bool(row["squareTop"]), row["shape"], row["variant"]): row for row in rows}
        native.require(len(rows) == len(sources) == 120, "Incomplete accepted role snapshot")
        snapshots.update(sources)
    return snapshots, model_files, accepted_regular


def audit_historical_baselines(accepted_root, model_files, accepted_regular):
    """Prove old reference values remain historical rather than rebaselined."""
    names = ("original-ground-hashes.json", "original-module-hashes.json", "bottom-original-kit-hashes.json",
             "pre-regular-chunky-regular-hashes.json", "pre-regular-chunky-protected-hashes.json",
             "regular-chunky-approved-bottom-hashes.json")
    legacy, modules, kit, regular_old, protected, approved = [
        json.loads((OUT/name).read_text(encoding="utf-8")) for name in names]
    native.require([len(value) for value in (legacy, modules, kit, regular_old, protected, approved)]
                   == [16, 61, 122, 120, 138, 122], "Incomplete historical reference baselines")
    native.require(all(kit.get(name) == value for name, value in modules.items())
                   and all(kit.get(name) == value for name, value in regular_old.items())
                   and all(protected.get(name) == value for name, value in legacy.items()),
                   "Original historical reference values were rewritten")
    cap_names = set(kit)-set(regular_old)
    native.require(len(cap_names) == 2 and all(protected.get(name) == kit[name] == approved.get(name)
                                              for name in cap_names), "Historical cap references changed")
    for name, expected in regular_old.items():
        native.require(native.digest(OUT/"pre-regular-chunky-models"/Path(name).name) == expected,
                       f"Historical original reference no longer matches its frozen model: {name}")
    for name, expected in legacy.items():
        native.require(native.digest(ROOT/name) == expected, f"Historical original/PBR file changed: {name}")
    expected_approved = {name: row["sha256"] for name, row in model_files.items()
                         if not Path(name).name.startswith("ground_bottom_")}
    expected_approved.update({name: kit[name] for name in cap_names})
    native.require(approved == expected_approved
                   and native.digest(OUT/names[-1]) == accepted_regular["approved_bottom_reference_baseline"]["sha256"],
                   "Approved regular reference no longer matches its accepted frozen kit")
    unchanged = ["tools/validate_ground_modules.py", "tools/validate_ground_bottoms.py", "tools/validate_regular_ground.py"]
    diff = subprocess.run(["git", "diff", "--exit-code", "HEAD", "--", *unchanged], cwd=ROOT,
                          capture_output=True, text=True)
    native.require(diff.returncode == 0, "Historical validators differ from the unchanged repository versions")
    return {"original_61_and_122_entry_baselines_match_frozen_original_models": True,
            "original_16_legacy_and_pbr_hashes_match_protected_files": True,
            "approved_122_entry_baseline_matches_accepted_sha256_and_frozen_models": True,
            "historical_validators_unchanged_in_git": True,
            "baseline_files_sha256": {name: native.digest(OUT/name) for name in names},
            "historical_validator_files_sha256": {name: native.digest(ROOT/name) for name in unchanged}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--models", type=Path, default=native.DEFAULT_MODELS)
    parser.add_argument("--contract", type=Path, default=native.DEFAULT_CONTRACT)
    parser.add_argument("--source-audit", type=Path, default=OUT/"shoulder-source-audit.json")
    parser.add_argument("--accepted-root", type=Path, default=ACCEPTED)
    parser.add_argument("--output", type=Path, default=OUT/"shoulder-transition-validation.json")
    parser.add_argument("--native-source-audit", type=Path, default=native.DEFAULT_SOURCE_AUDIT)
    parser.add_argument("--square-source-audit", type=Path, default=native.DEFAULT_SQUARE_SOURCE_AUDIT)
    args = parser.parse_args()
    args.models, args.contract, args.accepted_root = args.models.absolute(), args.contract.absolute(), args.accepted_root.absolute()
    accepted, backups, accepted_regular = load_accepted(args.accepted_root)
    historical_baselines = audit_historical_baselines(args.accepted_root, backups, accepted_regular)
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    audit_contract(contract)
    protected = json.loads((OUT/"original-ground-hashes.json").read_text(encoding="utf-8"))
    for key, digest_key, owner in (("capSurfaces", "cap_surfaces_sha256", accepted_regular),
                                    ("squareCapSurfaces", "cap_surfaces_sha256", accepted_regular["square_modules"])):
        name = (args.contract.parent/contract[key]).relative_to(ROOT).as_posix()
        protected[name] = owner[digest_key]
    for name, expected in protected.items():
        native.require(native.digest(ROOT/name) == expected, f"Protected caps/legacy/PBR changed: {name}")
    rows = json.loads(args.source_audit.read_text(encoding="utf-8"))
    sources = {(bool(row["bottom"]), bool(row["squareTop"]), row["shape"], row["variant"]): row for row in rows}
    native.require(len(rows) == len(sources) == 240 and set(sources) == set(accepted), "Incomplete explicit transition source snapshot")
    loaded, bottom_loaded, reports = {False: {}, True: {}}, {}, []
    for key, row in sources.items():
        bottom, square, family, variant = key
        mask = dict(rock.FAMILIES)[family]
        native.require(row["exposedSideMask"] == mask, "Native transition exposure metadata differs")
        name = f"ground_{'bottom_' if bottom else ''}{'square_' if square else ''}{family}_{variant+1:02}.glb"
        path = args.models/name
        old = args.accepted_root/backups[path.relative_to(ROOT).as_posix()]["backup"]
        primitives, roles, report = audit_body(path, old, row["body"], accepted[key]["body"], bottom, square, mask, variant, family)
        if bottom:
            floor, boundary, _ = rock.floor_disk(roles["IslandUnderside"], path.name)
            rock.audit_core_floor_attachment(row["body"], boundary, mask, path.name)
            for side in range(4):
                if not mask & (1 << side):
                    rock.hidden_floor_trace(boundary, side, path.name)
            bottom_loaded[(square, family, variant)] = {"floor_boundary": boundary, "primitives": primitives}
        else:
            loaded[square][(family, variant)] = primitives
        reports.append(report)
        print(f"PASS transition body {name}", flush=True)
    for (square, family, variant), value in bottom_loaded.items():
        value["regular_primitives"] = loaded[square][(family, variant)]
    caps, cap_report = native.read_native_caps(args.contract.parent/contract["capSurfaces"], contract)
    square_caps, square_cap_report = native.read_square_caps(args.contract.parent/contract["squareCapSurfaces"], contract)
    joins = native.audit_caps_v2(caps, contract)
    closure = native.audit_body_cap_closure_v2(loaded[False], caps, contract)
    contacts = native.audit_square_contacts(loaded[False], loaded[True], square_caps, contract)
    bottom_joins = rock.audit_rotated_joins(bottom_loaded)
    native_source = native.audit_native_source_v2(args.native_source_audit, args.contract, contract, loaded[False], caps)
    square_contract = {**contract, "outerLipHeight": 1, "lipDepth": 0, "preservationLipDepth": contract["lipDepth"]}
    square_source = native.audit_native_source_v2(args.square_source_audit, args.contract, square_contract, loaded[True], square_caps)
    result = {"status": "PASS", "shoulder_transition_revision": 1, "regular_design_revision": 2, "bottom_revision": 5,
              "native_models": 240, "frozen_accepted_models": 240, "protected_cap_legacy_and_pbr_files": len(protected),
              "source_snapshot_sha256": native.digest(args.source_audit),
              "accepted_baseline_sha256": native.digest(args.accepted_root/"baseline-sha256.json"),
              "native_caps": cap_report, "square_native_caps": square_cap_report,
              "caps_and_joins": joins, "body_cap_closure": closure, "layer_contacts": contacts,
              "bottom_connected_and_supported_joins": bottom_joins,
              "native_source": native_source, "square_native_source": square_source, "models": reports,
              "historical_baseline_and_validator_proof": historical_baselines,
              "legacy_validators_and_original_baselines_unchanged": True}
    native.write_json(args.output, result)
    print(json.dumps({key: value for key, value in result.items() if key != "models"}))


if __name__ == "__main__":
    main()
