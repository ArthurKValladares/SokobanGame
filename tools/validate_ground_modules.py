#!/usr/bin/env python3
"""Read-only audit of the native Blender ground bodies and broad splat caps.

The JSON authoring contract defines the six canonical exposure masks. The
audit reads every exported GLB and the actual native cap mesh table. Eight
neighbors determine concave corners; compatible two-cell neighborhoods check
mixed-style height and material-coverage traces. No model geometry is created
or modified by this tool.
Use --record-baseline before authoring to retain the original map hashes.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
from urllib.parse import unquote

import numpy as np
from PIL import Image

from pbr_gltf import Document

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
DEFAULT_CONTRACT = ASSETS / "custom/source/ground_modules/contract.json"
DEFAULT_MODELS = ASSETS / "custom/models/ground_modules"
DEFAULT_OUTPUT = ROOT / "out/ground-modules/native-validation.json"
DEFAULT_BASELINE = ROOT / "out/ground-modules/original-ground-hashes.json"
DEFAULT_SOURCE_AUDIT = ROOT / "out/ground-modules/native-source-audit.json"
TOLERANCE = 2e-5


def require(condition, message):
    if not bool(condition):
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def resource_path(parent, uri):
    # Normalize relative '..' components before Windows queries the actual file.
    # This also keeps each permission check on the intended resolved resource.
    path = Path(os.path.abspath(parent / unquote(uri))).resolve()
    require(path.is_file(), f"Missing image resource: {path}")
    return path


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def original_models():
    return [ASSETS / f"custom/pbr/models/GroundRock{i:02}.glb" for i in range(1, 11)]


def texture_path(document, texture_index):
    texture = document.data["textures"][texture_index]
    image = document.data["images"][texture["source"]]
    require("uri" in image, f"{document.path.name}: embedded PBR map")
    path = resource_path(document.path.parent, image["uri"])
    require(path.is_relative_to(ASSETS), f"PBR resource outside assets: {path}")
    return path


def record_baseline(path):
    files = set(original_models())
    for model in original_models():
        document = Document(model)
        for image in document.data.get("images", []):
            require("uri" in image, f"Original resource unexpectedly embedded: {model}")
            files.add(resource_path(model.parent, image["uri"]))
    value = {file.relative_to(ROOT).as_posix(): digest(file) for file in sorted(files)}
    write_json(path, value)
    return {"files": len(value), "baseline": str(path.relative_to(ROOT))}


def verify_baseline(path):
    require(path.is_file(), f"Missing pre-authoring baseline: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    for relative, expected in value.items():
        file = ROOT / relative
        require(file.is_file() and digest(file) == expected,
                f"Original ground/PBR file changed: {relative}")
    return len(value)


def blender_points(points):
    # Export maps Blender (x,y,z) to glTF (x,z,-y).
    return np.column_stack((points[:, 0], -points[:, 2], points[:, 1]))


@dataclass
class Primitive:
    material: dict
    points: np.ndarray
    normals: np.ndarray
    uvs: np.ndarray
    triangles: np.ndarray
    min_normal_dot: float


def read_primitives(document):
    data = document.data
    require(data.get("asset", {}).get("version") == "2.0", "Expected glTF 2.0")
    require(len(data.get("nodes", [])) == 1, f"{document.path.name}: expected one body node")
    node = data["nodes"][0]
    require("mesh" in node and not node.get("children"), "Expected a direct mesh node")
    require(np.allclose(node.get("translation", [0, 0, 0]), [0, 0, 0], atol=1e-7)
            and np.allclose(node.get("rotation", [0, 0, 0, 1]), [0, 0, 0, 1], atol=1e-7)
            and np.allclose(node.get("scale", [1, 1, 1]), [1, 1, 1], atol=1e-7)
            and np.allclose(node.get("matrix", np.eye(4).reshape(-1)), np.eye(4).reshape(-1), atol=1e-7),
            f"{document.path.name}: nonidentity native export transform")
    scene = data["scenes"][data.get("scene", 0)]
    require(scene.get("nodes") == [0], "Expected sole body node in default scene")
    output = []
    for primitive in data["meshes"][node["mesh"]]["primitives"]:
        require(primitive.get("mode", 4) == 4, "Expected triangle primitive")
        attributes = primitive["attributes"]
        require(all(key in attributes for key in ("POSITION", "NORMAL", "TEXCOORD_0")),
                "Missing position/normal/UV attributes")
        points = document.accessor(attributes["POSITION"]).astype(float)
        normals = document.accessor(attributes["NORMAL"]).astype(float)
        uvs = document.accessor(attributes["TEXCOORD_0"]).astype(float)
        require(points.shape == normals.shape and points.shape[1] == 3
                and uvs.shape == (len(points), 2), "Vertex attribute count mismatch")
        require(all(np.isfinite(values).all() for values in (points, normals, uvs)),
                "Nonfinite vertex attribute")
        for name, accessor in attributes.items():
            if name.startswith("TEXCOORD_"):
                values = document.accessor(accessor)
                require(values.shape == (len(points), 2) and np.isfinite(values).all(),
                        f"Invalid native UV stream {name}")
        require(np.allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-4), "Nonunit normal")
        indices = document.accessor(primitive["indices"]).reshape(-1).astype(np.int64)
        require(len(indices) > 0 and len(indices) % 3 == 0
                and indices.min() >= 0 and indices.max() < len(points), "Invalid triangle indices")
        triangles = indices.reshape(-1, 3)
        material = data["materials"][primitive["material"]]
        for spec in (material.get("normalTexture"), material.get("occlusionTexture"),
                     material.get("pbrMetallicRoughness", {}).get("baseColorTexture"),
                     material.get("pbrMetallicRoughness", {}).get("metallicRoughnessTexture")):
            if spec is not None:
                require(f'TEXCOORD_{spec.get("texCoord", 0)}' in attributes,
                        "PBR material references a missing UV stream")
        if "normalTexture" in material:
            require("TANGENT" in attributes, "PBR primitive lacks tangent frames")
        if "TANGENT" in attributes:
            tangents = document.accessor(attributes["TANGENT"]).astype(float)
            require(tangents.shape == (len(points), 4) and np.isfinite(tangents).all(), "Invalid tangent stream")
            require(np.allclose(np.linalg.norm(tangents[:, :3], axis=1), 1, atol=1e-4)
                    and np.allclose(np.abs(tangents[:, 3]), 1, atol=1e-5), "Nonunit tangent frame")
            require(np.max(np.abs(np.sum(tangents[:, :3] * normals, axis=1))) <= 1e-3,
                    "Tangent frame is not orthogonal to normal")
        points = blender_points(points)
        normals = blender_points(normals)
        faces = points[triangles]
        cross = np.cross(faces[:, 1] - faces[:, 0], faces[:, 2] - faces[:, 0])
        lengths = np.linalg.norm(cross, axis=1)
        require(np.all(lengths > 1e-11), "Degenerate geometry triangle")
        agreement = np.sum((cross / lengths[:, None])[:, None, :] * normals[triangles], axis=2)
        require(np.all(agreement > 0), f"{document.path.name}: normal faces away from its triangle")
        uv_faces = uvs[triangles]
        a, b = uv_faces[:, 1] - uv_faces[:, 0], uv_faces[:, 2] - uv_faces[:, 0]
        uv_area = np.abs(a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0])
        bottom = np.all(np.abs(faces[:, :, 2]) < TOLERANCE, axis=1)
        require(np.all((uv_area > 1e-12) | bottom), "Degenerate non-bottom UV triangle")
        output.append(Primitive(material, points, normals, uvs, triangles, float(agreement.min())))
    require(output, "Empty body mesh")
    return output


def side_point(side, u, inset=0, height=1):
    return np.array(((u, inset, height), (1-inset, u, height),
                     (1-u, 1-inset, height), (inset, 1-u, height))[side], dtype=float)


def rotate(points, turns):
    output = points.copy()
    for _ in range(turns):
        output[:, 0], output[:, 1] = 1-output[:, 1], output[:, 0].copy()
    return output


def rotated_mask(mask, turns):
    return ((mask << turns) | (mask >> (4-turns))) & 15


def canonical_for_mask(contract, mask):
    for family in contract["families"]:
        for turns in range(4):
            if rotated_mask(family["mask"], turns) == mask:
                return family, turns
    raise AssertionError(f"No module for mask {mask}")


def source_material_side(document, material_index):
    arrays = [document.accessor(p["attributes"]["POSITION"]) for m in document.data["meshes"]
              for p in m["primitives"] if p["material"] == material_index]
    points = blender_points(np.concatenate(arrays))
    lo, hi = points.min(axis=0), points.max(axis=0)
    if hi[2] < .001:
        return -1
    if hi[1] < .13:
        return 0
    if lo[0] > .87:
        return 1
    if lo[1] > .87:
        return 2
    if hi[0] < .13:
        return 3
    raise AssertionError(f"Unclassified original stone material {material_index}: {lo}, {hi}")


def material_maps(document, material):
    pbr = material.get("pbrMetallicRoughness", {})
    info = {"albedo": pbr.get("baseColorTexture"), "normal": material.get("normalTexture"),
            "orm": pbr.get("metallicRoughnessTexture"), "ao": material.get("occlusionTexture")}
    return {kind: texture_path(document, spec["index"]).relative_to(ASSETS).as_posix()
            for kind, spec in info.items() if spec is not None}


def concave_allowed(exposed):
    return sum(1 << corner for corner, incident in enumerate((9, 3, 6, 12)) if exposed & incident == 0)


def audit_body_v2(path, family, variant, contract):
    document = Document(path)
    primitives = read_primitives(document)
    exposed = family["mask"]
    original = Document(original_models()[variant])
    source_materials = {}
    for index, material in enumerate(original.data["materials"]):
        points = np.concatenate([blender_points(original.accessor(p["attributes"]["POSITION"]))
                                 for mesh in original.data["meshes"] for p in mesh["primitives"] if p["material"] == index])
        side = source_material_side(original, index)
        expected = points.copy()
        if side >= 0:
            expected[:, 2] -= contract["lipDepth"] * np.clip((expected[:, 2]-(1-contract["upperBodyBand"])) / contract["upperBodyBand"], 0, 1)
        source_materials[material["name"]] = (side, material_maps(original, material), expected)
    pngs = set()
    for image in document.data.get("images", []):
        require("uri" in image and "bufferView" not in image, f"{path.name}: image metadata has not been finalized")
        resource = resource_path(path.parent, image["uri"])
        require(resource.is_relative_to(ASSETS) and resource.suffix.lower() == ".png", "Invalid native PBR dependency")
        with Image.open(resource) as decoded:
            decoded.verify()
        pngs.add(resource.relative_to(ASSETS).as_posix())
    sides, seal_triangles, seal_area = Counter(), Counter(), Counter()
    bottom_triangles, backing_triangles = 0, 0
    points = np.concatenate([p.points for p in primitives])
    require(np.all(points[:, 2] >= -TOLERANCE) and np.all(points[:, 2] <= contract["outerLipHeight"]+TOLERANCE),
            f"{path.name}: body overlaps the chamfer or extends below the original bottom")
    require(abs(points[:, 2].max()-contract["outerLipHeight"]) < TOLERANCE if exposed
            else np.all(np.abs(points[:, 2]) < TOLERANCE), "Incorrect straight outer lip/interior body height")
    for primitive in primitives:
        name = primitive.material.get("name", "")
        faces = primitive.points[primitive.triangles]
        matches = [key for key in source_materials if name == key or name.startswith(key+".")]
        if matches:
            key = max(matches, key=len)
            side, maps, expected = source_materials[key]
            require(material_maps(document, primitive.material) == maps, f"{path.name}: original PBR maps changed")
            if side == -1:
                require(np.all(np.abs(faces[:, :, 2]) < TOLERANCE), "Native bottom moved")
                bottom_triangles += len(faces)
                continue
            require(exposed & (1 << side), f"{path.name}: stone geometry remains on hidden side {side}")
            sides[side] += len(faces)
            seal_points = np.array([side_point(side, station, inset, contract["outerLipHeight"])
                                    for station in contract["bodySealStations"] for inset in (0, contract["bodyRecessInset"])])
            # New seal vertices and the original shells are checked separately.
            native_distance = np.linalg.norm(primitive.points[:, None, :]-seal_points[None, :, :], axis=2).min(axis=1)
            source_distance = np.linalg.norm(primitive.points[:, None, :]-expected[None, :, :], axis=2).min(axis=1)
            require(np.all((native_distance < TOLERANCE) | (source_distance <= contract["microscopicCleanupTolerance"]+TOLERANCE)),
                    f"{path.name}: original shell moved in XY or has nonuniform height compression")
            native_face = np.all(native_distance[primitive.triangles] < TOLERANCE, axis=1)
            for face in faces[native_face]:
                cross = np.cross(face[1]-face[0], face[2]-face[0])
                require(cross[2] > 0 and np.linalg.norm(cross[:2]) < TOLERANCE, "Native rock seal is not upward/flat")
                seal_triangles[side] += 1
                seal_area[side] += float(cross[2]/2)
        else:
            require(name == "GroundModuleRecess" and not material_maps(document, primitive.material), "Unexpected body material")
            backing_triangles += len(faces)
            for face in faces:
                midpoint = face.mean(axis=0)
                depths = (midpoint[1], 1-midpoint[0], 1-midpoint[1], midpoint[0])
                candidate = [side for side, depth in enumerate(depths) if abs(depth-contract["bodyRecessInset"]) < TOLERANCE]
                require(any(exposed & (1 << side) for side in candidate), "Backing exists on an occupied side or is not recessed")
    expected_sides = {side for side in range(4) if exposed & (1 << side)}
    require(set(sides) == expected_sides and bottom_triangles == 2, "Incorrect exposed stone sides/bottom geometry")
    panels = len(contract["bodySealStations"])-1
    require(backing_triangles == exposed.bit_count()*panels*2, "Incorrect native backing panel count")
    for side in expected_sides:
        require(seal_triangles[side] == panels*2 and abs(seal_area[side]-contract["bodyRecessInset"]) < TOLERANCE,
                f"{path.name}: rock seal does not continuously close the full exposed lip")
        axis = 1 if side % 2 == 0 else 0
        plane = 1 if side in (1, 2) else 0
        lip = points[(np.abs(points[:, axis]-plane) < TOLERANCE) & (np.abs(points[:, 2]-contract["outerLipHeight"]) < TOLERANCE)]
        for station in contract["bodySealStations"]:
            point = side_point(side, station, height=contract["outerLipHeight"])
            require(len(lip) and np.min(np.linalg.norm(lip-point, axis=1)) < TOLERANCE,
                    "Native outer border is not exactly straight at the shared lip height")
    return primitives, {"file": path.relative_to(ROOT).as_posix(), "sha256": digest(path), "bytes": path.stat().st_size,
                        "variant": variant+1, "canonical_exposed_mask": exposed,
                        "vertices": sum(len(p.points) for p in primitives), "triangles": sum(len(p.triangles) for p in primitives),
                        "side_triangles": dict(sides), "seal_triangles": dict(seal_triangles), "seal_areas": dict(seal_area),
                        "bottom_triangles": bottom_triangles, "backing_triangles": backing_triangles,
                        "original_shell_xy_preserved": True, "uniform_upper_band_compression": True,
                        "pbr_dependencies": sorted(pngs), "min_face_normal_agreement": min(p.min_normal_dot for p in primitives)}


def occupancy_state(occupied, cell=(0, 0)):
    x, y = cell
    cardinal = ((0, -1), (1, 0), (0, 1), (-1, 0))
    diagonal = ((-1, -1), (1, -1), (1, 1), (-1, 1))
    exposed = sum(1 << side for side, (dx, dy) in enumerate(cardinal) if (x+dx, y+dy) not in occupied)
    allowed = concave_allowed(exposed)
    concave = sum(1 << corner for corner, (dx, dy) in enumerate(diagonal)
                  if allowed & (1 << corner) and (x+dx, y+dy) not in occupied)
    return exposed, concave


def quantized_point(point):
    return tuple(np.rint(np.asarray(point)/TOLERANCE).astype(np.int64))


def triangles_for_faces(faces):
    return np.array([triangle for face in faces for triangle in
                     ([face[:3]] if len(face) == 3 else [face[:3], [face[0], face[2], face[3]]])], dtype=np.int64)


def audit_cap_disk(points, faces, label):
    triangles = triangles_for_faces(faces)
    p = points[triangles]
    cross = np.cross(p[:, 1]-p[:, 0], p[:, 2]-p[:, 0])
    require(np.all(cross[:, 2] > 1e-10), f"{label}: cap projection folds or degenerates")
    area = float(cross[:, 2].sum()/2)
    require(abs(area-1) < TOLERANCE, f"{label}: cap projected area is {area}, expected one")
    vertices, remap = {}, []
    for point in points:
        key = quantized_point(point)
        vertices.setdefault(key, len(vertices))
        remap.append(vertices[key])
    welded = np.asarray(remap)[triangles]
    used = set(welded.reshape(-1).tolist())
    require(used == set(range(len(vertices))), f"{label}: unused source cap vertices")
    edges, directed, adjacency = Counter(), Counter(), {index: set() for index in used}
    for triangle in welded:
        require(len(set(triangle)) == 3, f"{label}: triangle collapses at weld tolerance")
        for a, b in zip(triangle, np.roll(triangle, -1)):
            a, b = int(a), int(b)
            edges[tuple(sorted((a, b)))] += 1
            directed[(a, b)] += 1
            adjacency[a].add(b)
            adjacency[b].add(a)
    require(all(count in (1, 2) for count in edges.values()), f"{label}: nonmanifold cap edge")
    boundary_graph = {}
    coordinates = np.array(list(vertices))*TOLERANCE
    for edge, count in edges.items():
        if count == 2:
            require(directed[edge] == 1 and directed[edge[::-1]] == 1, f"{label}: cap winding disagrees across an edge")
        else:
            a, b = edge
            boundary_graph.setdefault(a, set()).add(b)
            boundary_graph.setdefault(b, set()).add(a)
            aa, bb = coordinates[a], coordinates[b]
            require(any(abs(aa[axis]-plane) <= TOLERANCE and abs(bb[axis]-plane) <= TOLERANCE
                        for axis, plane in ((0, 0), (0, 1), (1, 0), (1, 1))),
                    f"{label}: open interior cap boundary")
    def connected(graph):
        reached, frontier = set(), [next(iter(graph))]
        while frontier:
            index = frontier.pop()
            if index not in reached:
                reached.add(index)
                frontier.extend(graph[index]-reached)
        return len(reached) == len(graph)
    require(connected(adjacency) and len(used)-len(edges)+len(welded) == 1,
            f"{label}: cap is not one connected manifold disk")
    require(boundary_graph and all(len(values) == 2 for values in boundary_graph.values())
            and connected(boundary_graph), f"{label}: cap boundary is not one closed unit-cell loop")
    return triangles, area


def read_native_caps(path, contract):
    data = json.loads(path.read_text(encoding="utf-8"))
    require(data["format"] == 2 and len(data["models"]) == contract["nativeCapCount"], "Incorrect native cap table format/count")
    require(abs(data["cornerWidth"]-contract["cornerWidth"]) < 1e-8
            and abs(data["lipDepth"]-contract["lipDepth"]) < 1e-8
            and np.allclose(data["sideStations"], contract["sideStations"], atol=1e-8)
            and np.allclose(data["middleWidths"], contract["variantMiddleWidths"], atol=1e-8),
            "Native cap authoring parameters differ from the contract")
    caps, names, face_count = {}, set(), 0
    for row in data["models"]:
        variant, shape, exposed, concave = (row[name] for name in ("variant", "shapeIndex", "exposedSideMask", "concaveCorners"))
        key = (variant, shape, concave)
        require(0 <= variant < 10 and 0 <= shape < 6 and key not in caps,
                "Invalid or repeated canonical native cap")
        require(exposed == contract["families"][shape]["mask"] and concave & ~concave_allowed(exposed) == 0,
                "Native cap has physically impossible exposure/concave state")
        require(row["object"] not in names, "Two native caps share an editable object name")
        names.add(row["object"])
        points = np.asarray(row["vertices"], dtype=float)
        faces = row["faces"]
        normals = np.asarray(row["normals"], dtype=float)
        require(points.ndim == 2 and points.shape[1] == 3 and np.isfinite(points).all(), "Invalid cap positions")
        require(np.all(points[:, :2] >= -TOLERANCE) and np.all(points[:, :2] <= 1+TOLERANCE)
                and np.all(points[:, 2] >= contract["outerLipHeight"]-TOLERANCE)
                and np.all(points[:, 2] <= 1+TOLERANCE), "Native cap outside its exact unit/height envelope")
        require(len(faces) > 0 and normals.shape == (len(faces), 3) and np.isfinite(normals).all()
                and np.allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-4) and np.all(normals[:, 2] > 0),
                "Invalid native face normals")
        require(len(row["wallCoverage"]) == len(faces), "Cap coverage/face count mismatch")
        vertex_coverage = {}
        for face_index, face in enumerate(faces):
            require(len(face) in (3, 4) and min(face) >= 0 and max(face) < len(points), "Invalid cap polygon indices")
            polygon = points[face]
            face_triangles = triangles_for_faces([face])
            triangles = points[face_triangles]
            cross = np.cross(triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0])
            normal = cross/np.linalg.norm(cross, axis=1)[:, None]
            require(np.all(normal @ normals[face_index] > .999), "Native face normal disagrees with actual geometry")
            coverage = row["wallCoverage"][face_index]
            require(len(coverage) == len(face), "Native wall coverage is not aligned with its face indices")
            for index, value in zip(face, coverage):
                expected = (1-points[index, 2])/contract["lipDepth"]
                require(0 <= value <= 1 and abs(value-expected) < TOLERANCE,
                        "Native wall coverage differs from its lowered lip/flat top")
                require(index not in vertex_coverage or vertex_coverage[index] == value, "Shared vertex coverage differs between faces")
                vertex_coverage[index] = value
        triangles, area = audit_cap_disk(points, faces, row["object"])
        for corner, xy in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
            on_corner = np.all(np.abs(points[:, :2]-xy) < TOLERANCE, axis=1)
            active = bool(exposed & (9, 3, 6, 12)[corner] or concave & (1 << corner))
            expected_height = contract["outerLipHeight"] if active else 1
            require(np.any(on_corner) and np.all(np.abs(points[on_corner, 2]-expected_height) < TOLERANCE),
                    "Native corner height reintroduces raised posts or loses a concave wedge")
        if exposed == 0 and concave == 0:
            require(len(faces) == 1 and len(faces[0]) == 4 and np.allclose(points[:, 2], 1),
                    "Fully surrounded cap must be one flat square quad")
        for side in range(4):
            if not exposed & (1 << side):
                continue
            for station in (0, *contract["sideStations"], 1):
                point = side_point(side, station, height=contract["outerLipHeight"])
                require(np.min(np.linalg.norm(points-point, axis=1)) < TOLERANCE,
                        "Exposed native lip is not straight or misses a broad-panel endpoint")
            widths = [contract["cornerWidth"], *contract["variantMiddleWidths"][variant], contract["cornerWidth"]]
            for station, width in zip(contract["sideStations"], widths):
                point = side_point(side, station, width, 1)
                require(np.min(np.linalg.norm(points-point, axis=1)) < TOLERANCE,
                        "Actual native inner chamfer outline differs from its broad style knots")
        caps[key] = {"points": points, "faces": faces, "normals": normals,
                     "coverage": vertex_coverage, "triangles": triangles, "object": row["object"], "area": area}
        face_count += len(faces)
    expected = {(variant, shape, concave) for variant in range(10) for shape, family in enumerate(contract["families"])
                for concave in range(16) if concave & ~concave_allowed(family["mask"]) == 0}
    require(set(caps) == expected, "Native cap table does not cover all twenty-five canonical states/style")
    return caps, {"native_editable_caps": len(caps), "native_cap_faces": face_count,
                  "max_native_cap_faces": max(len(cap["faces"]) for cap in caps.values()),
                  "projected_area_per_cap": 1, "every_cap_is_an_oriented_manifold_disk": True}


def cap_for_state(caps, contract, variant, state):
    exposed, concave = state
    family, turns = canonical_for_mask(contract, exposed)
    shape = contract["families"].index(family)
    canonical_concave = rotated_mask(concave, (4-turns) % 4)
    source = caps[(variant, shape, canonical_concave)]
    normals = source["normals"].copy()
    for _ in range(turns):
        normals[:, 0], normals[:, 1] = -normals[:, 1], normals[:, 0].copy()
    return {**source, "points": rotate(source["points"], turns), "normals": normals}


def cap_edge_trace(cap, side):
    axis = 1 if side % 2 == 0 else 0
    plane = 1 if side in (1, 2) else 0
    tangent = 1-axis
    segments = []
    for face in cap["faces"]:
        for a, b in zip(face, np.roll(face, -1)):
            aa, bb = cap["points"][a], cap["points"][b]
            if abs(aa[axis]-plane) < TOLERANCE and abs(bb[axis]-plane) < TOLERANCE and abs(aa[tangent]-bb[tangent]) > TOLERANCE:
                if aa[tangent] > bb[tangent]:
                    a, b, aa, bb = b, a, bb, aa
                segments.append((float(aa[tangent]), float(bb[tangent]), float(aa[2]), float(bb[2]),
                                 cap["coverage"][a], cap["coverage"][b]))
    require(segments, "Native cap has no outer edge trace")
    return segments


def sample_trace(trace, u):
    values = []
    for a, b, za, zb, ca, cb in trace:
        if a-TOLERANCE <= u <= b+TOLERANCE:
            weight = (u-a)/(b-a)
            values.append(np.array([za+(zb-za)*weight, ca+(cb-ca)*weight]))
    require(values and all(np.allclose(value, values[0], atol=TOLERANCE, rtol=0) for value in values),
            "Cap edge has a gap or inconsistent overlapping height/coverage")
    return values[0]


def compatible_join_states(axis):
    centers = {(0, 0), (1, 0)} if axis == 0 else {(0, 0), (0, 1)}
    cells = [(x, y) for y in range(-1, 2) for x in range(-1, 3)] if axis == 0 else [(x, y) for y in range(-1, 3) for x in range(-1, 2)]
    variable = [cell for cell in cells if cell not in centers]
    states = set()
    neighbor = (1, 0) if axis == 0 else (0, 1)
    for pattern in range(1 << len(variable)):
        occupied = centers | {cell for bit, cell in enumerate(variable) if pattern & (1 << bit)}
        states.add((occupancy_state(occupied), occupancy_state(occupied, neighbor)))
    return states


def audit_caps_v2(caps, contract):
    offsets = contract["neighborOffsets"]
    states = set()
    for pattern in range(256):
        occupied = {(0, 0)} | {tuple(offset) for bit, offset in enumerate(offsets) if pattern & (1 << bit)}
        states.add(occupancy_state(occupied))
    require(len(states) == 47, "Eight-neighbor topology does not produce forty-seven oriented states")
    cached = {(variant, state): cap_for_state(caps, contract, variant, state) for variant in range(10) for state in states}
    traces = {(variant, state, side): cap_edge_trace(cap, side) for (variant, state), cap in cached.items() for side in range(4)}
    joins = 0
    state_pairs = {}
    for axis, sides in ((0, (1, 3)), (1, (2, 0))):
        pairs = compatible_join_states(axis)
        state_pairs[axis] = len(pairs)
        for left_state, right_state in pairs:
            require(not left_state[0] & (1 << sides[0]) and not right_state[0] & (1 << sides[1]),
                    "Two occupied cells wrongly retain a shared exposed side")
            for left_variant in range(10):
                a = traces[(left_variant, left_state, sides[0])]
                for right_variant in range(10):
                    b = traces[(right_variant, right_state, sides[1])]
                    stations = sorted({0, 1, *[u for trace in (a, b) for segment in trace for u in segment[:2]]})
                    samples = stations + [(low+high)/2 for low, high in zip(stations, stations[1:]) if high-low > TOLERANCE]
                    for u in samples:
                        require(np.allclose(sample_trace(a, u), sample_trace(b, u), atol=TOLERANCE, rtol=0),
                                f"Open actual native cap join on axis {axis}, states {left_state}/{right_state}, styles {left_variant}/{right_variant}")
                    joins += 1
    # Verify the three-cell notch explicitly, including the cardinal-interior cell.
    occupied = {(0, 0), (1, 0), (1, -1)}
    notch_cells = [((0, 0), (1, 0)), ((1, 0), (0, 0)), ((1, -1), (0, 1))]
    notch_checks = 0
    for cell, local in notch_cells:
        state = occupancy_state(occupied, cell)
        for variant in range(10):
            points = cached[(variant, state)]["points"]
            selected = np.all(np.abs(points[:, :2]-local) < TOLERANCE, axis=1)
            require(np.any(selected) and np.all(np.abs(points[selected, 2]-contract["outerLipHeight"]) < TOLERANCE),
                    "Three-occupied-cell notch retains a full-height post")
            notch_checks += 1
    return {"eight_neighbor_patterns": 256, "variant_neighborhood_cases": 2560,
            "distinct_oriented_caps_per_variant": len(states), "rotated_native_caps_checked": len(cached),
            "two_cell_neighborhood_patterns_per_axis": 1024, "compatible_join_states_per_axis": state_pairs,
            "actual_mixed_variant_3d_cap_and_coverage_joins": joins, "three_cell_concave_corner_checks": notch_checks,
            "surrounded_without_concave_corners_has_one_quad": True, "runtime_mesh_vertex_deformation_required": False}


def audit_native_source_v2(path, contract_path, contract, loaded, caps):
    rows = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(rows, list) and len(rows) == 60, "Expected sixty actual native body/cap source records")
    seen = set()
    families = {family["name"]: index for index, family in enumerate(contract["families"])}
    for row in rows:
        shape, variant = row["shape"], row["variant"]
        key = (shape, variant)
        require(shape in families and 0 <= variant < 10 and key not in seen, "Invalid/repeated actual source body")
        seen.add(key)
        exposed = contract["families"][families[shape]]["mask"]
        require(row["exposedSideMask"] == exposed, "Source body has incorrect exposure metadata")
        body, top = row["body"], row["top"]
        for source in (body, top):
            require(source["properties"]["GroundModuleVariant"] == variant
                    and source["properties"]["ExposedSideMask"] == exposed,
                    "Native source properties disagree with exported shape")
        require(body["properties"]["GroundModuleShape"] == shape, "Incorrect source body family")
        points = np.concatenate([primitive.points for primitive in loaded[key]])
        require(np.allclose(points.min(axis=0), body["bounds"]["min"], atol=TOLERANCE, rtol=0)
                and np.allclose(points.max(axis=0), body["bounds"]["max"], atol=TOLERANCE, rtol=0),
                "Exported GLB bounds differ from their editable native bodies")
        cap = caps[(variant, families[shape], 0)]
        native_top = np.asarray(top["vertices"], dtype=float)
        require(top["object"] == cap["object"] and native_top.shape == cap["points"].shape
                and np.allclose(native_top, cap["points"], atol=TOLERANCE, rtol=0)
                and top["faces"] == cap["faces"], "Actual base source cap differs from the complete native cap table")
    source = Path(os.path.abspath(contract_path.parent/contract["source"])).resolve()
    require(source.is_file() and source.suffix == ".blend", "Editable native Blender source is missing")
    return {"source": source.relative_to(ROOT).as_posix(), "source_sha256": digest(source),
            "native_editable_bodies": len(seen), "native_editable_caps": len(caps),
            "actual_base_caps_crosschecked": len(seen), "source_and_exported_body_bounds_match": True,
            "cap_table_is_actual_native_mesh_data": True}


def audit_body_cap_closure_v2(loaded, caps, contract):
    closures, backing_joins = 0, 0
    for (variant, shape, _), cap in caps.items():
        family = contract["families"][shape]
        body = loaded[(family["name"], variant)]
        body_points = np.concatenate([primitive.points for primitive in body])
        for side in range(4):
            if not family["mask"] & (1 << side):
                continue
            trace = cap_edge_trace(cap, side)
            for station in contract["bodySealStations"]:
                require(np.allclose(sample_trace(trace, station), [contract["outerLipHeight"], 1], atol=TOLERANCE, rtol=0),
                        "Native painted cap lip does not close the exact rock seal")
                point = side_point(side, station, height=contract["outerLipHeight"])
                require(np.min(np.linalg.norm(body_points-point, axis=1)) < TOLERANCE,
                        "Native rock seal lacks an actual cap lip endpoint")
                closures += 1
    # All canonical bodies rotate against actual states; backings use fixed
    # station/height frames so different source styles leave no side seam.
    outward = np.array([[0, -1, 0], [1, 0, 0], [0, 1, 0], [-1, 0, 0]])
    cached = {}
    def frames(variant, exposed, join_side, wall_side, shift):
        key = (variant, exposed, join_side, wall_side, shift)
        if key not in cached:
            family, turns = canonical_for_mask(contract, exposed)
            values = set()
            for primitive in loaded[(family["name"], variant)]:
                if primitive.material.get("name") != "GroundModuleRecess":
                    continue
                points = rotate(primitive.points, turns)
                normals = primitive.normals.copy()
                for _ in range(turns):
                    normals[:, 0], normals[:, 1] = -normals[:, 1], normals[:, 0].copy()
                axis = 1 if join_side % 2 == 0 else 0
                plane = 1 if join_side in (1, 2) else 0
                selected = (np.abs(points[:, axis]-plane) < TOLERANCE) & (normals @ outward[wall_side] > .99)
                points = points[selected].copy()
                points[:, axis] += shift
                for point, normal in zip(points, normals[selected]):
                    values.add((quantized_point(point), quantized_point(normal)))
            cached[key] = values
        return cached[key]
    for axis, sides in ((0, (1, 3)), (1, (2, 0))):
        for left in range(16):
            if left & (1 << sides[0]):
                continue
            for right in range(16):
                if right & (1 << sides[1]):
                    continue
                for wall_side in ((0, 2) if axis == 0 else (1, 3)):
                    if not left & right & (1 << wall_side):
                        continue
                    for a in range(10):
                        for b in range(10):
                            aa = frames(a, left, sides[0], wall_side, 0)
                            bb = frames(b, right, sides[1], wall_side, 1)
                            require(aa and aa == bb, "Native body backing has a mixed-style geometry/normal seam")
                            backing_joins += 1
    return {"actual_native_body_cap_lip_checks": closures,
            "mixed_variant_backing_geometry_and_normal_joins": backing_joins,
            "outer_lip_height": contract["outerLipHeight"], "body_has_no_overlapping_chamfer": True}


def write_fixture(destination):
    destination = destination.resolve()
    require(destination.is_relative_to(ROOT / "out"), "Fixture must stay in ignored out tree")
    width, height = 23, 17
    grid = [[" "]*width for _ in range(height)]
    styles = ".AFSVXYZac"
    def put(x, y, character):
        grid[y][x] = character
    for i, style in enumerate(styles):
        put(2*i+1, 1, style)  # All ten standalone variants.
    for i in range(7):
        put(i+1, 4, styles[i])
        put(10, i+3, styles[(i+3) % 10])
    # A four-by-four plateau has corners, edges and surrounded top-only cells.
    for y in range(8, 12):
        for x in range(1, 5):
            put(x, y, styles[(x+3*y) % 10])
    # Rotated concave outline and a ring exercise opposing exposure masks.
    for x, y in ((6,8),(7,8),(8,8),(8,9),(8,10),(7,10),(6,10)):
        put(x, y, styles[(x+y) % 10])
    for y in range(5, 10):
        for x in range(15, 20):
            if x in (15, 19) or y in (5, 9):
                put(x, y, styles[(x+y) % 10])
    # Adjacent families stay separate, with three styles of wall geometry.
    for i, style in enumerate("u.xAu.x"):
        put(i+2, 14, style)
    # All cliff module families remain visible beside the new ground family.
    for y in range(12, 15):
        for x in range(15, 18):
            put(x, y, "u" if (x+y) % 2 else "x")
    put(20, 12, "u")
    for y in range(13, 16):
        put(20, y, "x")
    top = [[" "]*width for _ in range(height)]
    top[8][1] = "C"
    top[11][4] = "E"
    header = '@camera {"pitch":45,"yaw":-25}\n@groundsplat {"base":"GroundGrass","color":[0.25,0.75,0.35],"detail":"GroundGrass","mask":"GroundSplatMap0_0","name":"Meadow"}\n'
    text = header + "\n@layer 0\n" + "\n".join("".join(row) for row in grid)
    text += "\n\n@layer 1\n" + "\n".join("".join(row) for row in top) + "\n"
    target = destination / "level0/screen0.scr"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    write_json(target.parent / "metadata.json", {"format": 1, "name": "Native ground modules", "screens": [""]})
    occupancy = {(x, y) for y, row in enumerate(grid) for x, character in enumerate(row) if character in styles}
    masks = Counter()
    for x, y in occupancy:
        mask = 15
        for side, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
            if (x+dx, y+dy) in occupancy:
                mask &= ~(1 << side)
        masks[mask] += 1
    require(set(masks) == set(range(16)), "Evidence fixture does not cover all exposure masks")
    return {"screen": target.relative_to(ROOT).as_posix(), "width": width, "height": height,
            "ground_cells": sum(c in styles for row in grid for c in row),
            "cliff_cells": sum(c in "ux" for row in grid for c in row),
            "ground_mask_cell_counts": dict(sorted(masks.items())), "original_levels_changed": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--models", type=Path, default=DEFAULT_MODELS)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--source-audit", type=Path, default=DEFAULT_SOURCE_AUDIT,
                        help="Actual Blender object snapshot; checked when this JSON exists")
    parser.add_argument("--record-baseline", action="store_true")
    parser.add_argument("--write-fixture", type=Path)
    parser.add_argument("--fixture-only", action="store_true")
    args = parser.parse_args()
    if args.record_baseline:
        print(json.dumps(record_baseline(args.baseline)))
        return
    fixture = write_fixture(args.write_fixture) if args.write_fixture else None
    if args.fixture_only:
        require(fixture is not None, "--fixture-only requires --write-fixture")
        print(json.dumps(fixture))
        return
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    require(contract["format"] == 2 and contract["nativeCapCount"] == 250
            and contract["preserveOriginalXY"] is True, "Unexpected native broad-rim contract")
    original_count = verify_baseline(args.baseline)
    loaded, models = {}, []
    for family in contract["families"]:
        for variant in range(10):
            path = args.models / f'ground_{family["name"]}_{variant+1:02}.glb'
            primitives, report = audit_body_v2(path, family, variant, contract)
            loaded[(family["name"], variant)] = primitives
            models.append(report)
    cap_path = Path(os.path.abspath(args.contract.parent/contract["capSurfaces"])).resolve()
    caps, native_caps = read_native_caps(cap_path, contract)
    topology = audit_caps_v2(caps, contract)
    closure = audit_body_cap_closure_v2(loaded, caps, contract)
    source = audit_native_source_v2(args.source_audit, args.contract, contract, loaded, caps) if args.source_audit.is_file() else None
    result = {"status": "PASS", "contract_format": 2, "native_models": len(models), "identity_node_models": len(models),
              "preserved_original_model_and_map_hashes": original_count,
              "cap_surfaces_sha256": digest(cap_path), "native_caps": native_caps,
              "caps_and_joins": topology, "body_cap_closure": closure,
              "interior_bodies_have_only_two_bottom_triangles": True,
              "hidden_side_geometry_removed_in_native_files": True, "models": models}
    if source:
        result["native_source"] = source
    if fixture:
        result["fixture"] = fixture
    write_json(args.output, result)
    print(json.dumps({key: value for key, value in result.items() if key != "models"}))


if __name__ == "__main__":
    main()
