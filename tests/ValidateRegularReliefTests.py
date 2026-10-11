"""Destructive fixtures for sparse regular-only inward rock relief."""
from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import validate_regular_relief as audit
from ValidateGroundBottomsTests import primitive
from ValidateGroundShouldersTests import assemble, current_parts


def fixture():
    _, old, _ = assemble(current_parts())
    body = copy.deepcopy(old)
    points, polygons = np.asarray(old["points"]), np.asarray(old["polygons"])
    parent = next(index for index in old["faceGroups"]["ShoulderRock01"]
                  if np.cross(points[polygons[index][1]]-points[polygons[index][0]],
                              points[polygons[index][2]]-points[polygons[index][0]])[1] < -1e-4)
    vertices = polygons[parent].tolist()
    face = points[vertices]
    normal = np.cross(face[1]-face[0], face[2]-face[0]); normal /= np.linalg.norm(normal)
    bary, depth, center = [.29, .33, .38], .0048, len(points)
    body["points"].append((np.asarray(bary)@face-depth*normal).tolist())
    replacement = [[vertices[i], vertices[(i+1) % 3], center] for i in range(3)]
    body["polygons"] = old["polygons"][:parent]+replacement+old["polygons"][parent+1:]
    for name, indices in old["faceGroups"].items():
        body["faceGroups"][name] = [j for i in indices for j in
                                    ([i] if i < parent else [i+2] if i > parent else range(parent, parent+3))]
        if parent in indices:
            body["vertexGroups"][name].append(center)
    body["vertices"] += 1; body["faces"] += 2
    body["reliefFaces"] = [{"parent": parent, "vertices": vertices, "center": center,
                            "barycentric": bary, "depth": depth, "role": "ShoulderRock01"}]
    return body, old


def source(body=None):
    current, old = fixture()
    return audit.audit_source_refinement(body or current, old, False, 1, .91, "fixture")


def export_fixture():
    body, old = fixture()
    current = primitive(np.asarray(body["points"])[body["polygons"]], {"name": "stone"})
    accepted = primitive(np.asarray(old["points"])[old["polygons"]], {"name": "stone"})
    streams = [{"TEXCOORD_0": current.uvs.copy(), "TEXCOORD_1": current.uvs.copy()*2+.3}]
    old_streams = [{"TEXCOORD_0": accepted.uvs.copy(), "TEXCOORD_1": accepted.uvs.copy()*2+.3}]
    return [current], [accepted], streams, old_streams, body, old


class RegularReliefTests(unittest.TestCase):
    def test_shallow_triangle_fan_preserves_all_old_edges_and_bounds(self):
        roles, report = source()
        self.assertEqual(report["selected_faces"], 1)
        self.assertEqual(report["added_triangles"], 2)
        self.assertAlmostEqual(report["maximum_relief_depth"], .0048)
        old_roles = audit.shoulder.source_roles(fixture()[1], False, "accepted fixture")
        closed = audit.audit_closed_roles(roles, old_roles, "fixture")
        self.assertTrue(closed["ShoulderRock01"]["closed_consistently_wound"])
        self.assertLess(closed["ShoulderRock01"]["retained_volume_fraction"], 1)

    def test_original_positions_are_exactly_protected(self):
        body, _ = fixture(); body["points"][0][0] += 1e-8
        with self.assertRaisesRegex(AssertionError, "original vertices"):
            source(body)

    def test_excessive_and_outward_declared_depth_are_rejected(self):
        for depth in (.00721, -.003, 0, float("nan")):
            body, _ = fixture(); body["reliefFaces"][0]["depth"] = depth
            with self.assertRaisesRegex(AssertionError, "shallow inward depth"):
                source(body)

    def test_actual_center_cannot_shift_sideways_or_outward(self):
        body, _ = fixture(); center = body["reliefFaces"][0]["center"]
        body["points"][center][0] += .001
        with self.assertRaisesRegex(AssertionError, "moves laterally or outward"):
            source(body)

    def test_center_must_project_strictly_inside_original_triangle(self):
        for bary in ([0, .5, .5], [-.1, .6, .5], [.3, .3, .3]):
            body, _ = fixture(); body["reliefFaces"][0]["barycentric"] = bary
            with self.assertRaisesRegex(AssertionError, "barycentric center"):
                source(body)

    def test_duplicate_parent_and_center_cannot_rebaseline_geometry(self):
        body, _ = fixture(); body["reliefFaces"].append(copy.deepcopy(body["reliefFaces"][0]))
        with self.assertRaises(AssertionError):
            source(body)

    def test_parent_orientation_and_child_winding_are_protected(self):
        body, _ = fixture(); item = body["reliefFaces"][0]
        item["vertices"] = list(reversed(item["vertices"]))
        with self.assertRaisesRegex(AssertionError, "vertices or winding"):
            source(body)
        body, _ = fixture(); parent = body["reliefFaces"][0]["parent"]
        body["polygons"][parent].reverse()
        with self.assertRaisesRegex(AssertionError, "coverage, winding"):
            source(body)

    def test_gap_or_repeated_child_cannot_pass(self):
        body, _ = fixture(); parent = body["reliefFaces"][0]["parent"]
        body["polygons"][parent+1] = body["polygons"][parent].copy()
        with self.assertRaisesRegex(AssertionError, "coverage, winding"):
            source(body)

    def test_role_changes_and_hidden_faces_are_rejected(self):
        body, _ = fixture(); body["reliefFaces"][0]["role"] = "RegularCore"
        with self.assertRaisesRegex(AssertionError, "accepted rock role"):
            source(body)
        body, old = fixture()
        with self.assertRaisesRegex(AssertionError, "hidden, top or non-cardinal"):
            audit.audit_source_refinement(body, old, False, 2, .91, "fixture")

    def test_bottom_native_geometry_is_exactly_protected(self):
        _, old, _ = assemble(current_parts(True), True)
        old["properties"] = {"BottomRevision": 5}
        body = copy.deepcopy(old); body["reliefFaces"] = []
        audit.audit_source_refinement(body, old, True, 1, .91, "fixture")
        body["points"][0][0] += 1e-8
        with self.assertRaisesRegex(AssertionError, "protected bottom native points"):
            audit.audit_source_refinement(body, old, True, 1, .91, "fixture")

    def test_original_vertex_role_selection_cannot_change(self):
        body, _ = fixture(); body["vertexGroups"]["ShoulderRock01"].remove(body["reliefFaces"][0]["vertices"][0])
        with self.assertRaises(AssertionError):
            source(body)

    def test_uv_interpolation_material_and_flat_normals_pass(self):
        values = export_fixture()
        result = audit.audit_export_refinement(*values, "fixture")
        self.assertTrue(result["unchanged_export_corner_streams_identical"])
        self.assertTrue(result["new_center_all_uv_charts_are_parent_barycentric_interpolation"])

    def test_new_center_uv_cannot_be_reprojected_or_shifted(self):
        values = export_fixture(); current, _, streams, _, body, _ = values
        center = np.asarray(body["points"])[body["reliefFaces"][0]["center"]]
        indices = np.flatnonzero(np.all(current[0].points == center, axis=1))
        streams[0]["TEXCOORD_1"][indices[0], 0] += .0001
        with self.assertRaisesRegex(AssertionError, "center UV"):
            audit.audit_export_refinement(*values, "fixture")

    def test_unchanged_corner_uv_and_normal_are_exact(self):
        for mode in ("uv", "normal"):
            values = export_fixture(); current, _, streams, _, _, _ = values
            if mode == "uv": streams[0]["TEXCOORD_0"][0, 0] += 1e-8
            else: current[0].normals[0, 0] += 1e-8
            with self.assertRaisesRegex(AssertionError, "unchanged face positions/normals/material"):
                audit.audit_export_refinement(*values, "fixture")

    def test_relief_child_boundary_uv_and_normals_are_protected(self):
        for mode in ("uv", "normal"):
            values = export_fixture(); current, _, streams, _, body, _ = values
            index = 3*body["reliefFaces"][0]["parent"]
            if mode == "uv": streams[0]["TEXCOORD_0"][index, 0] += 1e-8
            else: current[0].normals[index, 0] += .1
            with self.assertRaisesRegex(AssertionError, "boundary UV corner|normals do not describe"):
                audit.audit_export_refinement(*values, "fixture")


if __name__ == "__main__":
    unittest.main()
