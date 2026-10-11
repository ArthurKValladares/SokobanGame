"""Destructive fixtures for the separate collar-free transition audit."""
from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import validate_ground_shoulders as audit
from ValidateGroundBottomsTests import primitive, box_faces


def accepted_parts(bottom=False, lip=.91):
    base = box_faces([0, 0, -.34 if bottom else 0], [1, 1, lip])[:2]
    backing = np.asarray([[[0, .055, 0], [1, .055, 0], [1, .055, lip]],
                          [[0, .055, 0], [1, .055, lip], [0, .055, lip]]])
    seal = np.asarray([[[0, 0, lip], [1, 0, lip], [1, .055, lip]],
                       [[0, 0, lip], [1, .055, lip], [0, .055, lip]]])
    collar = np.asarray([[[0, 0, .78], [1, 0, .78], [1, 0, lip]],
                         [[0, 0, .78], [1, 0, lip], [0, 0, lip]]])
    parts = {"base": base, "backing": backing, "seal": seal, "collar": collar,
             "BoulderMass01": box_faces([.1, .02, .08], [.45, .25, .7])}
    if bottom:
        parts["FloatingRock01"] = box_faces([.2, .2, -.82], [.4, .4, -.62])
    return parts


def current_parts(bottom=False, lip=.91):
    parts = accepted_parts(bottom, lip)
    del parts["collar"]
    parts["ShoulderRock01"] = box_faces([.0002, .0002, .4], [.9998, .32, lip-.001])
    return parts


def assemble(parts, bottom=False):
    meshes, points, polygons, groups, core = [], [], [], {}, []
    for name, faces in parts.items():
        mesh = primitive(faces, {"name": "GroundModuleRecess" if name == "backing" else "stone"})
        indices = list(range(len(polygons), len(polygons)+len(mesh.triangles)))
        meshes.append(mesh)
        polygons.extend((mesh.triangles+len(points)).tolist())
        points.extend(mesh.points.tolist())
        if name.startswith(("BoulderMass", "FloatingRock", "ShoulderRock")):
            groups[name] = indices
        else:
            core.extend(indices)
    groups["IslandCore" if bottom else "RegularCore"] = core
    positions = np.asarray(points)
    body = {"points": points, "polygons": polygons, "vertices": len(points), "faces": len(polygons),
            "faceGroups": groups, "vertexGroups": {name: sorted({vertex for index in indices for vertex in polygons[index]})
                                                      for name, indices in groups.items()},
            "bounds": {"min": positions.min(0).tolist(), "max": positions.max(0).tolist()}}
    streams = [{"TEXCOORD_0": mesh.uvs.copy(), "TEXCOORD_1": mesh.uvs.copy()*2+.3} for mesh in meshes]
    return meshes, body, streams


def preservation(parts=None, bottom=False):
    old, accepted, old_streams = assemble(accepted_parts(bottom), bottom)
    meshes, body, streams = assemble(parts or current_parts(bottom), bottom)
    return audit.audit_preservation(meshes, old, streams, old_streams, body, accepted, bottom, .91, "fixture")


def geometry(parts=None, bottom=False, mask=1):
    _, body, _ = assemble(parts or current_parts(bottom), bottom)
    return audit.audit_shoulder_geometry(audit.source_roles(body, bottom, "fixture"), bottom, mask, .91, "fixture")


class GroundShoulderAuditTests(unittest.TestCase):
    @patch.object(audit.native, "texture_path", return_value=audit.native.ASSETS/"fixture.png")
    def test_material_texture_sampler_and_pbr_parameters_remain_exact(self, _):
        document = SimpleNamespace(path=audit.native.DEFAULT_MODELS/"fixture.glb", data={
            "textures": [{"source": 0}], "images": [{"uri": "../../textures/fixture.png"}]})
        material = {"name": "stone", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}
        originals = {"stone": audit.material_signature(document, material)}
        audit.require_original_material(document, material, originals, "fixture")
        document.data["samplers"] = [{"wrapS": 10497, "wrapT": 10497}]
        document.data["textures"][0]["sampler"] = 0
        audit.require_original_material(document, material, originals, "fixture")
        for key, value in (("wrapS", 33071), ("wrapT", 33648), ("magFilter", 9728), ("minFilter", 9984)):
            changed = copy.deepcopy(document)
            changed.data["samplers"][0][key] = value
            with self.assertRaisesRegex(AssertionError, "texture or sampler changed"):
                audit.require_original_material(changed, material, originals, "fixture")
        changed = copy.deepcopy(material)
        changed["pbrMetallicRoughness"]["metallicFactor"] = .5
        with self.assertRaisesRegex(AssertionError, "PBR material"):
            audit.require_original_material(document, changed, originals, "fixture")

    def test_only_explicit_agreed_contract_can_select_the_new_revision(self):
        contract = {"format": 2, "nativeCapCount": 250, "squareNativeCapCount": 60,
                    "regularDesignRevision": 2, "bottomDesignRevision": 5, "shoulderTransitionRevision": 1,
                    "bottomPreserveUpperBandGeometry": False, "shoulderTransitionMinimumZ": .4,
                    "shoulderCollarRemovalMinimumZ": .78, "shoulderNearLipCoverageMinimum": .8,
                    "outerLipHeight": .91, "squareOuterLipHeight": 1, "bodyRecessInset": .055,
                    "bottomMaximumExposedSideOverhang": .04, "bottomHiddenConnectionHeight": -.34,
                    "shoulderRoofClearance": "0.001 + 0.0006 * zero-based shoulder index"}
        audit.audit_contract(contract)
        for name in ("regularDesignRevision", "bottomDesignRevision", "shoulderTransitionRevision",
                     "shoulderTransitionMinimumZ", "shoulderCollarRemovalMinimumZ",
                     "shoulderRoofClearance", "shoulderNearLipCoverageMinimum"):
            changed = copy.deepcopy(contract)
            del changed[name]
            with self.assertRaises(AssertionError):
                audit.audit_contract(changed)

    def test_only_outer_collar_is_removed_while_accepted_rocks_and_lip_remain(self):
        for bottom in (False, True):
            roles, report = preservation(bottom=bottom)
            self.assertEqual(report["removed_outer_collar_triangles"], 2)
            self.assertIn("ShoulderRock01", roles)
            self.assertTrue(report["all_accepted_rock_surfaces_topology_and_uv_preserved"])

    def test_old_collar_cannot_remain_or_be_relabelled(self):
        parts = current_parts()
        parts["collar"] = accepted_parts()["collar"]
        with self.assertRaisesRegex(AssertionError, "protected core changed or old outer collar remains"):
            preservation(parts)
        parts = current_parts()
        parts["ShoulderRock02"] = accepted_parts()["collar"]
        with self.assertRaisesRegex(AssertionError, "not closed|no closed volume"):
            geometry(parts)

    def test_every_accepted_boulder_and_floater_surface_is_exact(self):
        for name in ("BoulderMass01", "FloatingRock01"):
            parts = current_parts(True)
            parts[name][0, 0, 0] += 1e-7
            with self.assertRaisesRegex(AssertionError, "accepted "+name+" surfaces or topology changed"):
                preservation(parts, True)

    def test_protected_uv_normal_material_and_position_streams_are_exact(self):
        old, accepted, old_uv = assemble(accepted_parts())
        current, body, uv = assemble(current_parts())
        for mode in ("uv0", "uv1", "normal", "position", "material"):
            changed, changed_uv = copy.deepcopy(current), copy.deepcopy(uv)
            mesh = changed[3]  # Accepted BoulderMass01, after core parts.
            if mode.startswith("uv"):
                changed_uv[3]["TEXCOORD_"+mode[-1]][0, 0] += 1e-7
            elif mode == "normal":
                mesh.normals[0, 0] += 1e-7
            elif mode == "position":
                mesh.points[0, 0] += 1e-7
            else:
                mesh.material["name"] = "Changed stone"
            with self.assertRaisesRegex(AssertionError, "protected positions/normals/material/all-UV"):
                audit.audit_preservation(changed, old, changed_uv, old_uv, body, accepted, False, .91, "fixture")

    def test_lip_seal_and_recess_backing_cannot_change(self):
        for name in ("seal", "backing", "base"):
            parts = current_parts()
            parts[name][0, 0, 0] += .002
            with self.assertRaisesRegex(AssertionError, "protected core changed"):
                preservation(parts)

    def test_high_backing_is_protected_even_above_old_collar_height(self):
        parts = accepted_parts()
        parts["backing"][:, :, 2] = .78+parts["backing"][:, :, 2]*(.13/.91)
        old, body, _ = assemble(parts)
        _, kept, removed = audit.removable_collar_faces(body, old, .91, False)
        self.assertEqual(len(removed), 2)
        self.assertTrue({2, 3} <= kept)

    def test_closed_rooted_shoulders_reach_the_cap_without_a_separate_band(self):
        report = geometry()
        self.assertEqual(report["shoulder_rocks"], 1)
        self.assertEqual(report["directly_rooted_shoulders"], 1)
        self.assertGreater(report["near_lip_coverage"]["minimum_measured_coverage"], .99)

    def test_low_legacy_rocks_without_shoulders_fail(self):
        parts = current_parts()
        del parts["ShoulderRock01"]
        with self.assertRaisesRegex(AssertionError, "incorrect exposed/interior shoulder topology"):
            geometry(parts)

    def test_open_thin_detached_and_touching_shoulders_fail(self):
        parts = current_parts()
        parts["ShoulderRock01"] = parts["ShoulderRock01"][:-1]
        with self.assertRaisesRegex(AssertionError, "not closed"):
            geometry(parts)
        parts = current_parts()
        parts["ShoulderRock01"] = np.concatenate((parts["ShoulderRock01"], parts["ShoulderRock01"][:1]))
        with self.assertRaisesRegex(AssertionError, "not closed"):
            geometry(parts)
        parts = current_parts()
        parts["ShoulderRock01"] = box_faces([0, 0, .4], [1, .002, .909])
        with self.assertRaisesRegex(AssertionError, "thin plate or stretched spike"):
            geometry(parts)
        for minimum in ((.6, .4, .4), (.45, .25, .4)):
            parts = current_parts()
            parts["ShoulderRock01"] = box_faces(minimum, [.99, .6, .909])
            with self.assertRaisesRegex(AssertionError, "not rooted in accepted rock/core"):
                geometry(parts)

    def test_vertical_clearance_and_upper_footprint_are_strict(self):
        for low, high, message in (([.0002, .0002, .399], [.99, .32, .909], "vertical clearance"),
                                   ([.0002, .0002, .4], [.99, .32, .91], "vertical clearance"),
                                   ([-.001, .0002, .4], [.99, .32, .909], "tapered footprint")):
            parts = current_parts()
            parts["ShoulderRock01"] = box_faces(low, high)
            with self.assertRaisesRegex(AssertionError, message):
                geometry(parts)

    def test_bottom_shoulders_can_taper_overhang_only_on_exposed_sides(self):
        parts = current_parts(True)
        faces = parts["ShoulderRock01"]
        faces[:, :, 1] -= np.where(faces[:, :, 2] < .78, .0202, 0)
        geometry(parts, True)
        with self.assertRaisesRegex(AssertionError, "tapered footprint"):
            geometry(parts, True, mask=2)
        parts = current_parts(True)
        parts["ShoulderRock01"][:, :, 1] -= .02
        with self.assertRaisesRegex(AssertionError, "tapered footprint|roof overhangs"):
            geometry(parts, True)

    def test_source_roles_export_faces_and_editable_selections_are_real(self):
        meshes, body, streams = assemble(current_parts())
        old, accepted, old_streams = assemble(accepted_parts())
        body["polygons"][0][0] = -1
        with self.assertRaisesRegex(AssertionError, "invalid native triangulated"):
            audit.source_roles(body, False, "fixture")
        meshes, body, streams = assemble(current_parts())
        body["vertexGroups"]["ShoulderRock01"].pop()
        with self.assertRaisesRegex(AssertionError, "incomplete editable"):
            audit.source_roles(body, False, "fixture")
        meshes, body, streams = assemble(current_parts())
        meshes[-1].triangles[0] = meshes[-1].triangles[0][::-1]
        with self.assertRaisesRegex(AssertionError, "exported triangles differ"):
            audit.audit_preservation(meshes, old, streams, old_streams, body, accepted, False, .91, "fixture")


if __name__ == "__main__":
    unittest.main()
