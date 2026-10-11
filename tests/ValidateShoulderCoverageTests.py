"""Destructive fixtures for pure near-cap shoulder ray coverage."""
from pathlib import Path
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ground_shoulder_coverage as audit


def box(low, high):
    x0, y0, z0 = low
    x1, y1, z1 = high
    points = np.asarray([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
                         [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]])
    return points[np.asarray([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
                              [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
                              [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]])]


def shoulders(lip=.91, low=.4, high=None):
    high = lip - .002 if high is None else high
    return np.concatenate([box([0, .01, low], [.49, .24, high]),
                           box([.51, .02, low], [1, .26, high])])


def rotate(values, turns):
    result = values.copy()
    for _ in range(turns):
        previous = result.copy()
        result[:, :, 0] = 1 - previous[:, :, 1]
        result[:, :, 1] = previous[:, :, 0]
    return result


class ShoulderCoverageTests(unittest.TestCase):
    def test_two_chunky_shoulders_reach_both_slices_beneath_either_cap(self):
        for lip in (.91, 1):
            result = audit.audit_shoulder_coverage(shoulders(lip), 1, lip)
            self.assertEqual(result["slice_checks"], 2)
            self.assertGreater(result["minimum_measured_coverage"], .97)
            self.assertAlmostEqual(result["largest_gap"], .02)
            for report in result["slices"]:
                self.assertEqual(report["sample_count"], 101)
                self.assertEqual(len(report["covered_intervals"]), 2)
                self.assertAlmostEqual(report["frontmost_depths"][0], .01)
                self.assertAlmostEqual(report["frontmost_depths"][-1], .02)

    def test_old_low_boulder_ceiling_cannot_pass(self):
        with self.assertRaisesRegex(AssertionError, "north shoulder coverage.*0.0%"):
            audit.audit_shoulder_coverage(shoulders(high=.777), 1, .91)

    def test_tall_narrow_spike_cannot_replace_broad_shoulders(self):
        spike = box([.48, .01, .4], [.52, .2, .908])
        with self.assertRaisesRegex(AssertionError, "below 80.0%"):
            audit.audit_shoulder_coverage(spike, 1, .91)

    def test_shoulders_behind_backing_depth_are_rejected(self):
        inset = shoulders()
        inset[:, :, 1] += .046
        with self.assertRaisesRegex(AssertionError, "shoulder coverage.*0.0%"):
            audit.audit_shoulder_coverage(inset, 1, .91)

    def test_missing_wide_interval_is_rejected_and_gap_is_reported(self):
        partial = np.concatenate([box([0, .01, .4], [.35, .25, .908]),
                                  box([.65, .01, .4], [1, .25, .908])])
        measured = audit.measure_shoulder_slice(partial, 0, .89)
        self.assertAlmostEqual(measured["largest_gap"], .30)
        with self.assertRaisesRegex(AssertionError, "largest uncovered interval 0.300000"):
            audit.audit_shoulder_coverage(partial, 1, .91)

    def test_upper_only_roof_cannot_hide_a_missing_lower_slice(self):
        roof = shoulders(low=.88)
        upper = audit.measure_shoulder_slice(roof, 0, .89)
        self.assertGreater(upper["coverage_fraction"], .97)
        with self.assertRaisesRegex(AssertionError, "Z=0.860000.*0.0%"):
            audit.audit_shoulder_coverage(roof, 1, .91)

    def test_side_rotations_use_correct_minimum_and_maximum_planes(self):
        original = audit.audit_shoulder_coverage(shoulders(), 1, .91)
        for side in range(4):
            result = audit.audit_shoulder_coverage(rotate(shoulders(), side), 1 << side, .91)
            self.assertEqual(result["slices"][0]["side_index"], side)
            self.assertAlmostEqual(result["minimum_measured_coverage"], original["minimum_measured_coverage"])
            self.assertAlmostEqual(result["largest_gap"], .02)
            self.assertAlmostEqual(result["slices"][0]["frontmost_depths"][0], .01)
            self.assertAlmostEqual(result["slices"][0]["frontmost_depths"][-1], .02)

    def test_overlapping_hulls_use_frontmost_depth_and_union_not_sum(self):
        faces = np.concatenate([shoulders(), box([0, .1, .4], [1, .3, .908]), shoulders()])
        result = audit.measure_shoulder_slice(faces, 0, .89)
        self.assertLessEqual(result["coverage_fraction"], 1)
        self.assertAlmostEqual(result["largest_gap"], .02)
        self.assertAlmostEqual(result["frontmost_depths"][0], .01)
        self.assertAlmostEqual(result["frontmost_depths"][50], .1)

    def test_sloped_depth_crossing_is_clipped_at_exact_border_threshold(self):
        sloped = box([0, .01, .4], [1, .2, .908])
        sloped[:, :, 1] += .1 * sloped[:, :, 0]
        result = audit.measure_shoulder_slice(sloped, 0, .89)
        self.assertEqual(len(result["covered_intervals"]), 1)
        self.assertAlmostEqual(result["covered_intervals"][0][1], .45, places=6)
        self.assertAlmostEqual(result["coverage_fraction"], (.45 - .01) / .98, places=6)

    def test_coplanar_roofs_and_triangle_order_are_stable(self):
        faces = shoulders(high=.89)
        first = audit.measure_shoulder_slice(faces, 0, .89)
        second = audit.measure_shoulder_slice(faces[::-1, ::-1], 0, .89)
        self.assertAlmostEqual(first["coverage_fraction"], second["coverage_fraction"])
        self.assertGreater(first["coverage_fraction"], .97)
        self.assertEqual(first["covered_intervals"], second["covered_intervals"])

    def test_empty_interior_has_no_required_exposed_slices(self):
        result = audit.audit_shoulder_coverage([], 0, .91)
        self.assertEqual(result["slice_checks"], 0)
        with self.assertRaisesRegex(AssertionError, "shoulder coverage.*0.0%"):
            audit.audit_shoulder_coverage([], 1, .91)

    def test_bottom_overhang_is_explicit_and_negative_frontmost_depth_is_retained(self):
        faces = shoulders()
        faces[:, :, 1] -= .03
        with self.assertRaisesRegex(AssertionError, "unit XY"):
            audit.audit_shoulder_coverage(faces, 1, .91)
        with self.assertRaisesRegex(AssertionError, "allowed overhang"):
            audit.audit_shoulder_coverage(faces, 1, .91, max_overhang=.01)
        result = audit.audit_shoulder_coverage(faces, 1, .91, max_overhang=.04)
        self.assertGreater(result["minimum_measured_coverage"], .97)
        self.assertAlmostEqual(result["slices"][0]["frontmost_depths"][0], -.02)
        self.assertAlmostEqual(result["slices"][0]["frontmost_depths"][-1], -.01)
        for side in range(4):
            rotated = audit.audit_shoulder_coverage(rotate(faces, side), 1 << side, .91, max_overhang=.04)
            self.assertAlmostEqual(rotated["slices"][0]["frontmost_depths"][0], -.02)

    def test_nonfinite_outside_or_malformed_triangle_data_is_rejected(self):
        for field, value, message in ((0, -.001, "unit XY"), (2, np.nan, "finite Nx3x3")):
            faces = shoulders()
            faces[0, 0, field] = value
            with self.assertRaisesRegex(AssertionError, message):
                audit.measure_shoulder_slice(faces, 0, .89)
        with self.assertRaisesRegex(AssertionError, "finite Nx3x3"):
            audit.measure_shoulder_slice([[0, 0, 0]], 0, .89)


if __name__ == "__main__":
    unittest.main()
