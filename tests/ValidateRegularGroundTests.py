"""Destructive geometry fixtures for the explicit chunky regular revision."""
from __future__ import annotations

import copy
from pathlib import Path
import subprocess
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_regular_ground as audit
from ValidateGroundBottomsTests import primitive, box_faces


def fixture(exposed=15):
    parts = {"FlatBase": box_faces([0, 0, 0], [1, 1, 1])[:2], "RecessBacking": []}
    for side in range(4):
        if not exposed & (1 << side):
            continue
        a, b = [audit.native.side_point(side, u, .055, 0) for u in (0, 1)]
        c, d = [audit.native.side_point(side, u, .055, .91) for u in (0, 1)]
        parts["RecessBacking"].extend(([a, b, d], [a, d, c]))
    parts["RecessBacking"] = np.asarray(parts["RecessBacking"]).reshape(-1, 3, 3)
    if exposed:
        parts["UpperBand"] = np.asarray([[[0, 0, .78], [1, 0, .78], [1, 0, .91]],
                                        [[0, 0, .78], [1, 0, .91], [0, 0, .91]]])
        parts["BoulderMass01"] = box_faces([.1, .02, .08], [.45, .25, .70])
        parts["BoulderMass02"] = box_faces([.55, .02, .1], [.9, .25, .72])
    return parts


def assemble(parts):
    primitives, points, polygons = [], [], []
    groups, core = {}, []
    for name, faces in parts.items():
        indices = list(range(len(polygons), len(polygons)+len(faces)))
        if not name.startswith("BoulderMass"):
            core.extend(indices)
        if name != "UpperBand" and name != "OldPlate":
            groups[name] = indices
        if not len(faces):
            continue
        mesh = primitive(faces, {"name": "GroundModuleRecess" if name == "RecessBacking" else "stone"})
        primitives.append(mesh)
        polygons.extend((mesh.triangles+len(points)).tolist())
        points.extend(mesh.points.tolist())
    groups["RegularCore"] = core
    coordinates = np.asarray(points)
    properties = {"RegularDesignRevision": 1, "RegularRemodelHeight": .78,
                  "RegularRockMinimumZ": .02, "RegularRockMaximumZ": .777,
                  "RegularBoulderMassCount": sum(name.startswith("BoulderMass") for name in parts)}
    body = {"points": points, "polygons": polygons, "vertices": len(points), "faces": len(polygons),
            "faceGroups": groups, "vertexGroups": {name: sorted({vertex for index in indices for vertex in polygons[index]})
                                                      for name, indices in groups.items()},
            "bounds": {"min": coordinates.min(0).tolist(), "max": coordinates.max(0).tolist()}, "properties": properties}
    return primitives, body


def run_geometry(parts, exposed=15):
    primitives, body = assemble(parts)
    return audit.audit_regular_geometry(primitives, body, exposed, .91, "fixture")


def streams(primitives):
    return [{"TEXCOORD_0": mesh.uvs.copy(), "TEXCOORD_1": mesh.uvs.copy()*2+.3} for mesh in primitives]


class RegularGroundAuditTests(unittest.TestCase):
    def test_protected_files_cannot_be_waived_by_an_unrelated_hash_manifest(self):
        original = {"legacy.glb": "legacy", "pbr.png": "pbr"}
        prekit = {"regular.glb": "regular", "cap.json": "cap"}
        bottom = {"status": "PASS", "native_source": {"bottom_revision": 4}, "native_models": 1,
                  "models": [{"file": "bottom.glb", "sha256": "bottom"}]}
        references = {"regular.glb": "regular"}
        protected = {**original, "cap.json": "cap", "bottom.glb": "bottom"}
        audit.verify_protection_manifests(protected, original, prekit, bottom, references, ["regular.glb"], ["cap.json"])
        for name in ("legacy.glb", "pbr.png", "cap.json", "bottom.glb"):
            broken = {**protected, name: "modified"}
            with self.assertRaisesRegex(AssertionError, "Protected hashes do not exactly cover"):
                audit.verify_protection_manifests(broken, original, prekit, bottom, references, ["regular.glb"], ["cap.json"])
        with self.assertRaisesRegex(AssertionError, "references do not match"):
            audit.verify_protection_manifests(protected, original, prekit, bottom, {"regular.glb": "modified"}, ["regular.glb"], ["cap.json"])
        with self.assertRaisesRegex(AssertionError, "passing V4 audit"):
            audit.verify_protection_manifests(protected, original, prekit, {**bottom, "status": "FAIL"}, references, ["regular.glb"], ["cap.json"])

    def test_explicit_revision_is_required_and_unknown_revision_rejected(self):
        # An approval flag cannot waive the legacy path, even before any asset
        # access. Argparse also rejects unsupported revision names.
        command = [sys.executable, str(ROOT / "tools/validate_ground_modules.py")]
        result = subprocess.run(command+["--approve-bottom-baseline", "out/should-not-exist.json"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires the explicit chunky-v1 audit", result.stderr)
        result = subprocess.run(command+["--regular-revision", "future"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid choice", result.stderr)

    def test_closed_regular_boulders_attach_to_retained_open_backing(self):
        report = run_geometry(fixture())
        self.assertEqual(report["boulder_masses"], 2)
        self.assertEqual(report["directly_rooted_masses"], 2)
        self.assertTrue(report["all_masses_rooted"])

    def test_interior_remains_only_its_original_two_base_triangles(self):
        report = run_geometry(fixture(0), 0)
        self.assertEqual(report["boulder_masses"], 0)
        meshes, body = assemble(fixture(0))
        del body["faceGroups"]["RecessBacking"], body["vertexGroups"]["RecessBacking"]
        audit.audit_regular_geometry(meshes, body, 0, .91, "fixture")
        meshes, body = assemble(fixture())
        del body["faceGroups"]["RecessBacking"], body["vertexGroups"]["RecessBacking"]
        with self.assertRaisesRegex(AssertionError, "missing editable regular core/backing/base"):
            audit.audit_regular_geometry(meshes, body, 15, .91, "fixture")
        parts = fixture(0)
        parts["BoulderMass01"] = box_faces([.1, .02, .08], [.45, .25, .70])
        with self.assertRaisesRegex(AssertionError, "interior mass topology"):
            run_geometry(parts, 0)

    def test_square_unit_and_vertical_clearances_are_hard_limits(self):
        for low, high, message in (([-.001, .02, .08], [.45, .25, .7], "unit/lip envelope"),
                                   ([.1, .02, -.001], [.45, .25, .7], "unit/lip envelope"),
                                   ([.1, .02, .019], [.45, .25, .7], "base/top-band clearance"),
                                   ([.1, .02, .08], [.45, .25, .778], "base/top-band clearance")):
            parts = fixture()
            parts["BoulderMass01"] = box_faces(low, high)
            with self.assertRaisesRegex(AssertionError, message):
                run_geometry(parts)

    def test_old_lower_plates_cannot_remain_under_a_new_role_label(self):
        parts = fixture()
        parts["OldPlate"] = np.asarray([[[.2, .01, .1], [.4, .01, .2], [.3, .01, .5]]])
        with self.assertRaisesRegex(AssertionError, "old fractured lower plates"):
            run_geometry(parts)

    def test_nonclosed_concave_and_thin_masses_are_rejected(self):
        parts = fixture()
        parts["BoulderMass01"] = parts["BoulderMass01"][:-1]
        with self.assertRaisesRegex(AssertionError, "not closed"):
            run_geometry(parts)
        parts = fixture()
        faces = parts["BoulderMass01"]
        faces[np.all(np.isclose(faces, [.45, .25, .7]), axis=2)] = [.2, .1, .3]
        with self.assertRaisesRegex(AssertionError, "concave or wound inward"):
            run_geometry(parts)
        parts = fixture()
        parts["BoulderMass01"] = box_faces([.1, .02, .08], [.45, .025, .7])
        with self.assertRaisesRegex(AssertionError, "thin plate or stretched spike"):
            run_geometry(parts)

    def test_overlap_chains_root_boulders_but_touching_or_detached_masses_do_not(self):
        parts = fixture()
        parts["BoulderMass02"] = box_faces([.15, .2, .1], [.4, .45, .7])
        self.assertEqual(run_geometry(parts)["directly_rooted_masses"], 1)
        for low, high in (([.55, .5, .1], [.9, .8, .72]), ([.15, .25, .1], [.4, .5, .7])):
            parts["BoulderMass02"] = box_faces(low, high)
            with self.assertRaisesRegex(AssertionError, "not rooted in retained backing"):
                run_geometry(parts)

    def test_source_export_roles_and_metadata_must_match_actual_geometry(self):
        meshes, body = assemble(fixture())
        body["faceGroups"]["BoulderMass01"].append(0)
        with self.assertRaisesRegex(AssertionError, "overlapping primary regular roles"):
            audit.audit_regular_geometry(meshes, body, 15, .91, "fixture")
        meshes, body = assemble(fixture())
        meshes[-1].triangles[0] = meshes[-1].triangles[0][::-1]
        with self.assertRaisesRegex(AssertionError, "source/export regular triangle ownership"):
            audit.audit_regular_geometry(meshes, body, 15, .91, "fixture")
        for field in ("RegularDesignRevision", "RegularBoulderMassCount", "RegularRockMaximumZ"):
            meshes, body = assemble(fixture())
            body["properties"][field] = -1
            with self.assertRaisesRegex(AssertionError, "revision/rock metadata"):
                audit.audit_regular_geometry(meshes, body, 15, .91, "fixture")

    def test_core_subroles_and_vertex_selections_are_independently_checked(self):
        meshes, body = assemble(fixture())
        body["faceGroups"]["RecessBacking"].pop()
        with self.assertRaisesRegex(AssertionError, "RecessBacking role differs"):
            audit.audit_regular_geometry(meshes, body, 15, .91, "fixture")
        meshes, body = assemble(fixture())
        body["vertexGroups"]["BoulderMass01"].pop()
        with self.assertRaisesRegex(AssertionError, "selection misses its faces"):
            audit.audit_regular_geometry(meshes, body, 15, .91, "fixture")

    def test_negative_or_out_of_range_source_vertices_cannot_alias_valid_points(self):
        for index in (-1, 100000):
            _, body = assemble(fixture())
            body["polygons"][0][0] = index
            with self.assertRaisesRegex(AssertionError, "invalid triangulated native source geometry"):
                audit.source_roles(body, "fixture")

    def test_unknown_material_names_cannot_bypass_original_pbr_parameters(self):
        original = {"name": "Original rock", "pbrMetallicRoughness": {"roughnessFactor": .8}}
        signatures = {original["name"]: copy.deepcopy(original)}
        audit.require_original_material(None, original, signatures, "fixture")
        for changed in ({**original, "name": "Unrecognized rock"},
                        {**original, "pbrMetallicRoughness": {"roughnessFactor": .2}}):
            with self.assertRaisesRegex(AssertionError, "PBR material name or parameters changed"):
                audit.require_original_material(None, changed, signatures, "fixture")

    def test_retained_backing_and_base_preserve_exact_uv_normal_and_face_data(self):
        old, _ = assemble(fixture())
        old_streams = streams(old)
        for index, corruption, message in ((1, "uv", "backing"), (1, "normal", "backing"),
                                             (1, "point", "backing"), (0, "uv", "base"),
                                             (0, "triangle", "base")):
            meshes, coordinates = copy.deepcopy(old), copy.deepcopy(old_streams)
            if corruption == "uv":
                coordinates[index]["TEXCOORD_1"][0, 0] += 1e-7
            elif corruption == "normal":
                meshes[index].normals[0, 0] += 1e-7
            elif corruption == "point":
                meshes[index].points[0, 0] += 1e-7
            else:
                meshes[index].triangles[0] = meshes[index].triangles[0][::-1]
            with self.assertRaisesRegex(AssertionError, "original "+message+" faces"):
                audit.audit_retained_geometry(meshes, old, coordinates, old_streams, "fixture")

    def test_retained_face_comparison_accepts_only_order_and_cyclic_start_changes(self):
        old, _ = assemble(fixture())
        meshes, coordinates = copy.deepcopy(old), streams(old)
        for mesh in meshes:
            mesh.triangles = np.roll(mesh.triangles[::-1], 1, axis=1)
        audit.audit_retained_geometry(meshes, old, coordinates, coordinates, "fixture")


if __name__ == "__main__":
    unittest.main()
