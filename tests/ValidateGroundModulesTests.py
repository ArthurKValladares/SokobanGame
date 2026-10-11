"""Regression fixtures for the independent square-ground seam audit."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("validate_ground_modules", ROOT / "tools/validate_ground_modules.py")
AUDIT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = AUDIT
SPEC.loader.exec_module(AUDIT)


class SquareGroundAuditTests(unittest.TestCase):
    def setUp(self):
        self.contract = {"squareNativeCapCount": 60,
                         "families": [{"mask": mask} for mask in (15, 11, 5, 3, 1, 0)]}
        self.document = {"format": 2, "models": [
            {"variant": variant, "shapeIndex": shape, "exposedSideMask": family["mask"],
             "concaveCorners": 0, "object": f"square_{variant}_{shape}",
             "vertices": [[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]],
             "faces": [[0, 1, 2, 3]], "normals": [[0, 0, 1]], "wallCoverage": [[0, 0, 0, 0]]}
            for variant in range(10) for shape, family in enumerate(self.contract["families"])]}

    def load(self):
        output = ROOT / "out/ground-modules"
        output.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="square_audit_", dir=output) as temporary:
            path = Path(temporary) / "caps.json"
            path.write_text(json.dumps(self.document), encoding="utf-8")
            return AUDIT.read_square_caps(path, self.contract)

    def test_complete_square_kit_accepts_all_styles_and_shapes(self):
        caps, report = self.load()
        self.assertEqual(len(caps), 60)
        self.assertTrue(report["full_unit_square_at_z_1"])
        for cap in caps.values():
            for side in range(4):
                trace = AUDIT.cap_edge_trace(cap, side)
                for station in (0, .25, .5, .75, 1):
                    self.assertEqual(AUDIT.sample_trace(trace, station).tolist(), [1, 0])

    def test_original_chamfer_gap_cannot_enter_square_table(self):
        self.document["models"][0]["vertices"][0][2] = .91
        with self.assertRaisesRegex(AssertionError, "complete exact unit footprint at Z=1"):
            self.load()

    def test_inset_corner_cannot_leave_stacked_footprint_gap(self):
        self.document["models"][0]["vertices"][1][0] = .94
        with self.assertRaisesRegex(AssertionError, "complete exact unit footprint"):
            self.load()

    def test_square_cap_rejects_material_ramp_and_downward_winding(self):
        original = copy.deepcopy(self.document)
        self.document["models"][0]["wallCoverage"][0][0] = 1
        with self.assertRaisesRegex(AssertionError, "material transition"):
            self.load()
        self.document = original
        self.document["models"][0]["faces"][0].reverse()
        with self.assertRaisesRegex(AssertionError, "projection folds"):
            self.load()

    def test_square_cap_rejects_missing_shapes_and_concave_state(self):
        original = copy.deepcopy(self.document)
        self.document["models"].pop()
        with self.assertRaisesRegex(AssertionError, "format/count"):
            self.load()
        self.document = original
        self.document["models"][0]["concaveCorners"] = 1
        with self.assertRaisesRegex(AssertionError, "canonical square cap"):
            self.load()

    def test_upper_bottom_must_fill_the_entire_stacked_footprint(self):
        points = np.asarray([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=float)
        body = AUDIT.Primitive({}, points, np.tile([0, 0, -1], (4, 1)), np.zeros((4, 2)),
                               np.asarray([[0, 2, 1], [0, 3, 2]]), 1)
        bottom = AUDIT.body_bottom([body], "fixture bottom")
        self.assertEqual(AUDIT.sample_trace(AUDIT.cap_edge_trace(bottom, 1), .5).tolist(), [0, 0])
        points[1:3, 0] = .94
        with self.assertRaisesRegex(AssertionError, "projected area"):
            AUDIT.body_bottom([body], "inset upper bottom")


if __name__ == "__main__":
    unittest.main()
