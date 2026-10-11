"""Destructive fixtures for the independent floating-island export audit."""
from __future__ import annotations
import copy
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_ground_bottoms as audit


def primitive(faces, material=None):
    points = np.asarray(faces, dtype=float).reshape(-1, 3)
    triangles = np.arange(len(points)).reshape(-1, 3)
    cross = np.cross(points[triangles[:, 1]]-points[triangles[:, 0]], points[triangles[:, 2]]-points[triangles[:, 0]])
    normals = np.repeat(cross/np.linalg.norm(cross, axis=1)[:, None], 3, axis=0)
    return audit.native.Primitive(material or {}, points, normals, points[:, [0, 2]], triangles, 1)


def box_faces(low, high):
    low, high = np.asarray(low), np.asarray(high)
    points = np.asarray([[low[0], low[1], low[2]], [high[0], low[1], low[2]],
                         [high[0], high[1], low[2]], [low[0], high[1], low[2]],
                         [low[0], low[1], high[2]], [high[0], low[1], high[2]],
                         [high[0], high[1], high[2]], [low[0], high[1], high[2]]])
    return points[np.asarray([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
                              [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
                              [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]])]


def boulder_fixture(exposed=15):
    roles = {"IslandCore": box_faces([0, 0, audit.CONNECTION_HEIGHT], [1, 1, .77]),
             "BoulderMass01": box_faces([.12, .12, -.75], [.45, .45, .5]),
             "BoulderMass02": box_faces([.52, .52, -.82], [.85, .85, .4])}
    if exposed:
        roles.update({"FloatingRock01": box_faces([.05, .6, -.99], [.24, .79, -.84]),
                      "FloatingRock02": box_faces([.62, .08, -.98], [.70, .16, -.9])})
    return roles


def assemble(roles):
    faces, groups = [], {}
    for name, values in roles.items():
        groups[name] = list(range(len(faces), len(faces)+len(values)))
        faces.extend(values)
    groups["IslandUnderside"] = groups["IslandCore"][:2]
    groups["IslandRockWalls"] = groups["IslandCore"][4:]
    mesh = primitive(faces)
    source = {"points": mesh.points.tolist(), "polygons": mesh.triangles.tolist(), "faceGroups": groups,
              "vertices": len(mesh.points), "faces": len(mesh.triangles),
              "vertexGroups": {name: sorted(int(vertex) for face in indices for vertex in mesh.triangles[face])
                               for name, indices in groups.items()}}
    return mesh, source


def audit_fixture(roles, exposed=15):
    mesh, source = assemble(roles)
    return audit.audit_lower_geometry([mesh], exposed, "fixture", source)


def backing(exposed, square=False, lower=audit.CONNECTION_HEIGHT):
    faces = []
    for side in range(4):
        if exposed & (1 << side):
            a, b = [audit.native.side_point(side, u, .055, lower) for u in (0, 1)]
            c, d = [audit.native.side_point(side, u, .055, 1 if square else .91) for u in (0, 1)]
            faces.extend(([a, b, d], [a, d, c]))
    return primitive(faces, {"name": "GroundModuleRecess"}) if faces else None


def upper_quad(split=False):
    if split:
        z = audit.REMODEL_HEIGHT
        faces = [[[0, 0, z], [1, 0, z], [1, 0, 1]], [[0, 0, z], [1, 0, 1], [0, 0, 1]]]
    else:
        faces = [[[0, 0, 0], [1, 0, 0], [1, 0, 1]], [[0, 0, 0], [1, 0, 1], [0, 0, 1]]]
    body = primitive(faces, {"name": "rock"})
    streams = [{"TEXCOORD_0": body.uvs.copy(), "TEXCOORD_1": body.uvs.copy()*2+.3}]
    return body, streams


class GroundBottomAuditTests(unittest.TestCase):
    def test_valid_boulder_bodies_accept_uv_vertex_splits(self):
        for mask in (15, 11, 5, 3, 1, 0):
            _, floating, report = audit_fixture(boulder_fixture(mask), mask)
            self.assertEqual(len(floating), 2 if mask else 0)
            self.assertLess(report["minimum_z"], -.4)
            self.assertEqual(report["boulder_masses"], 2)
            self.assertTrue(report["all_masses_rooted"])
            self.assertGreater(report["boulders"]["BoulderMass01"]["volume"], .1)

    def test_rejected_pasted_on_wall_geometry_cannot_pass(self):
        upper = [[[0, 0, 0], [1, 0, 0], [1, 0, .9]], [[0, 0, 0], [1, 0, .9], [0, 0, .9]]]
        lower = [[[0, 0, 0], [1, 0, -.6], [1, 0, 0]], [[0, 0, 0], [0, 0, -.6], [1, 0, -.6]]]
        with self.assertRaisesRegex(AssertionError, "pasted onto a z=0 collar"):
            audit.audit_integration(np.asarray(upper+lower), 1, "old look")
        roles = boulder_fixture()
        del roles["BoulderMass01"], roles["BoulderMass02"]
        with self.assertRaisesRegex(AssertionError, "missing volumetric attached"):
            audit_fixture(roles)

    def test_a_hidden_connector_must_stay_flat_and_cover_its_side(self):
        roles = boulder_fixture(11)
        floor = roles["IslandCore"][:2]
        floor[np.all(np.isclose(floor, [1, 1, audit.CONNECTION_HEIGHT]), axis=2), 2] += .01
        with self.assertRaisesRegex(AssertionError, "hidden floor connector"):
            audit_fixture(roles, 11)

    def test_overhang_is_allowed_only_on_exposed_sides(self):
        roles = boulder_fixture()
        roles["BoulderMass02"] = box_faces([.8, .52, -.82], [1.03, .85, .4])
        audit_fixture(roles)
        with self.assertRaisesRegex(AssertionError, "visual envelope"):
            audit_fixture(roles, 13)
        roles["BoulderMass02"][:, :, 0] += .03
        with self.assertRaisesRegex(AssertionError, "visual envelope"):
            audit_fixture(roles)
        roles = boulder_fixture()
        roles["FloatingRock01"][:, :, 2] -= .1
        with self.assertRaisesRegex(AssertionError, "visual envelope"):
            audit_fixture(roles)

    def test_a_flat_bottom_cannot_survive_inside_the_new_body(self):
        roles = boulder_fixture()
        roles["IslandCore"] = np.concatenate((roles["IslandCore"], box_faces([0, 0, 0], [1, 1, .1])[:2]))
        with self.assertRaisesRegex(AssertionError, "old flat bottom remains"):
            audit_fixture(roles)

    def test_role_ownership_and_exported_winding_are_checked(self):
        mesh, source = assemble(boulder_fixture())
        source["faceGroups"]["BoulderMass01"].append(0)
        with self.assertRaisesRegex(AssertionError, "overlapping primary roles"):
            audit.audit_lower_geometry([mesh], 15, "fixture", source)
        mesh, source = assemble(boulder_fixture())
        mesh.triangles[15] = mesh.triangles[15][::-1]
        with self.assertRaisesRegex(AssertionError, "source/export triangle ownership"):
            audit.audit_lower_geometry([mesh], 15, "fixture", source)

    def test_missing_and_open_floating_rocks_are_rejected(self):
        roles = boulder_fixture()
        del roles["FloatingRock01"], roles["FloatingRock02"]
        with self.assertRaisesRegex(AssertionError, "floating-rock count"):
            audit_fixture(roles)
        roles = boulder_fixture()
        roles["FloatingRock01"] = roles["FloatingRock01"][:-1]
        with self.assertRaisesRegex(AssertionError, "not closed"):
            audit_fixture(roles)
        with self.assertRaisesRegex(AssertionError, "floating-rock count"):
            audit_fixture(boulder_fixture(), 0)

    def test_boulders_require_closed_convex_volumes(self):
        roles = boulder_fixture()
        roles["BoulderMass01"] = roles["BoulderMass01"][:-1]
        with self.assertRaisesRegex(AssertionError, "not closed"):
            audit_fixture(roles)
        roles = boulder_fixture()
        mass = roles["BoulderMass01"]
        mass[np.all(np.isclose(mass, [.45, .45, .5]), axis=2)] = [.25, .25, -.2]
        with self.assertRaisesRegex(AssertionError, "concave or wound inward"):
            audit_fixture(roles)
        roles = boulder_fixture()
        roles["BoulderMass01"] = box_faces([.12, .12, -.75], [.45, .13, .5])
        with self.assertRaisesRegex(AssertionError, "thin plate or stretched spike"):
            audit_fixture(roles)

    def test_attachment_graph_allows_overlap_chains_but_not_detached_masses(self):
        roles = boulder_fixture()
        roles["BoulderMass02"] = box_faces([.16, .16, -.95], [.42, .42, -.65])
        report = audit_fixture(roles)[2]
        self.assertEqual(report["directly_rooted_masses"], 1)
        roles["BoulderMass02"] = box_faces([.72, .35, -.95], [.95, .48, -.55])
        with self.assertRaisesRegex(AssertionError, "not rooted in the core"):
            audit_fixture(roles)
        roles["BoulderMass02"] = box_faces([.52, .52, -.82], [.85, .85, audit.CONNECTION_HEIGHT])
        with self.assertRaisesRegex(AssertionError, "not rooted in the core"):
            audit_fixture(roles)

    def test_missing_floor_faces_cannot_leave_an_exposed_hole(self):
        roles = boulder_fixture()
        roles["IslandCore"] = np.delete(roles["IslandCore"], 1, axis=0)
        mesh, source = assemble(roles)
        source["faceGroups"]["IslandUnderside"] = [0]
        with self.assertRaisesRegex(AssertionError, "does not attach to its exposed wall boundary"):
            audit.audit_lower_geometry([mesh], 15, "fixture", source)

    def test_floating_rocks_need_actual_surface_gaps(self):
        for low, high, message in (([.05, .6, .1], [.24, .79, .25], "inside its core"),
                                   ([.05, .6, -.50], [.24, .79, -.341], "actual gap from its core"),
                                   ([.12, .12, -.84], [.31, .31, -.69], "intersects another rock")):
            roles = boulder_fixture()
            roles["FloatingRock01"] = box_faces(low, high)
            with self.assertRaisesRegex(AssertionError, message):
                audit_fixture(roles)
        roles = boulder_fixture()
        roles["FloatingRock01"] = box_faces([.14, .14, -.99], [.33, .33, -.80])
        self.assertGreater(audit_fixture(roles)[2]["floating_rock_gaps"]["FloatingRock01"], .04)

    def test_floating_rocks_have_visible_size_and_variation(self):
        roles = boulder_fixture()
        roles["FloatingRock01"] = box_faces([.05, .6, -.99], [.13, .68, -.91])
        with self.assertRaisesRegex(AssertionError, "tiny specks"):
            audit_fixture(roles)
        roles["FloatingRock01"] = box_faces([.05, .6, -.99], [.24, .79, -.84])
        roles["FloatingRock02"] = box_faces([.62, .08, -.99], [.81, .27, -.84])
        with self.assertRaisesRegex(AssertionError, "lack visible size variation"):
            audit_fixture(roles)

    def test_new_boulders_cannot_enter_preserved_band(self):
        roles = boulder_fixture()
        roles["BoulderMass01"][:, :, 2] += .28
        with self.assertRaisesRegex(AssertionError, "preserved upper band"):
            audit_fixture(roles)

    def test_skew_edges_determine_the_exact_surface_gap(self):
        a = np.asarray([[[-1, 0, 0], [1, 0, 0], [0, -1, -1]]], dtype=float)
        b = np.asarray([[[0, -1, .2], [0, 1, .2], [1, 0, 1.2]]], dtype=float)
        self.assertAlmostEqual(audit.mesh_surface_distance(a, b), .2)

    def test_convex_attachment_test_includes_edge_cross_axes(self):
        # These oriented boxes overlap along all face normals, but a skew
        # edge pair separates them. Face-only SAT would incorrectly attach them.
        rotation = np.asarray([[-.494291274558, .773844081021, -.396032163043],
                               [-.674674624231, -.054220407901, .736121116927],
                               [.548169943827, .631051095899, .548893639104]])
        faces = box_faces([-.4, -.12, -.12], [.4, .12, .12])
        a = audit.convex_rock(faces, "a")
        b = audit.convex_rock(faces @ rotation.T+[.470233229003, .463539362984, .166321665927], "b")
        axes = audit.unique_axes(np.concatenate((a["normals"], b["normals"])))
        ap, bp = a["points"] @ axes.T, b["points"] @ axes.T
        self.assertLess(np.maximum(ap.min(0)-bp.max(0), bp.min(0)-ap.max(0)).max(), 0)
        self.assertAlmostEqual(audit.convex_gap(a, b), .091090750589, places=9)
        self.assertAlmostEqual(audit.mesh_surface_distance(a["faces"], b["faces"]), .091090750589, places=9)

    def test_thinning_maps_every_exposure_envelope_and_keeps_hidden_planes(self):
        for mask in range(16):
            old_low = [-.12 if mask & 8 else 0, -.12 if mask & 1 else 0]
            old_high = [1.12 if mask & 2 else 1, 1.12 if mask & 4 else 1]
            body = {"points": [[*old_low, -.8], [*old_high, .777], [.31, .72, .4]],
                    "vertexGroups": {"BoulderMass01": [0, 1], "IslandCore": [2]}}
            points, selected, _ = audit.compressed_rock_points(body, mask)
            self.assertEqual(selected, [0, 1])
            np.testing.assert_allclose(points[0, :2], [-.04 if mask & 8 else 0, -.04 if mask & 1 else 0], atol=1e-15)
            np.testing.assert_allclose(points[1, :2], [1.04 if mask & 2 else 1, 1.04 if mask & 4 else 1], atol=1e-15)
            np.testing.assert_array_equal(points[:, 2], np.asarray(body["points"])[:, 2])
            np.testing.assert_array_equal(points[2], body["points"][2])

    def test_thinning_preserves_exact_core_z_roles_and_affine_shape(self):
        _, old = assemble(boulder_fixture())
        new = copy.deepcopy(old)
        points, _, _ = audit.compressed_rock_points(old, 15)
        new["points"] = points.tolist()
        _, report = audit.audit_accepted_shape_source(new, old, 15, "fixture")
        self.assertEqual(report["maximum_affine_position_error"], 0)
        for field in ("polygons", "faceGroups", "vertexGroups"):
            broken = copy.deepcopy(new)
            if field == "polygons":
                broken[field][0] = broken[field][0][::-1]
            else:
                broken[field]["BoulderMass01"] = broken[field]["BoulderMass01"][:-1]
            with self.assertRaisesRegex(AssertionError, "accepted "+field+" changed"):
                audit.audit_accepted_shape_source(broken, old, 15, "fixture")
        for vertex, axis, delta, message in ((0, 0, 1e-10, "core or cap positions"),
                                             (40, 2, 1e-10, "Z values"),
                                             (40, 0, 1e-6, "exact accepted affine map")):
            broken = copy.deepcopy(new)
            broken["points"][vertex][axis] += delta
            with self.assertRaisesRegex(AssertionError, message):
                audit.audit_accepted_shape_source(broken, old, 15, "fixture")

    def test_thinning_export_preserves_all_uv_corner_streams_and_pbr(self):
        old_mesh, old = assemble(boulder_fixture())
        new = copy.deepcopy(old)
        points, _, _ = audit.compressed_rock_points(old, 15)
        new["points"] = points.tolist()
        mapping, _ = audit.audit_accepted_shape_source(new, old, 15, "fixture")
        mesh = primitive(points[np.asarray(new["polygons"])])
        streams = [{"TEXCOORD_0": old_mesh.uvs.copy(), "TEXCOORD_1": old_mesh.uvs.copy()*2+.3}]
        old_document = SimpleNamespace(data={"materials": [{"roughnessFactor": .8}]})
        document = copy.deepcopy(old_document)
        # Reordering exported triangles and cyclic corner starts is harmless.
        mesh.triangles = np.roll(mesh.triangles[::-1], 1, axis=1)
        audit.audit_accepted_shape_export([mesh], [old_mesh], streams, streams, mapping,
                                          document, old_document, "fixture")
        broken = copy.deepcopy(streams)
        broken[0]["TEXCOORD_1"][40, 0] += 1e-7
        with self.assertRaisesRegex(AssertionError, "material/UV corner streams"):
            audit.audit_accepted_shape_export([mesh], [old_mesh], broken, streams, mapping,
                                              document, old_document, "fixture")
        document.data["materials"][0]["roughnessFactor"] += .01
        with self.assertRaisesRegex(AssertionError, "accepted PBR materials changed"):
            audit.audit_accepted_shape_export([mesh], [old_mesh], streams, streams, mapping,
                                              document, old_document, "fixture")

    def test_top_band_may_be_bisected_and_retriangulated(self):
        original, original_streams = upper_quad()
        body, streams = upper_quad(True)
        report = audit.audit_top_band([body], [original], streams, original_streams, "fixture")
        self.assertTrue(report["original_authored_upper_vertices_preserved"])
        self.assertGreater(report["bidirectional_position_uv_normal_samples"], 0)
        body.points[np.isclose(body.points[:, 2], audit.REMODEL_HEIGHT), 0] += .0003
        audit.audit_top_band([body], [original], streams, original_streams, "cut cleanup")

    def test_top_band_checks_second_uv_stream_and_original_vertices(self):
        original, original_streams = upper_quad()
        body, streams = upper_quad(True)
        streams[0]["TEXCOORD_1"] += .01
        with self.assertRaisesRegex(AssertionError, "upper vertex position or UV"):
            audit.audit_top_band([body], [original], streams, original_streams, "fixture")
        body, streams = upper_quad(True)
        streams[0]["TEXCOORD_1"][np.isclose(body.points[:, 2], audit.REMODEL_HEIGHT), 0] += .02
        with self.assertRaisesRegex(AssertionError, "UV or normal coverage"):
            audit.audit_top_band([body], [original], streams, original_streams, "cut UV corruption")
        body, streams = upper_quad(True)
        body.points[np.isclose(body.points[:, 2], 1), 0] += .01
        with self.assertRaisesRegex(AssertionError, "upper vertex position or UV"):
            audit.audit_top_band([body], [original], streams, original_streams, "fixture")

    def test_top_band_rejects_missing_surface_and_changed_normals(self):
        original, original_streams = upper_quad()
        body, streams = upper_quad(True)
        body.triangles = body.triangles[:-1]
        with self.assertRaisesRegex(AssertionError, "surface area"):
            audit.audit_top_band([body], [original], streams, original_streams, "fixture")
        original, original_streams = upper_quad(True)
        original.points[:, 2] += .01
        body = copy.deepcopy(original)
        body.normals[:, 0] += .1
        with self.assertRaisesRegex(AssertionError, "normal coverage"):
            audit.audit_top_band([body], [original], original_streams, original_streams, "fixture")

    def test_rotated_joins_read_hidden_floor_and_supported_regular_frames(self):
        loaded = {}
        for square in (False, True):
            for name, mask in audit.FAMILIES:
                body, source = assemble(boulder_fixture(mask))
                main = audit.audit_lower_geometry([body], mask, "fixture", source)[0]
                new_backing, regular = backing(mask, square), backing(mask, square, 0)
                model = {**main, "primitives": [body]+([new_backing] if new_backing else []),
                         "regular_primitives": [regular] if regular else []}
                for variant in range(10):
                    loaded[(square, name, variant)] = model
        report = audit.audit_rotated_joins(loaded)
        self.assertEqual(report["all_rotated_exposure_masks"], 16)
        self.assertEqual(report["actual_mixed_style_connected_edge_joins"], 25600)
        self.assertGreater(report["mixed_supported_regular_backing_contacts"], 1000)
        broken = copy.deepcopy(loaded)
        for a, b in broken[(False, "end", 0)]["floor_boundary"]:
            if abs(a[1]-1) < audit.TOLERANCE and abs(b[1]-1) < audit.TOLERANCE:
                a[2] -= .01
                break
        with self.assertRaisesRegex(AssertionError, "hidden floor connector"):
            audit.audit_rotated_joins(broken)
        broken = copy.deepcopy(loaded)
        regular = broken[(False, "end", 0)]["regular_primitives"][0]
        regular.points[np.isclose(regular.points[:, 2], .91), 2] -= .01
        with self.assertRaisesRegex(AssertionError, "regular ground misses"):
            audit.audit_rotated_joins(broken)


if __name__ == "__main__":
    unittest.main()
