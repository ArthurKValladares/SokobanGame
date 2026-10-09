#!/usr/bin/env python3
"""Serialize Blender-authored ground cap surfaces into C++ literals.

This tool validates and copies existing vertices, faces, normals, and paint
weights. It never creates, triangulates, deforms, or repairs geometry. Blender's
authored JSON is the source of truth; --check detects a stale generated header.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from typing import Any, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = REPOSITORY_ROOT / "assets/custom/source/ground_modules/cap_surfaces.json"
DEFAULT_OUTPUT = REPOSITORY_ROOT / "src/engine/GroundTileCapData.hpp"
CANONICAL_EXPOSED_MASKS = (15, 11, 5, 3, 1, 0)
CANONICAL_CONCAVE_MASKS = (0, 0, 0, 8, 12, 15)
VARIANT_COUNT = 10
PATCH_CAPACITY = 38
RECORD_COUNT = 250
SOURCE_FORMAT = 2
INVALID_INDEX = 65535
PLANAR_TOLERANCE = 1e-5
NORMAL_LENGTH_TOLERANCE = 1e-3
NORMAL_ALIGNMENT_MINIMUM = 0.98
MINIMUM_DOUBLE_AREA = 1e-12

Vector = tuple[float, float, float]


class CapDataError(ValueError):
    """The authored source does not satisfy the native cap contract."""


@dataclass(frozen=True)
class Patch:
    vertices: tuple[Vector, Vector, Vector, Vector]
    wall_coverage: tuple[float, float, float, float]
    normal: Vector


@dataclass(frozen=True)
class CapRecord:
    variant: int
    shape_index: int
    exposed_side_mask: int
    concave_corners: int
    object_name: str
    patches: tuple[Patch, ...]


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CapDataError(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def _finite_json_float(value: str) -> float:
    number = float(value)
    if not math.isfinite(number):
        raise CapDataError(f"nonfinite JSON number: {value}")
    return number


def _reject_json_constant(value: str) -> None:
    raise CapDataError(f"nonfinite JSON number: {value}")


def _required_fields(value: Any, fields: Sequence[str], context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CapDataError(f"{context}: expected an object")
    missing = [field for field in fields if field not in value]
    if missing:
        raise CapDataError(f"{context}: missing required fields: {', '.join(missing)}")
    # Blender may add provenance or other metadata alongside the required data.
    return value


def _integer(value: Any, minimum: int, maximum: int, context: str) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise CapDataError(f"{context}: expected an integer in [{minimum}, {maximum}]")
    return value


def _number(value: Any, context: str) -> float:
    if type(value) not in (int, float):
        raise CapDataError(f"{context}: expected a finite number")
    try:
        number = float(value)
        rounded = struct.unpack("<f", struct.pack("<f", number))[0]
    except (OverflowError, struct.error):
        raise CapDataError(f"{context}: number is outside the finite float32 range") from None
    if not math.isfinite(number) or not math.isfinite(rounded):
        raise CapDataError(f"{context}: expected a finite float32 number")
    if number != 0.0 and rounded == 0.0:
        raise CapDataError(f"{context}: number underflows float32")
    return number


def _vector(value: Any, context: str) -> Vector:
    if not isinstance(value, list) or len(value) != 3:
        raise CapDataError(f"{context}: expected three coordinates")
    return tuple(_number(component, f"{context}[{index}]")
                 for index, component in enumerate(value))  # type: ignore[return-value]


def _subtract(left: Vector, right: Vector) -> Vector:
    return tuple(a - b for a, b in zip(left, right))  # type: ignore[return-value]


def _cross(left: Vector, right: Vector) -> Vector:
    return (left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0])


def _dot(left: Vector, right: Vector) -> float:
    return sum(a * b for a, b in zip(left, right))


def _length(value: Vector) -> float:
    return math.sqrt(_dot(value, value))


def _triangle_normal(vertices: Sequence[Vector], indices: tuple[int, int, int],
                     context: str) -> Vector:
    a, b, c = (vertices[index] for index in indices)
    cross = _cross(_subtract(b, a), _subtract(c, a))
    length = _length(cross)
    if not math.isfinite(length) or length <= MINIMUM_DOUBLE_AREA:
        raise CapDataError(f"{context}: degenerate face triangle")
    return tuple(component / length for component in cross)  # type: ignore[return-value]


def _validate_face(vertices: Sequence[Vector], normal: Vector, context: str) -> None:
    normal_length = _length(normal)
    if abs(normal_length - 1.0) > NORMAL_LENGTH_TOLERANCE:
        raise CapDataError(f"{context}: normal must be normalized within {NORMAL_LENGTH_TOLERANCE}")
    unit_normal = tuple(component / normal_length for component in normal)
    first_triangle = _triangle_normal(vertices, (0, 1, 2), context)
    if _dot(first_triangle, unit_normal) < NORMAL_ALIGNMENT_MINIMUM:
        raise CapDataError(f"{context}: normal disagrees with face winding")
    if len(vertices) == 4:
        plane_distance = abs(_dot(_subtract(vertices[3], vertices[0]), first_triangle))
        if plane_distance > PLANAR_TOLERANCE:
            raise CapDataError(f"{context}: nonplanar quad (distance {plane_distance:.8g})")
        second_triangle = _triangle_normal(vertices, (0, 2, 3), context)
        if _dot(second_triangle, unit_normal) < NORMAL_ALIGNMENT_MINIMUM:
            raise CapDataError(f"{context}: quad has inconsistent winding")


def _record(value: Any, record_index: int) -> CapRecord:
    context = f"models[{record_index}]"
    fields = _required_fields(value, (
        "variant", "shapeIndex", "exposedSideMask", "concaveCorners", "object",
        "vertices", "faces", "normals", "wallCoverage"), context)
    variant = _integer(fields["variant"], 0, VARIANT_COUNT - 1, f"{context}.variant")
    shape_index = _integer(fields["shapeIndex"], 0, 5, f"{context}.shapeIndex")
    exposed = _integer(fields["exposedSideMask"], 0, 15, f"{context}.exposedSideMask")
    if exposed != CANONICAL_EXPOSED_MASKS[shape_index]:
        raise CapDataError(f"{context}: exposedSideMask is not canonical for shapeIndex {shape_index}")
    concave = _integer(fields["concaveCorners"], 0, 15, f"{context}.concaveCorners")
    if concave & ~CANONICAL_CONCAVE_MASKS[shape_index]:
        raise CapDataError(f"{context}: concaveCorners is not a canonical subset for shapeIndex {shape_index}")
    object_name = fields["object"]
    if not isinstance(object_name, str) or not object_name.strip():
        raise CapDataError(f"{context}.object: expected a nonempty Blender object name")
    source_vertices = fields["vertices"]
    if not isinstance(source_vertices, list) or len(source_vertices) < 3:
        raise CapDataError(f"{context}.vertices: expected at least three vertices")
    vertices = [_vector(vertex, f"{context}.vertices[{index}]")
                for index, vertex in enumerate(source_vertices)]
    faces = fields["faces"]
    if not isinstance(faces, list) or not 1 <= len(faces) <= PATCH_CAPACITY:
        raise CapDataError(f"{context}.faces: expected 1 to {PATCH_CAPACITY} faces")
    normals = fields["normals"]
    coverage = fields["wallCoverage"]
    if not isinstance(normals, list) or len(normals) != len(faces):
        raise CapDataError(f"{context}.normals: expected one normal per face")
    if not isinstance(coverage, list) or len(coverage) != len(faces):
        raise CapDataError(f"{context}.wallCoverage: expected one weight list per face")
    patches: list[Patch] = []
    for index, face in enumerate(faces):
        face_context = f"{context}.faces[{index}]"
        if not isinstance(face, list) or len(face) not in (3, 4):
            raise CapDataError(f"{face_context}: expected three or four indices")
        indices = [_integer(vertex, 0, len(vertices) - 1, f"{face_context}[{corner}]")
                   for corner, vertex in enumerate(face)]
        if len(set(indices)) != len(indices):
            raise CapDataError(f"{face_context}: repeated vertex indices make a degenerate face")
        face_vertices = [vertices[vertex] for vertex in indices]
        normal = _vector(normals[index], f"{context}.normals[{index}]")
        _validate_face(face_vertices, normal, face_context)
        weights = coverage[index]
        if not isinstance(weights, list) or len(weights) != len(indices):
            raise CapDataError(f"{context}.wallCoverage[{index}]: expected one weight per face vertex")
        face_weights = [_number(weight, f"{context}.wallCoverage[{index}][{corner}]")
                        for corner, weight in enumerate(weights)]
        if any(not 0.0 <= weight <= 1.0 for weight in face_weights):
            raise CapDataError(f"{context}.wallCoverage[{index}]: weights must be in [0, 1]")
        if len(indices) == 3:
            # Existing quad draw/pick paths represent a triangle by repeating
            # its final authored vertex; the extra triangle has zero area.
            face_vertices.append(face_vertices[-1])
            face_weights.append(face_weights[-1])
        patches.append(Patch(tuple(face_vertices), tuple(face_weights), normal))  # type: ignore[arg-type]
    return CapRecord(variant, shape_index, exposed, concave, object_name, tuple(patches))


def load_records(source_bytes: bytes) -> tuple[CapRecord, ...]:
    """Load the complete authored contract, rejecting corrupt/ambiguous data."""
    try:
        document = json.loads(source_bytes.decode("utf-8-sig"), object_pairs_hook=_unique_object,
                              parse_float=_finite_json_float, parse_constant=_reject_json_constant)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CapDataError(f"invalid UTF-8 JSON: {error}") from None
    root = _required_fields(document, ("format", "source", "models"), "root")
    if type(root["format"]) is not int or root["format"] != SOURCE_FORMAT:
        raise CapDataError(f"root.format: expected format {SOURCE_FORMAT}")
    if not isinstance(root["source"], (str, dict)):
        raise CapDataError("root.source: expected a provenance string or object")
    models = root["models"]
    if not isinstance(models, list) or len(models) != RECORD_COUNT:
        raise CapDataError(f"root.models: expected exactly {RECORD_COUNT} authored records")
    records: dict[tuple[int, int, int], CapRecord] = {}
    for index, value in enumerate(models):
        record = _record(value, index)
        key = (record.variant, record.shape_index, record.concave_corners)
        if key in records:
            raise CapDataError(f"models[{index}]: duplicate variant/shape/concave key {key}")
        records[key] = record
    expected = {(variant, shape, concave)
                for variant in range(VARIANT_COUNT)
                for shape, allowed in enumerate(CANONICAL_CONCAVE_MASKS)
                for concave in range(16) if concave & ~allowed == 0}
    missing = sorted(expected - records.keys())
    if missing:
        raise CapDataError(f"root.models: missing variant/shape/concave keys: {missing}")
    return tuple(records[key] for key in sorted(records))


def _float_literal(value: float) -> str:
    # C++ stores these values as float32. Choose the shortest significant-digit
    # spelling that roundtrips to those exact bits, including the sign of zero.
    bits = struct.pack("<f", value)
    rounded = struct.unpack("<f", bits)[0]
    for precision in range(1, 10):
        literal = format(rounded, f".{precision}g")
        try:
            if struct.pack("<f", float(literal)) != bits:
                continue
        except OverflowError:
            continue
        if "e" in literal:
            significand, exponent = literal.split("e")
            # Leading exponent zeroes and a positive sign add no precision.
            literal = f"{significand}e{int(exponent)}"
        elif "." not in literal:
            literal += ".0"
        return literal + "f"
    raise CapDataError("float32 value could not be serialized without changing its bits")


def _vector_literal(value: Vector) -> str:
    return "Vec3{" + ",".join(f".{axis}={_float_literal(component)}"
                              for axis, component in zip("xyz", value)) + "}"


def serialize_header(records: Sequence[CapRecord], source_sha256: str) -> str:
    """Emit literal authored surfaces and a lookup table, without geometry math."""
    if len(records) != RECORD_COUNT or len(source_sha256) != 64 or any(
            character not in "0123456789abcdef" for character in source_sha256):
        raise CapDataError("serialization requires 250 validated records and the source SHA-256")
    lines = ["#pragma once", "", "// Generated by tools/serialize_ground_caps.py from Blender-authored cap surfaces.",
             f"// Source JSON SHA-256: {source_sha256}", "// Do not edit; re-export the source JSON and run the serializer.",
             "", '#include "engine/GroundTileCapTypes.hpp"', "", "#include <array>", "#include <cstdint>",
             "", "namespace sokoban {", "", "inline constexpr std::array<GroundTileNativeCapRecord, 250> groundTileNativeCaps {{"]
    lookup = [[[INVALID_INDEX] * 16 for _ in range(6)] for _ in range(VARIANT_COUNT)]
    for index, record in enumerate(records):
        lookup[record.variant][record.shape_index][record.concave_corners] = index
        lines.extend([
            "    GroundTileNativeCapRecord {",
            f"        .variant = {record.variant},",
            f"        .shapeIndex = {record.shape_index},",
            f"        .exposedSideMask = {record.exposed_side_mask},",
            f"        .concaveCorners = {record.concave_corners},",
            "        .surface = GroundTileCapSurface {",
            "            .patches = {{",
        ])
        for patch in record.patches:
            vertices = ", ".join(_vector_literal(vertex) for vertex in patch.vertices)
            weights = ", ".join(_float_literal(weight) for weight in patch.wall_coverage)
            lines.append("                GroundTileSurfacePatch { .vertices = {{ " + vertices +
                         " }}, .wallCoverage = {{ " + weights + " }}, .normal = " +
                         _vector_literal(patch.normal) + " },")
        lines.extend([
            "            }},",
            f"            .count = {len(record.patches)},",
            "        },",
            "    },",
        ])
    lines.extend(["}};", "", "// 65535 marks a concave-corner mask that is invalid for the canonical shape.",
                  "inline constexpr std::array<std::array<std::array<std::uint16_t, 16>, 6>, 10>",
                  "    groundTileNativeCapIndices {{"])
    for shapes in lookup:
        lines.append("    std::array<std::array<std::uint16_t, 16>, 6> {{")
        for indices in shapes:
            lines.append("        std::array<std::uint16_t, 16> {{ " +
                         ", ".join(str(index) for index in indices) + " }},")
        lines.append("    }},")
    lines.extend(["}};", "", "} // namespace sokoban", ""])
    return "\n".join(lines)


def main(arguments: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT,
                        help=f"authored JSON source (default: {DEFAULT_INPUT})")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help=f"literal C++ header (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--check", action="store_true",
                        help="compare against the existing header without writing")
    arguments = parser.parse_args(arguments)
    try:
        source_bytes = arguments.input.read_bytes()
        records = load_records(source_bytes)
        content = serialize_header(records, hashlib.sha256(source_bytes).hexdigest())
        if arguments.check:
            if not arguments.output.is_file():
                raise CapDataError(f"generated header is missing: {arguments.output}")
            if arguments.output.read_bytes() != content.encode("utf-8"):
                raise CapDataError(f"generated header is stale: {arguments.output}; run without --check")
            print(f"Verified {len(records)} authored cap records: {arguments.output}")
        else:
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            temporary = arguments.output.with_name(arguments.output.name + ".tmp")
            try:
                temporary.write_bytes(content.encode("utf-8"))
                temporary.replace(arguments.output)
            finally:
                temporary.unlink(missing_ok=True)
            print(f"Serialized {len(records)} authored cap records: {arguments.output}")
    except (CapDataError, OSError) as error:
        print(f"serialize_ground_caps: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
