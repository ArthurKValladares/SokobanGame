#!/usr/bin/env python3
"""Independently audit editable floating-island ground bodies and native exports.

Existing top bands, cap tables, and textures are inputs, never regenerated.
The audit checks actual triangle data, attachment frames, every rotated exposure
mask, mixed-style joins, and detached rock topology. Evidence fixtures are
written only below the ignored out directory.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image

import validate_ground_modules as native
from pbr_gltf import Document

ROOT = native.ROOT
MODELS = ROOT / "assets/custom/models/ground_modules"
FAMILIES = (("island", 15), ("end", 11), ("strip", 5), ("corner", 3), ("edge", 1), ("interior", 0))
REMODEL_HEIGHT = .78
CONNECTION_HEIGHT = -.34
MAX_OVERHANG = .04
ACCEPTED_OVERHANG = .12
AFFINE_TOLERANCE = 3e-7
ROCK_GAP = .002
ROCK_MAXIMUM_Z = .777
TOLERANCE = native.TOLERANCE
CUT_TOLERANCE = .0005 + TOLERANCE


def vertex_key(point):
    return native.quantized_point(point)


def texture_coordinates(document):
    node = document.data["nodes"][0]
    return [{name: document.accessor(accessor) for name, accessor in primitive["attributes"].items()
             if name.startswith("TEXCOORD_")}
            for primitive in document.data["meshes"][node["mesh"]]["primitives"]]


def clipped_top_band(primitives, streams, height=REMODEL_HEIGHT):
    output = []
    for index, primitive in enumerate(primitives):
        for indices in primitive.triangles:
            polygon = [{"point": primitive.points[vertex], "normal": primitive.normals[vertex],
                        "uv": {name: values[vertex] for name, values in streams[index].items()}}
                       for vertex in indices]
            clipped = []
            for a, b in zip(polygon, polygon[1:] + polygon[:1]):
                inside_a, inside_b = a["point"][2] >= height, b["point"][2] >= height
                if inside_a:
                    clipped.append(a)
                if inside_a != inside_b:
                    t = (height-a["point"][2])/(b["point"][2]-a["point"][2])
                    clipped.append({"point": a["point"]+t*(b["point"]-a["point"]),
                                    "normal": a["normal"]+t*(b["normal"]-a["normal"]),
                                    "uv": {name: a["uv"][name]+t*(b["uv"][name]-a["uv"][name]) for name in a["uv"]}})
            for offset in range(1, len(clipped)-1):
                triangle = [clipped[0], clipped[offset], clipped[offset+1]]
                points = np.asarray([vertex["point"] for vertex in triangle])
                area = np.linalg.norm(np.cross(points[1]-points[0], points[2]-points[0]))/2
                if area <= 1e-11:
                    continue
                output.append({"material": primitive.material.get("name", ""), "points": points,
                               "normals": np.asarray([vertex["normal"] for vertex in triangle]),
                               "uv": {name: np.asarray([vertex["uv"][name] for vertex in triangle]) for name in triangle[0]["uv"]},
                               "area": area, "strict_normals": primitive.points[indices, 2].min() > height+CUT_TOLERANCE})
    return output


def band_samples(surface):
    weights = np.asarray([[1, 0, 0], [0, 1, 0], [0, 0, 1], [1/3]*3, [.5, .5, 0], [0, .5, .5], [.5, 0, .5]])
    groups = {}
    for triangle in surface:
        key = (triangle["material"], tuple(sorted(triangle["uv"])))
        group = groups.setdefault(key, {"points": [], "normals": [], "strict": [], "uv": {name: [] for name in key[1]}})
        group["points"].append(weights @ triangle["points"])
        group["normals"].append(weights @ triangle["normals"])
        group["strict"].extend([triangle["strict_normals"]]*len(weights))
        for name in key[1]:
            group["uv"][name].append(weights @ triangle["uv"][name])
    for group in groups.values():
        group["points"] = np.concatenate(group["points"])
        group["normals"] = np.concatenate(group["normals"])
        group["strict"] = np.asarray(group["strict"])
        group["uv"] = {name: np.concatenate(values) for name, values in group["uv"].items()}
    return groups


def require_band_coverage(samples, reference, label):
    checked = 0
    for key, values in samples.items():
        points = values["points"]
        covered = np.zeros(len(points), dtype=bool)
        for triangle in reference:
            if (triangle["material"], tuple(sorted(triangle["uv"]))) != key:
                continue
            p = triangle["points"]
            selected = np.where(~covered & np.all(points >= p.min(axis=0)-CUT_TOLERANCE, axis=1)
                                & np.all(points <= p.max(axis=0)+CUT_TOLERANCE, axis=1))[0]
            if not len(selected):
                continue
            a, b, delta = p[1]-p[0], p[2]-p[0], points[selected]-p[0]
            cross = np.cross(a, b)
            denominator = cross@cross
            u = (np.cross(delta, b)@cross)/denominator
            v = (np.cross(a, delta)@cross)/denominator
            candidates = [np.column_stack((1-u-v, u, v))]
            # At the bisect plane, merging close vertices may shift a tiny
            # triangle by .0005 in position without shifting its UVs. A fixed
            # barycentric margin scales badly with those tiny triangles. Test
            # physical surface distance and each UV parameterization instead.
            for name in key[1]:
                uv = triangle["uv"][name]
                ua, ub = uv[1]-uv[0], uv[2]-uv[0]
                determinant = ua[0]*ub[1]-ua[1]*ub[0]
                if abs(determinant) < 1e-14:
                    continue
                delta_uv = values["uv"][name][selected]-uv[0]
                u = (delta_uv[:, 0]*ub[1]-delta_uv[:, 1]*ub[0])/determinant
                v = (ua[0]*delta_uv[:, 1]-ua[1]*delta_uv[:, 0])/determinant
                candidates.append(np.column_stack((1-u-v, u, v)))
            good = np.zeros(len(selected), dtype=bool)
            uv_tolerance = np.where(values["strict"][selected], 1e-4, .002)
            distance = np.full(len(selected), np.inf)
            for start, end in zip(p, np.roll(p, -1, axis=0)):
                edge = end-start
                t = np.clip((points[selected]-start)@edge/(edge@edge), 0, 1)
                distance = np.minimum(distance, np.linalg.norm(start+t[:, None]*edge-points[selected], axis=1))
            interior = np.all(candidates[0] >= 0, axis=1)
            distance[interior] = np.minimum(distance[interior], np.linalg.norm(candidates[0][interior]@p-points[selected][interior], axis=1))
            for weights in candidates:
                candidate = distance < CUT_TOLERANCE
                candidate &= np.linalg.norm(weights @ p-points[selected], axis=1) < CUT_TOLERANCE
                for name in key[1]:
                    candidate &= np.max(np.abs(weights @ triangle["uv"][name]-values["uv"][name][selected]), axis=1) < uv_tolerance
                if triangle["strict_normals"]:
                    candidate &= ~values["strict"][selected] | (np.max(np.abs(weights @ triangle["normals"]-values["normals"][selected]), axis=1) < 2e-4)
                good |= candidate
            covered[selected[good]] = True
        missing = np.flatnonzero(~covered)
        native.require(not len(missing), f"{label}: preserved top-band position, UV or normal coverage changed "
                       f"({len(missing)} samples in {key[0]}; first {points[missing[0]].tolist() if len(missing) else []})")
        checked += len(points)
    return checked


def audit_top_band(primitives, original_primitives, streams, original_streams, label):
    def authored_vertices(bodies, uv_streams):
        return {(primitive.material.get("name", ""), vertex_key(point),
                 tuple((name, *np.rint(values[index]/TOLERANCE).astype(int)) for name, values in sorted(uv_streams[body_index].items())))
                for body_index, primitive in enumerate(bodies) for index, point in enumerate(primitive.points)
                if point[2] > REMODEL_HEIGHT+CUT_TOLERANCE}
    native.require(authored_vertices(original_primitives, original_streams) <= authored_vertices(primitives, streams),
                   f"{label}: original authored upper vertex position or UV changed")
    actual = clipped_top_band(primitives, streams)
    expected = clipped_top_band(original_primitives, original_streams)
    areas = lambda surface: {name: sum(triangle["area"] for triangle in surface if triangle["material"] == name)
                             for name in {triangle["material"] for triangle in surface}}
    aa, bb = areas(actual), areas(expected)
    native.require(aa.keys() == bb.keys() and all(abs(aa[name]-bb[name]) < max(TOLERANCE, .005*bb[name]) for name in aa),
                   f"{label}: top-band surface area or material coverage changed")
    checked = require_band_coverage(band_samples(actual), expected, label)
    checked += require_band_coverage(band_samples(expected), actual, label)
    return {"top_band_height": REMODEL_HEIGHT, "bidirectional_position_uv_normal_samples": checked,
            "original_authored_upper_vertices_preserved": True,
            "maximum_material_area_change": max((abs(aa[name]-bb[name]) for name in aa), default=0),
            "cut_plane_cleanup_tolerance": CUT_TOLERANCE}


def components_for_faces(faces):
    """Weld position splits before finding connected rock components."""
    vertices, points, triangles = {}, [], []
    for face in faces:
        triangle = []
        for point in face:
            key = vertex_key(point)
            if key not in vertices:
                vertices[key] = len(points)
                points.append(point)
            triangle.append(vertices[key])
        triangles.append(triangle)
    points = np.asarray(points, dtype=float)
    triangles = np.asarray(triangles, dtype=np.int64)
    by_vertex = {index: [] for index in range(len(points))}
    for index, triangle in enumerate(triangles):
        for vertex in triangle:
            by_vertex[vertex].append(index)
    pending, result = set(range(len(triangles))), []
    while pending:
        found, queue = set(), [pending.pop()]
        while queue:
            index = queue.pop()
            if index in found:
                continue
            found.add(index)
            pending.discard(index)
            for vertex in triangles[index]:
                queue.extend(neighbor for neighbor in by_vertex[vertex] if neighbor not in found)
        indices = np.asarray(sorted(found), dtype=np.int64)
        used = np.unique(triangles[indices])
        result.append({"points": points[used], "triangles": triangles[indices], "faces": faces[indices]})
    return sorted(result, key=lambda component: len(component["triangles"]), reverse=True)


def require_closed_rock(component, label):
    edges = Counter()
    directed = Counter()
    for triangle in component["triangles"]:
        for a, b in zip(triangle, np.roll(triangle, -1)):
            edges[tuple(sorted((int(a), int(b))))] += 1
            directed[(int(a), int(b))] += 1
    native.require(all(count == 2 for count in edges.values()), f"{label}: floating rock is not closed")
    native.require(all(directed[(b, a)] == count for (a, b), count in directed.items()),
                   f"{label}: floating rock winding is inconsistent")


def interval_coverage(intervals, lo=0, hi=1):
    values = sorted((max(lo, min(a, b)), min(hi, max(a, b))) for a, b in intervals)
    total, end = 0.0, lo
    for a, b in values:
        if b > max(a, end):
            total += b-max(a, end)
            end = b
    return total


def integration_metrics(faces, exposed):
    spans = (faces[:, :, 2].min(axis=1) < -TOLERANCE) & (faces[:, :, 2].max(axis=1) > .05)
    areas = np.linalg.norm(np.cross(faces[:, 1]-faces[:, 0], faces[:, 2]-faces[:, 0]), axis=1)/2
    coverage = {}
    for side in range(4):
        if not exposed & (1 << side):
            continue
        axis, plane = (1 if side % 2 == 0 else 0), (1 if side in (1, 2) else 0)
        intervals = []
        for face in faces:
            for a, b in zip(face, np.roll(face, -1, axis=0)):
                if max(abs(a[2]), abs(b[2])) < TOLERANCE and max(abs(a[axis]-plane), abs(b[axis]-plane)) < TOLERANCE:
                    intervals.append((a[1-axis], b[1-axis]))
        coverage[side] = interval_coverage(intervals)
    return {"triangles_spanning_logical_base": int(spans.sum()), "wall_area_spanning_logical_base": float(areas[spans].sum()),
            "exposed_z0_border_edge_coverage": coverage}


def audit_integration(faces, exposed, label):
    report = integration_metrics(faces, exposed)
    if exposed:
        native.require(report["wall_area_spanning_logical_base"] > .10*exposed.bit_count(),
                       f"{label}: rock walls remain pasted onto a z=0 collar")
        native.require(all(value < .2 for value in report["exposed_z0_border_edge_coverage"].values()),
                       f"{label}: continuous exposed z=0 mesh seam remains")
    return report


def floor_disk(faces, label):
    native.require(len(faces) > 0, f"{label}: missing authored floor disk")
    cross = np.cross(faces[:, 1]-faces[:, 0], faces[:, 2]-faces[:, 0])
    native.require(np.all(cross[:, 2] < -1e-10), f"{label}: underside floor projection folds upward or degenerates")
    area = float(-cross[:, 2].sum()/2)
    native.require(.15 < area <= 1+TOLERANCE, f"{label}: invalid tapered floor footprint area")
    components = components_for_faces(faces)
    native.require(len(components) == 1, f"{label}: underside floor is disconnected")
    edges, directed = Counter(), Counter()
    vertices = set()
    for face in faces:
        keys = [vertex_key(point) for point in face]
        vertices.update(keys)
        for a, b in zip(keys, keys[1:]+keys[:1]):
            edges[tuple(sorted((a, b)))] += 1
            directed[(a, b)] += 1
    native.require(all(count in (1, 2) for count in edges.values()), f"{label}: nonmanifold underside floor edge")
    boundary = {}
    for edge, count in edges.items():
        if count == 2:
            native.require(directed[edge] == 1 and directed[edge[::-1]] == 1,
                           f"{label}: underside floor winding disagrees")
        else:
            a, b = edge
            boundary.setdefault(a, set()).add(b)
            boundary.setdefault(b, set()).add(a)
    native.require(len(vertices)-len(edges)+len(faces) == 1 and boundary
                   and all(len(neighbors) == 2 for neighbors in boundary.values()),
                   f"{label}: underside floor contains a hole or broken boundary")
    seen, pending = set(), [next(iter(boundary))]
    while pending:
        point = pending.pop()
        if point not in seen:
            seen.add(point)
            pending.extend(boundary[point]-seen)
    native.require(len(seen) == len(boundary), f"{label}: underside floor has multiple boundary loops")
    boundary_edges = [(np.asarray(a)*TOLERANCE, np.asarray(b)*TOLERANCE) for (a, b), count in edges.items() if count == 1]
    return components[0], boundary_edges, area


def hidden_floor_trace(edges, side, label):
    axis, plane = (1 if side % 2 == 0 else 0), (1 if side in (1, 2) else 0)
    selected = [(a, b) for a, b in edges if max(abs(a[axis]-plane), abs(b[axis]-plane)) < TOLERANCE]
    native.require(selected and all(max(abs(a[2]-CONNECTION_HEIGHT), abs(b[2]-CONNECTION_HEIGHT)) < TOLERANCE for a, b in selected),
                   f"{label}: hidden floor connector is not flat at the shared height")
    native.require(abs(interval_coverage([(a[1-axis], b[1-axis]) for a, b in selected])-1) < TOLERANCE,
                   f"{label}: hidden floor connector leaves a gap")
    return selected


def audit_core_floor_attachment(body, boundary, exposed, label):
    indices = sorted(set(body["faceGroups"]["IslandCore"])-set(body["faceGroups"]["IslandUnderside"]))
    faces = np.asarray(body["points"])[np.asarray(body["polygons"])[indices]]
    edges = {tuple(sorted((vertex_key(a), vertex_key(b)))) for face in faces
             for a, b in zip(face, np.roll(face, -1, axis=0))}
    attached = 0
    for a, b in boundary:
        hidden = any(not exposed & (1 << side)
                     and max(abs(a[1 if side % 2 == 0 else 0]-(1 if side in (1, 2) else 0)),
                             abs(b[1 if side % 2 == 0 else 0]-(1 if side in (1, 2) else 0))) < TOLERANCE
                     for side in range(4))
        if not hidden:
            native.require(tuple(sorted((vertex_key(a), vertex_key(b)))) in edges,
                           f"{label}: core floor does not attach to its exposed wall boundary")
            attached += 1
    return attached


def triangle_key(face):
    vertices = [vertex_key(point) for point in face]
    return min(tuple(vertices[offset:]+vertices[:offset]) for offset in range(3))


def compressed_rock_points(body, exposed):
    """The specified V3-to-V4 map, applied only to selected rock vertices."""
    points = np.asarray(body["points"], dtype=float).copy()
    selected = sorted({int(index) for name, indices in body["vertexGroups"].items()
                       if re.fullmatch(r"(?:BoulderMass|FloatingRock)\d+", name) for index in indices})
    factors = []
    for axis, low_side, high_side in ((0, 3, 1), (1, 0, 2)):
        old_low = -ACCEPTED_OVERHANG if exposed & (1 << low_side) else 0
        old_high = 1+ACCEPTED_OVERHANG if exposed & (1 << high_side) else 1
        new_low = -MAX_OVERHANG if exposed & (1 << low_side) else 0
        new_high = 1+MAX_OVERHANG if exposed & (1 << high_side) else 1
        factor = (new_high-new_low)/(old_high-old_low)
        points[selected, axis] = new_low+(points[selected, axis]-old_low)*factor
        factors.append(factor)
    return points, selected, factors


def audit_accepted_shape_source(body, accepted, exposed, label):
    for field in ("vertices", "faces", "polygons", "faceGroups", "vertexGroups"):
        native.require(body[field] == accepted[field], f"{label}: accepted {field} changed during thinning")
    points, old = np.asarray(body["points"]), np.asarray(accepted["points"])
    expected, selected, factors = compressed_rock_points(accepted, exposed)
    fixed = sorted(set(range(len(old)))-set(selected))
    native.require(np.array_equal(points[:, 2], old[:, 2]), f"{label}: accepted rock/core Z values changed")
    native.require(np.array_equal(points[fixed], old[fixed]), f"{label}: accepted core or cap positions changed")
    error = float(np.abs(points-expected).max())
    native.require(error <= AFFINE_TOLERANCE, f"{label}: rock XY differs from its exact accepted affine map ({error})")
    # GLB vertices split at UV/material seams. Source positions identify the
    # same corners even if the exporter reorders those split vertices.
    mapping = {}
    for a, b in zip(old, points):
        key, value = vertex_key(a), vertex_key(b)
        native.require(key not in mapping or mapping[key] == value,
                       f"{label}: ambiguous accepted source corner correspondence")
        mapping[key] = value
    return mapping, {"unchanged_topology_role_ownership_and_vertex_groups": True,
                     "all_z_values_and_nonrock_positions_identical": True,
                     "rock_vertices": len(selected), "axis_scale_factors": factors,
                     "maximum_affine_position_error": error}


def corner_signatures(primitives, streams, mapping=None):
    signatures = Counter()
    for primitive, coordinates in zip(primitives, streams):
        for triangle in primitive.triangles:
            corners = []
            for index in triangle:
                point = vertex_key(primitive.points[index])
                if mapping is not None:
                    native.require(point in mapping, "Accepted export corner is absent from its source snapshot")
                    point = mapping[point]
                corners.append((point, tuple((name, tuple(float(value) for value in values[index]))
                                              for name, values in sorted(coordinates.items()))))
            canonical = min(tuple(corners[offset:]+corners[:offset]) for offset in range(3))
            signatures[(primitive.material.get("name", ""), canonical)] += 1
    return signatures


def audit_accepted_shape_export(primitives, accepted_primitives, streams, accepted_streams,
                                mapping, document, accepted_document, label):
    native.require(corner_signatures(primitives, streams)
                   == corner_signatures(accepted_primitives, accepted_streams, mapping),
                   f"{label}: accepted material/UV corner streams or triangle topology changed")
    for field in ("materials", "textures", "images", "samplers"):
        native.require(document.data.get(field, []) == accepted_document.data.get(field, []),
                       f"{label}: accepted PBR {field} changed")
    return {"all_material_uv_corner_streams_identical": True, "all_pbr_material_and_texture_metadata_identical": True}


def source_roles(body, label):
    points = np.asarray(body["points"], dtype=float)
    polygons = np.asarray(body["polygons"], dtype=np.int64)
    native.require(polygons.ndim == 2 and polygons.shape[1] == 3,
                   f"{label}: source role polygons must be triangulated")
    groups = body.get("faceGroups", {})
    native.require(all(name in groups for name in ("IslandCore", "IslandUnderside", "IslandRockWalls")),
                   f"{label}: editable core/floor/wall face ownership is missing")
    native.require(all(len(indices) == len(set(indices)) and all(0 <= index < len(polygons) for index in indices)
                       for indices in groups.values()), f"{label}: invalid role face indices")
    native.require(set(groups["IslandUnderside"]) <= set(groups["IslandCore"])
                   and set(groups["IslandRockWalls"]) <= set(groups["IslandCore"]),
                   f"{label}: floor or wall lies outside its core role")
    primary = ["IslandCore"]+[name for name in groups if re.fullmatch(r"(?:BoulderMass|FloatingRock)\d+", name)]
    coverage = Counter(index for name in primary for index in groups[name])
    native.require(set(coverage) == set(range(len(polygons))) and all(count == 1 for count in coverage.values()),
                   f"{label}: source faces have missing or overlapping primary roles")
    return {name: points[polygons[indices]] for name, indices in groups.items()}


def unique_axes(values):
    lengths = np.linalg.norm(values, axis=1)
    values = values[lengths > 1e-8]/lengths[lengths > 1e-8, None]
    if not len(values):
        return values
    pivot = np.argmax(np.abs(values), axis=1)
    values *= np.where(values[np.arange(len(values)), pivot] < 0, -1, 1)[:, None]
    _, indices = np.unique(np.rint(values/1e-6).astype(np.int64), axis=0, return_index=True)
    return values[np.sort(indices)]


def mesh_edges(faces):
    edges = {}
    for face in faces:
        for a, b in zip(face, np.roll(face, -1, axis=0)):
            edges.setdefault(tuple(sorted((vertex_key(a), vertex_key(b)))), (a, b))
    return np.asarray(list(edges.values()))


def convex_rock(faces, label):
    native.require(len(faces) >= 4, f"{label}: rock has no closed volume")
    components = components_for_faces(faces)
    native.require(len(components) == 1, f"{label}: rock role contains disconnected pieces")
    require_closed_rock(components[0], label)
    points = components[0]["points"]
    cross = np.cross(faces[:, 1]-faces[:, 0], faces[:, 2]-faces[:, 0])
    areas = np.linalg.norm(cross, axis=1)/2
    native.require(np.all(areas > 1e-11), f"{label}: degenerate rock facet")
    normals = cross/(2*areas[:, None])
    heights = np.einsum("ij,ij->i", normals, faces[:, 0])
    native.require(np.all(points@normals.T <= heights+TOLERANCE), f"{label}: rock is concave or wound inward")
    volume = float(np.einsum("ij,ij->i", faces[:, 0], np.cross(faces[:, 1], faces[:, 2])).sum()/6)
    native.require(volume > 1e-6, f"{label}: rock has no outward volumetric mass")
    eigenvalues = np.linalg.eigvalsh(np.cov(points.T))
    aspect = float(np.sqrt(max(0, eigenvalues[0])/eigenvalues[-1]))
    native.require(aspect >= .12, f"{label}: rock is a thin plate or stretched spike")
    edges = mesh_edges(faces)
    return {"faces": faces, "points": points, "normals": normals, "heights": heights,
            "edges": edges, "edge_axes": unique_axes(edges[:, 1]-edges[:, 0]),
            "minimum": points.min(axis=0), "maximum": points.max(axis=0), "volume": volume,
            "principal_axis_ratio": aspect, "facets": len(faces),
            "median_facet_area": float(np.median(areas)), "maximum_facet_area": float(areas.max())}


def convex_gap(a, b):
    """Complete convex-polyhedron SAT; positive means a real separating gap."""
    crosses = np.cross(a["edge_axes"][:, None, :], b["edge_axes"][None, :, :]).reshape(-1, 3)
    axes = unique_axes(np.concatenate((a["normals"], b["normals"], crosses)))
    ap, bp = a["points"]@axes.T, b["points"]@axes.T
    return float(np.maximum(ap.min(axis=0)-bp.max(axis=0), bp.min(axis=0)-ap.max(axis=0)).max())


def core_intersection(rock, core_faces, margin=1e-5):
    """Clip core triangles into a slightly shrunken convex rock volume."""
    candidate = (np.all(core_faces.max(axis=1) >= rock["minimum"]+margin, axis=1)
                 & np.all(core_faces.min(axis=1) <= rock["maximum"]-margin, axis=1))
    for face in core_faces[candidate]:
        polygon = list(face)
        for normal, height in zip(rock["normals"], rock["heights"]-margin):
            clipped = []
            for a, b in zip(polygon, polygon[1:]+polygon[:1]):
                da, db = a@normal-height, b@normal-height
                if da <= 0:
                    clipped.append(a)
                if (da <= 0) != (db <= 0):
                    clipped.append(a+(b-a)*da/(da-db))
            polygon = clipped
            if len(polygon) < 3:
                break
        if len(polygon) >= 3:
            area = np.linalg.norm(sum((np.cross(polygon[index]-polygon[0], polygon[index+1]-polygon[0])
                                       for index in range(1, len(polygon)-1)), np.zeros(3)))/2
            if area > 1e-9:
                return float(area)
    return 0.0


def point_surface_distance(points, faces):
    best = np.inf
    a, b = faces[:, 1]-faces[:, 0], faces[:, 2]-faces[:, 0]
    normal = np.cross(a, b)
    denominator = np.einsum("ij,ij->i", normal, normal)
    for offset in range(0, len(points), 32):
        selected = points[offset:offset+32]
        delta = selected[:, None, :]-faces[None, :, 0]
        u = np.einsum("pij,ij->pi", np.cross(delta, b), normal)/denominator
        v = np.einsum("pij,ij->pi", np.cross(a, delta), normal)/denominator
        interior = (u >= 0) & (v >= 0) & (u+v <= 1)
        squared = np.einsum("pij,ij->pi", delta, normal)**2/denominator
        if np.any(interior):
            best = min(best, float(squared[interior].min()))
        for index in range(3):
            start, end = faces[:, index], faces[:, (index+1) % 3]
            edge = end-start
            delta = selected[:, None, :]-start
            t = np.clip(np.einsum("pij,ij->pi", delta, edge)/np.einsum("ij,ij->i", edge, edge), 0, 1)
            difference = delta-t[:, :, None]*edge
            best = min(best, float(np.einsum("pij,pij->pi", difference, difference).min()))
    return float(np.sqrt(max(0, best)))


def mesh_surface_distance(a_faces, b_faces):
    """Exact disjoint triangle-surface distance, including skew edge interiors."""
    a_points = np.unique(a_faces.reshape(-1, 3), axis=0)
    b_points = np.unique(b_faces.reshape(-1, 3), axis=0)
    best = min(point_surface_distance(a_points, b_faces), point_surface_distance(b_points, a_faces))
    a_edges, b_edges = mesh_edges(a_faces), mesh_edges(b_faces)
    v = b_edges[:, 1]-b_edges[:, 0]
    cc = np.einsum("ij,ij->i", v, v)
    for offset in range(0, len(a_edges), 64):
        edges = a_edges[offset:offset+64]
        u, w = edges[:, 1]-edges[:, 0], edges[:, None, 0]-b_edges[None, :, 0]
        aa = np.einsum("ij,ij->i", u, u)[:, None]
        bb = u@v.T
        dd, ee = np.einsum("pij,pj->pi", w, u), np.einsum("pij,ij->pi", w, v)
        denominator = aa*cc-bb*bb
        valid = denominator > 1e-15
        safe = np.where(valid, denominator, 1)
        s, t = (bb*ee-cc*dd)/safe, (aa*ee-bb*dd)/safe
        valid &= (s >= 0) & (s <= 1) & (t >= 0) & (t <= 1)
        difference = w+s[:, :, None]*u[:, None, :]-t[:, :, None]*v
        if np.any(valid):
            best = min(best, float(np.sqrt(np.einsum("pij,pij->pi", difference, difference)[valid].min())))
    return best


def point_inside_core(point, faces):
    # Three independent rays avoid accepting one accidental intersection with
    # an open authored panel as proof of containment.
    outcomes = []
    for direction in (np.asarray([.8123, .2347, .5356]), np.asarray([-.3171, .9137, .2873]), np.asarray([.1931, -.2473, .9491])):
        edge1, edge2 = faces[:, 1]-faces[:, 0], faces[:, 2]-faces[:, 0]
        h = np.cross(direction, edge2)
        denominator = np.einsum("ij,ij->i", edge1, h)
        valid = np.abs(denominator) > 1e-10
        inverse = np.zeros(len(faces))
        inverse[valid] = 1/denominator[valid]
        delta = point-faces[:, 0]
        u = inverse*np.einsum("ij,ij->i", delta, h)
        q = np.cross(delta, edge1)
        v = inverse*(q@direction)
        t = inverse*np.einsum("ij,ij->i", edge2, q)
        hits = t[valid & (u >= 0) & (v >= 0) & (u+v <= 1) & (t > TOLERANCE)]
        outcomes.append(len(np.unique(np.rint(hits/TOLERANCE))) % 2 == 1)
    return all(outcomes)


def audit_lower_geometry(primitives, exposed, label, body):
    faces = np.concatenate([primitive.points[primitive.triangles] for primitive in primitives])
    points = faces.reshape(-1, 3)
    roles = source_roles(body, label)
    native.require(Counter(triangle_key(face) for face in faces)
                   == Counter(triangle_key(face) for face in np.asarray(body["points"])[np.asarray(body["polygons"])]),
                   f"{label}: source/export triangle ownership differs")
    lower = points[points[:, 2] < REMODEL_HEIGHT-CUT_TOLERANCE]
    minimum = np.asarray([-MAX_OVERHANG if exposed & 8 else 0, -MAX_OVERHANG if exposed & 1 else 0])
    maximum = np.asarray([1+MAX_OVERHANG if exposed & 2 else 1, 1+MAX_OVERHANG if exposed & 4 else 1])
    native.require(np.all(lower[:, :2] >= minimum-TOLERANCE) and np.all(lower[:, :2] <= maximum+TOLERANCE)
                   and points[:, 2].min() >= -1-TOLERANCE, f"{label}: boulder geometry exceeds its exposed-side visual envelope")
    native.require(points[:, 2].min() < -.4, f"{label}: missing broad rock depth")
    flat = faces[np.all(np.abs(faces[:, :, 2]) < TOLERANCE, axis=1)]
    flat_area = sum(abs(np.cross(face[1]-face[0], face[2]-face[0])[2])/2 for face in flat)
    native.require(flat_area < .15, f"{label}: old flat bottom remains")
    integration = integration_metrics(faces, exposed)
    floor_faces = roles["IslandUnderside"]
    floor, boundary, area = floor_disk(floor_faces, label)
    valence = Counter(vertex_key(point) for face in floor_faces for point in face)
    maximum_valence = max(valence.values())
    for side in range(4):
        if not exposed & (1 << side):
            hidden_floor_trace(boundary, side, label)
    attached_floor_edges = audit_core_floor_attachment(body, boundary, exposed, label)
    masses = {name: convex_rock(role, f"{label} {name}") for name, role in roles.items() if re.fullmatch(r"BoulderMass\d+", name)}
    floating = {name: convex_rock(role, f"{label} {name}") for name, role in roles.items() if re.fullmatch(r"FloatingRock\d+", name)}
    native.require(len(masses) >= 1, f"{label}: missing volumetric attached boulder masses")
    native.require(all(rock["maximum"][2] <= ROCK_MAXIMUM_Z+TOLERANCE for rock in list(masses.values())+list(floating.values())),
                   f"{label}: new rock geometry enters the preserved upper band")
    native.require(1 <= len(floating) <= 3 if exposed else len(floating) == 0,
                   f"{label}: incorrect detached floating-rock count")
    roots = {name for name, rock in masses.items() if core_intersection(rock, roles["IslandCore"])}
    graph = {name: set() for name in masses}
    names = list(masses)
    for index, name in enumerate(names):
        for other in names[index+1:]:
            if convex_gap(masses[name], masses[other]) < -TOLERANCE:
                graph[name].add(other)
                graph[other].add(name)
    rooted, pending = set(), list(roots)
    while pending:
        name = pending.pop()
        if name not in rooted:
            rooted.add(name)
            pending.extend(graph[name]-rooted)
    native.require(rooted == set(masses), f"{label}: attached boulder is not rooted in the core")
    float_sizes = [float((rock["maximum"]-rock["minimum"])[:2].max()) for rock in floating.values()]
    native.require(not float_sizes or max(float_sizes) >= .12, f"{label}: floating rocks remain tiny specks")
    native.require(len(float_sizes) < 2 or max(float_sizes)/min(float_sizes) >= 1.15,
                   f"{label}: detached rocks lack visible size variation")
    gaps = {}
    for name, rock in floating.items():
        native.require(not core_intersection(rock, roles["IslandCore"])
                       and not point_inside_core(rock["points"].mean(axis=0), roles["IslandCore"]),
                       f"{label}: floating rock intersects or is inside its core")
        distance = mesh_surface_distance(rock["faces"], roles["IslandCore"])
        native.require(distance >= ROCK_GAP, f"{label}: floating rock has no actual gap from its core")
        for other_name, other in {**masses, **floating}.items():
            if other_name == name:
                continue
            native.require(convex_gap(rock, other) > 0, f"{label}: floating rock intersects another rock")
            distance = min(distance, mesh_surface_distance(rock["faces"], other["faces"]))
        native.require(distance >= ROCK_GAP, f"{label}: floating rock has no actual gap from attached geometry")
        gaps[name] = distance
    rock_report = lambda rock: {key: rock[key] for key in ("volume", "principal_axis_ratio", "facets", "median_facet_area", "maximum_facet_area")}
    report = {**integration, "floor_triangles": len(floor_faces), "floor_projected_area": area,
              "exposed_floor_edges_attached_to_core_walls": attached_floor_edges,
              "floor_maximum_vertex_valence": maximum_valence,
              "boulder_masses": len(masses), "directly_rooted_masses": len(roots), "all_masses_rooted": True,
              "boulders": {name: rock_report(rock) for name, rock in masses.items()},
              "floating_rocks": len(floating), "floating_rock_sizes": float_sizes, "floating_rock_gaps": gaps,
              "largest_boulder_horizontal_width": max(float((rock["maximum"]-rock["minimum"])[:2].max()) for rock in masses.values()),
              "largest_boulder_height": max(float(rock["maximum"][2]-rock["minimum"][2]) for rock in masses.values()),
              "minimum_z": float(points[:, 2].min()),
              "floor_depth_span": float(floor["points"][:, 2].max()-floor["points"][:, 2].min())}
    return {"floor_boundary": boundary}, list(floating.values()), report


def backing_frame_coverage(primitives, connection_side, wall_side, lo, hi):
    axis, plane = (1 if connection_side % 2 == 0 else 0), (1 if connection_side in (1, 2) else 0)
    other_axis = 1-axis
    recess = .055 if wall_side in (0, 3) else .945
    intervals = []
    for primitive in primitives:
        if primitive.material.get("name") != "GroundModuleRecess":
            continue
        for face in primitive.points[primitive.triangles]:
            for a, b in zip(face, np.roll(face, -1, axis=0)):
                if max(abs(a[axis]-plane), abs(b[axis]-plane), abs(a[other_axis]-recess), abs(b[other_axis]-recess)) < TOLERANCE:
                    intervals.append((a[2], b[2]))
    return interval_coverage(intervals, lo, hi)


def audit_rotated_joins(loaded):
    contract = {"families": [{"name": name, "mask": mask} for name, mask in FAMILIES]}
    traces, frames = {}, {}
    for square in (False, True):
        height = 1 if square else .91
        for mask in range(16):
            family, turns = native.canonical_for_mask(contract, mask)
            for variant in range(10):
                model = loaded[(square, family["name"], variant)]
                edges = [(native.rotate(np.asarray([a]), turns)[0], native.rotate(np.asarray([b]), turns)[0])
                         for a, b in model["floor_boundary"]]
                key = (square, mask, variant)
                rotated = {}
                for kind in ("primitives", "regular_primitives"):
                    rotated[kind] = [native.Primitive(primitive.material, native.rotate(primitive.points, turns),
                                                     primitive.normals, primitive.uvs, primitive.triangles, primitive.min_normal_dot)
                                     for primitive in model[kind]]
                for side in range(4):
                    if mask & (1 << side):
                        continue
                    traces[(*key, side)] = hidden_floor_trace(edges, side, "Rotated native body")
                    for wall in range(4):
                        if wall % 2 == side % 2 or not mask & (1 << wall):
                            continue
                        coverage = backing_frame_coverage(rotated["primitives"], side, wall, CONNECTION_HEIGHT, height)
                        native.require(abs(coverage-(height-CONNECTION_HEIGHT)) < TOLERANCE,
                                       "Remodeled wall misses its full shared backing frame")
                        regular = backing_frame_coverage(rotated["regular_primitives"], side, wall, 0, height)
                        native.require(abs(regular-height) < TOLERANCE,
                                       "Supported regular ground misses its original backing frame")
                        frames[(*key, side, wall)] = (coverage, regular)
    joins, supported = 0, 0
    for square in (False, True):
        height = 1 if square else .91
        for axis, a_side, b_side in ((0, 1, 3), (1, 2, 0)):
            for a_mask in range(16):
                if a_mask & (1 << a_side):
                    continue
                for b_mask in range(16):
                    if b_mask & (1 << b_side):
                        continue
                    for a_variant in range(10):
                        for b_variant in range(10):
                            a = traces[(square, a_mask, a_variant, a_side)]
                            b = traces[(square, b_mask, b_variant, b_side)]
                            native.require(a and b, "Mixed-style underside connector is missing")
                            joins += 1
                            for wall in ((0, 2) if axis == 0 else (1, 3)):
                                if not a_mask & b_mask & (1 << wall):
                                    continue
                                bottom_frame = frames[(square, a_mask, a_variant, a_side, wall)][0]
                                regular_frame = frames[(square, b_mask, b_variant, b_side, wall)][1]
                                native.require(abs(bottom_frame-(height-CONNECTION_HEIGHT)) < TOLERANCE
                                               and abs(regular_frame-height) < TOLERANCE
                                               and -1 <= CONNECTION_HEIGHT <= 0,
                                               "Remodeled wall leaves a gap beside supported regular ground")
                                supported += 1
    return {"all_rotated_exposure_masks": 16, "hidden_floor_traces": len(traces),
            "actual_mixed_style_connected_edge_joins": joins, "mixed_supported_regular_backing_contacts": supported}

def audit_source(path, reports, loaded):
    native.require(path.is_file(), f"Missing editable Blender source snapshot: {path}")
    records = json.loads(path.read_text(encoding="utf-8"))
    native.require(len(records) == 120, "Incomplete native bottom source snapshot")
    seen = set()
    for row in records:
        key = (bool(row["squareTop"]), row["shape"], int(row["variant"]))
        native.require(key in loaded and key not in seen, "Duplicate or unknown native bottom body")
        seen.add(key)
        body, report = row["body"], reports[key]
        properties = body["properties"]
        native.require(properties.get("GroundModuleBottom") is True
                       and properties.get("BottomAttachmentHeight") == 0
                       and properties.get("BottomMaxDepth") == 1
                       and properties.get("BottomRevision") == 4
                       and abs(properties.get("BottomRemodelHeight", -1)-REMODEL_HEIGHT) < TOLERANCE
                       and abs(properties.get("BottomConnectionHeight", 1)-CONNECTION_HEIGHT) < TOLERANCE
                       and abs(properties.get("BottomMaxOverhang", -1)-MAX_OVERHANG) < TOLERANCE,
                       "Native source lacks its bottom-placement metadata")
        native.require(properties.get("FloatingRockCount") == report["floating_rocks"],
                       "Native source floating-rock metadata differs from exported topology")
        native.require(properties.get("BoulderMassCount") == report["boulder_masses"],
                       "Native source boulder-mass metadata differs from exported topology")
        points = np.asarray(body["points"], dtype=float)
        native.require(points.shape == (body["vertices"], 3) and len(body["polygons"]) == body["faces"],
                       "Native source snapshot vertex/face counts disagree")
        exported = loaded[key]["all_points"]
        used = sorted({int(index) for polygon in body["polygons"] for index in polygon})
        native.require({vertex_key(point) for point in points[used]} == {vertex_key(point) for point in exported},
                       "Exported vertices differ from editable native Blender geometry")
        native.require(np.allclose(points.min(axis=0), body["bounds"]["min"], atol=TOLERANCE, rtol=0)
                       and np.allclose(points.max(axis=0), body["bounds"]["max"], atol=TOLERANCE, rtol=0),
                       "Native source bounds differ from geometry")
        native.require(all(name in body["vertexGroups"] for name in ("IslandCore", "IslandUnderside", "IslandRockWalls")),
                       "Editable core, underside or rock-wall selection group is missing")
        for role, count in (("FloatingRock", report["floating_rocks"]), ("BoulderMass", report["boulder_masses"])):
            groups = [name for name in body["vertexGroups"] if re.fullmatch(role+r"\d+", name)]
            native.require(len(groups) == count, f"Editable {role} selection groups are missing")
            native.require(all(name in body["faceGroups"] for name in groups), f"Editable {role} face ownership is missing")
        for name, indices in body["faceGroups"].items():
            native.require(name in body["vertexGroups"]
                           and {int(vertex) for index in indices for vertex in body["polygons"][index]}
                           <= set(body["vertexGroups"][name]),
                           f"Editable {name} vertex selection does not cover its owned faces")
    return {"native_editable_bodies": len(seen), "all_exported_positions_match_source": True,
            "editable_core_floor_wall_boulder_and_floating_rock_groups": True, "bottom_revision": 4,
            "source_snapshot_sha256": native.digest(path)}


def write_fixture(destination):
    destination = destination.resolve()
    native.require(destination.is_relative_to(ROOT / "out"), "Fixture must stay in ignored out tree")
    result = native.write_fixture(destination)
    path = destination / "level0/screen0.scr"
    text = path.read_text(encoding="utf-8")
    text = text.replace("@layer 1", "@layer 3").replace("@layer 0", "@layer 2")
    empty = "\n".join(" " * result["width"] for _ in range(result["height"]))
    text = text.replace("\n@layer 2", f"\n@layer 0\n{empty}\n\n@layer 1\n{empty}\n\n@layer 2")
    path.write_text(text, encoding="utf-8")
    width, height = 16, 12
    layers = [[[" "] * width for _ in range(height)] for _ in range(5)]
    styles = ".AFSVXYZac"
    # A wide floating plateau, isolated tile, thin strip, and L-shaped corner.
    for y in range(2, 6):
        for x in range(2, 7):
            layers[2][y][x] = styles[(x + y * 3) % 10]
    layers[2][2][10] = "A"
    for x in range(10, 14):
        layers[2][5][x] = styles[x % 10]
    for x, y in ((2, 9), (3, 9), (3, 8)):
        layers[2][y][x] = styles[(x+y) % 10]
    # Same-layer plateau with ground, cliff, wall, decoration and ice support.
    for x, support in zip(range(7, 12), (".", "u", "#", "D", "I")):
        layers[1][9][x] = support
        layers[2][9][x] = styles[x % 10]
    layers[3][3][3], layers[3][5][6] = "C", "E"
    header = '@camera {"pitch":55,"yaw":-25}\n@groundsplat {"base":"GroundGrass","color":[0.25,0.75,0.35],"detail":"GroundRock","mask":"GroundSplatMap0_0","name":"Floating island bottoms"}\n'
    def serialize(water=False):
        value = header + ("@water 1\n" if water else "")
        for z, grid in enumerate(layers):
            value += f"\n@layer {z}\n" + "\n".join("".join(row) for row in grid) + "\n"
        return value
    (path.parent / "screen1.scr").write_text(serialize(), encoding="utf-8")
    (path.parent / "screen2.scr").write_text(serialize(True), encoding="utf-8")
    native.write_json(path.parent / "metadata.json", {"format": 1, "name": "Floating island bottom review", "screens": ["All shapes", "Mixed support", "Water"]})
    result.update({"ground_layer": 2, "mixed_support_screen": (path.parent / "screen1.scr").relative_to(ROOT).as_posix(),
                   "water_screen": (path.parent / "screen2.scr").relative_to(ROOT).as_posix()})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--models", type=Path, default=MODELS)
    parser.add_argument("--output", type=Path, default=ROOT / "out/ground-modules/bottom-validation.json")
    parser.add_argument("--source-audit", type=Path, default=ROOT / "out/ground-modules/bottom-source-audit.json")
    parser.add_argument("--baseline", type=Path, default=ROOT / "out/ground-modules/bottom-original-kit-hashes.json")
    parser.add_argument("--accepted-source", type=Path, default=ROOT / "out/ground-modules/bottom-v3-accepted-source-audit.json")
    parser.add_argument("--accepted-models", type=Path, default=ROOT / "out/ground-modules/bottom-v3-accepted-models")
    parser.add_argument("--write-fixture", type=Path)
    parser.add_argument("--fixture-only", action="store_true")
    args = parser.parse_args()
    fixture = write_fixture(args.write_fixture) if args.write_fixture else None
    if args.fixture_only:
        native.require(fixture is not None, "--fixture-only requires --write-fixture")
        print(json.dumps(fixture))
        return
    preserved = native.verify_baseline(args.baseline)
    loaded, reports = {}, {}
    native.require(args.source_audit.is_file(), "Editable native source snapshot is missing")
    source_rows = json.loads(args.source_audit.read_text(encoding="utf-8"))
    sources = {(bool(row["squareTop"]), row["shape"], int(row["variant"])): row for row in source_rows}
    native.require(len(source_rows) == len(sources) == 120, "Incomplete editable native source snapshot")
    native.require(args.accepted_source.is_file(), "Accepted V3 source snapshot is missing")
    accepted_rows = json.loads(args.accepted_source.read_text(encoding="utf-8"))
    accepted_sources = {(bool(row["squareTop"]), row["shape"], int(row["variant"])): row for row in accepted_rows}
    native.require(len(accepted_rows) == len(accepted_sources) == 120, "Incomplete accepted V3 source snapshot")
    manifest = json.loads((ROOT / "assets/manifest.json").read_text(encoding="utf-8"))
    manifest_models = {model["name"]: model["path"] for model in manifest["models"]}
    for square in (False, True):
        for family, mask in FAMILIES:
            for variant in range(10):
                stem = f"ground_bottom_{'square_' if square else ''}{family}_{variant+1:02}"
                path = args.models / f"{stem}.glb"
                document = Document(path)
                primitives = native.read_primitives(document)
                key = (square, family, variant)
                body = sources[key]["body"]
                accepted_body = accepted_sources[key]["body"]
                native.require(accepted_body["properties"].get("BottomRevision") == 3, "Accepted source is not V3")
                mapping, shape_preservation = audit_accepted_shape_source(body, accepted_body, mask, path.name)
                accepted_path = args.accepted_models / path.name
                accepted_document = Document(accepted_path)
                shape_preservation.update(audit_accepted_shape_export(
                    primitives, native.read_primitives(accepted_document), texture_coordinates(document),
                    texture_coordinates(accepted_document), mapping, document, accepted_document, path.name))
                shape_preservation["accepted_export_sha256"] = native.digest(accepted_path)
                original = Document(args.models / f"ground_{'square_' if square else ''}{family}_{variant+1:02}.glb")
                original_primitives = native.read_primitives(original)
                top_band = audit_top_band(primitives, original_primitives, texture_coordinates(document),
                                          texture_coordinates(original), path.name)
                original_maps = {material["name"]: native.material_maps(original, material)
                                 for material in original.data["materials"]}
                for primitive in primitives:
                    name = primitive.material.get("name", "")
                    if name != "GroundModuleRecess" and np.any(primitive.points[:, 2] < -TOLERANCE):
                        native.require(set(native.material_maps(document, primitive.material)) == {"albedo", "normal", "orm", "ao"},
                                       f"{path.name}: underside lacks its full rock PBR bindings")
                    if name in original_maps:
                        native.require(native.material_maps(document, primitive.material) == original_maps[name],
                                       f"{path.name}: original upper PBR bindings changed")
                resources = []
                for item in document.data.get("images", []):
                    native.require("uri" in item and "bufferView" not in item, "Unfinalized embedded bottom texture")
                    resource = native.resource_path(path.parent, item["uri"])
                    native.require(resource.is_relative_to(ROOT / "assets") and resource.suffix.lower() == ".png",
                                   "Bottom model has a dependency outside shared assets")
                    with Image.open(resource) as image:
                        image.verify()
                    resources.append(resource.relative_to(ROOT).as_posix())
                main_component, _, report = audit_lower_geometry(primitives, mask, path.name, body)
                loaded[key] = {**main_component, "all_points": np.concatenate([p.points for p in primitives]),
                               "primitives": primitives, "regular_primitives": original_primitives}
                model_name = f"GroundRockBottom{'Square' if square else ''}{'' if family == 'island' else family.title()}{variant+1:02}"
                native.require(manifest_models.get(model_name) == path.relative_to(ROOT / "assets").as_posix(),
                               f"{path.name}: runtime manifest mapping is missing")
                reports[key] = {**report, "accepted_shape_preservation": shape_preservation,
                                "top_band": top_band, "file": path.relative_to(ROOT).as_posix(), "sha256": native.digest(path),
                                "bytes": path.stat().st_size, "variant": variant+1, "shape": family,
                                "square_top": square, "exposed_mask": mask, "pbr_dependencies": sorted(set(resources))}
    rejected = ROOT / "out/ground-modules/bottom-v1-island-01.glb"
    regression = {"destructive_fixture_coverage": "tests/ValidateGroundBottomsTests.py"}
    if rejected.is_file():
        old = native.read_primitives(Document(rejected))
        faces = np.concatenate([primitive.points[primitive.triangles] for primitive in old])
        try:
            audit_integration(faces, 15, "Rejected v1")
        except AssertionError as error:
            regression.update({"rejected_v1_status": "REJECTED", "reason": str(error),
                               **integration_metrics(faces, 15)})
        else:
            raise AssertionError("Rejected pasted-on v1 passed the remodeled geometry audit")
    result = {"status": "PASS", "native_models": len(reports), "preserved_original_kit_files": preserved,
              "accepted_v3_source_sha256": native.digest(args.accepted_source),
              "accepted_v3_shape_topology_z_uv_and_core_preserved": True,
              "top_band_geometry_all_uv_streams_and_pbr_preserved": True,
              "topology": audit_rotated_joins(loaded), "native_source": audit_source(args.source_audit, reports, loaded),
              "art_geometry_regression": regression,
              "models": list(reports.values())}
    if fixture:
        result["fixture"] = fixture
    native.write_json(args.output, result)
    print(json.dumps({key: value for key, value in result.items() if key != "models"}))


if __name__ == "__main__":
    main()
