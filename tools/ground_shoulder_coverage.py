"""Pure geometry checks for rocky shoulders immediately beneath ground caps.

The caller supplies only ShoulderRock triangles and separately proves their
ownership, convexity, closure and attachment. This module intersects those
triangles with horizontal planes, then casts inward rays from exposed tile
borders. North/East/South/West use the same side coordinates as native caps.
Bottom shoulders may opt into a limited overhang; their frontmost depths can
then be negative. The caller separately enforces exposed-side/taper limits.
"""
from __future__ import annotations

from itertools import combinations

import numpy as np


TOLERANCE = 1e-8
SIDE_NAMES = ("north", "east", "south", "west")


def _triangles(values, max_overhang=0):
    if not (np.isfinite(max_overhang) and 0 <= max_overhang <= 1):
        raise AssertionError("Invalid shoulder maximum overhang")
    values = np.asarray(values, dtype=float)
    if values.size == 0:
        return values.reshape(0, 3, 3)
    if values.ndim != 3 or values.shape[1:] != (3, 3) or not np.isfinite(values).all():
        raise AssertionError("Shoulder coverage requires finite Nx3x3 triangle positions")
    if (np.any(values[:, :, :2] < -max_overhang - TOLERANCE)
            or np.any(values[:, :, :2] > 1 + max_overhang + TOLERANCE)):
        raise AssertionError("Shoulder coverage geometry escapes the unit XY footprint or allowed overhang")
    return values


def _side_coordinates(point, side):
    x, y = point[:2]
    return ((x, y), (y, 1 - x), (1 - x, 1 - y), (1 - y, x))[side]


def _slice_segments(triangles, side, height):
    """Return (along-side, inward-depth) line segments in the slice plane."""
    segments = []
    for triangle in triangles:
        delta = triangle[:, 2] - height
        if np.min(delta) > TOLERANCE or np.max(delta) < -TOLERANCE:
            continue
        points = [point.copy() for point, distance in zip(triangle, delta) if abs(distance) <= TOLERANCE]
        for a, b in combinations(range(3), 2):
            if (delta[a] < -TOLERANCE and delta[b] > TOLERANCE
                    or delta[b] < -TOLERANCE and delta[a] > TOLERANCE):
                fraction = delta[a] / (delta[a] - delta[b])
                points.append(triangle[a] + fraction * (triangle[b] - triangle[a]))
        unique = []
        for point in points:
            if not any(np.linalg.norm(point - other) <= TOLERANCE for other in unique):
                unique.append(point)
        # A coplanar triangle contributes all of its edges. Otherwise the
        # horizontal intersection is a single segment; duplicate segments at
        # shared triangulation edges are harmless when taking a union/minimum.
        for a, b in combinations(unique, 2):
            segments.append((_side_coordinates(a, side), _side_coordinates(b, side)))
    return segments


def _near_border_interval(a, b, max_inset, station_range, max_overhang):
    start, end = station_range
    u0, d0 = a
    u1, d1 = b
    if abs(d1 - d0) <= TOLERANCE:
        if d0 < -max_overhang - TOLERANCE or d0 > max_inset + TOLERANCE:
            return None
        low, high = min(u0, u1), max(u0, u1)
    else:
        first = (-max_overhang - TOLERANCE - d0) / (d1 - d0)
        last = (max_inset + TOLERANCE - d0) / (d1 - d0)
        first, last = max(0.0, min(first, last)), min(1.0, max(first, last))
        if first > last:
            return None
        left, right = u0 + first * (u1 - u0), u0 + last * (u1 - u0)
        low, high = min(left, right), max(left, right)
    low, high = max(start, low), min(end, high)
    return (float(low), float(high)) if high - low > TOLERANCE else None


def _merge_intervals(intervals):
    merged = []
    for start, end in sorted(intervals):
        if merged and start <= merged[-1][1] + TOLERANCE:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def _frontmost_depth(segments, station, max_overhang):
    result = None
    for (u0, d0), (u1, d1) in segments:
        if station < min(u0, u1) - TOLERANCE or station > max(u0, u1) + TOLERANCE:
            continue
        if abs(u1 - u0) <= TOLERANCE:
            if abs(station - u0) > TOLERANCE:
                continue
            depth = min(d0, d1)
        else:
            fraction = min(1.0, max(0.0, (station - u0) / (u1 - u0)))
            depth = d0 + fraction * (d1 - d0)
        if depth >= -max_overhang - TOLERANCE:
            result = depth if result is None else min(result, depth)
    return None if result is None else float(result)


def measure_shoulder_slice(triangles, side, height, *, max_inset=.055,
                           station_range=(.01, .99), samples=101, max_overhang=0):
    """Measure exact covered intervals and sampled frontmost ray depths.

    Coverage uses the continuous interval union, so tiny spikes cannot pass
    merely by coinciding with sample rays. Largest gaps are reported in tile
    units; no independent maximum-gap threshold is imposed here.
    """
    triangles = _triangles(triangles, max_overhang)
    if side not in range(4) or not np.isfinite(height):
        raise AssertionError("Invalid shoulder side or slice height")
    if not (np.isfinite(max_inset) and 0 <= max_inset <= 1):
        raise AssertionError("Invalid shoulder maximum inset")
    if len(station_range) != 2 or not (0 <= station_range[0] < station_range[1] <= 1):
        raise AssertionError("Invalid shoulder station range")
    if not isinstance(samples, int) or samples < 2:
        raise AssertionError("Shoulder coverage needs at least two sample rays")
    segments = _slice_segments(triangles, side, height)
    intervals = []
    for a, b in segments:
        interval = _near_border_interval(a, b, max_inset, station_range, max_overhang)
        if interval is not None:
            intervals.append(interval)
    merged = _merge_intervals(intervals)
    gaps = []
    cursor = float(station_range[0])
    for start, end in merged:
        if start > cursor + TOLERANCE:
            gaps.append([cursor, start])
        cursor = max(cursor, end)
    if cursor < station_range[1] - TOLERANCE:
        gaps.append([cursor, float(station_range[1])])
    covered = sum(end - start for start, end in merged)
    stations = np.linspace(*station_range, samples)
    depths = [_frontmost_depth(segments, float(station), max_overhang) for station in stations]
    covered_samples = sum(depth is not None and depth <= max_inset + TOLERANCE for depth in depths)
    return {
        "side": SIDE_NAMES[side], "side_index": side, "height": float(height),
        "maximum_inset": float(max_inset), "maximum_overhang": float(max_overhang),
        "station_range": [float(value) for value in station_range],
        "covered_intervals": merged, "uncovered_intervals": gaps,
        "covered_length": float(covered), "coverage_fraction": float(covered / (station_range[1] - station_range[0])),
        "largest_gap": float(max((end - start for start, end in gaps), default=0.0)),
        "sample_count": samples, "covered_sample_count": covered_samples,
        "sampled_coverage_fraction": covered_samples / samples,
        "sample_stations": stations.tolist(), "frontmost_depths": depths,
    }


def audit_shoulder_coverage(triangles, exposed_mask, lip_height, label="shoulders", *,
                            minimum_coverage=.8, max_inset=.055, slice_offsets=(.02, .05),
                            station_range=(.01, .99), samples=101, max_overhang=0):
    """Require near-cap shoulders over each exposed side at both depth slices."""
    triangles = _triangles(triangles, max_overhang)
    if not isinstance(exposed_mask, int) or not 0 <= exposed_mask <= 15:
        raise AssertionError("Invalid shoulder exposure mask")
    if not (np.isfinite(lip_height) and np.isfinite(minimum_coverage) and 0 <= minimum_coverage <= 1):
        raise AssertionError("Invalid shoulder lip height or minimum coverage")
    if not slice_offsets or not all(np.isfinite(value) and value > 0 for value in slice_offsets):
        raise AssertionError("Shoulder slice offsets must be finite and positive")
    reports = []
    for side in range(4):
        if not exposed_mask & (1 << side):
            continue
        for offset in slice_offsets:
            report = measure_shoulder_slice(triangles, side, lip_height - offset,
                                             max_inset=max_inset, station_range=station_range, samples=samples,
                                             max_overhang=max_overhang)
            if report["coverage_fraction"] + TOLERANCE < minimum_coverage:
                raise AssertionError(
                    f"{label}: {SIDE_NAMES[side]} shoulder coverage at Z={lip_height-offset:.6f} "
                    f"is {report['coverage_fraction']:.1%}, below {minimum_coverage:.1%}; "
                    f"largest uncovered interval {report['largest_gap']:.6f} tile units")
            reports.append(report)
    return {"minimum_coverage": float(minimum_coverage), "maximum_inset": float(max_inset),
            "maximum_overhang": float(max_overhang),
            "slice_offsets": [float(value) for value in slice_offsets], "exposed_side_mask": exposed_mask,
            "slice_checks": len(reports), "slices": reports,
            "minimum_measured_coverage": min((report["coverage_fraction"] for report in reports), default=1.0),
            "largest_gap": max((report["largest_gap"] for report in reports), default=0.0)}
