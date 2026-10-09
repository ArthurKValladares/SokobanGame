"""Focused serializer fixtures; these do not generate production geometry."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "tools/serialize_ground_caps.py"
SPEC = importlib.util.spec_from_file_location("serialize_ground_caps", SCRIPT_PATH)
SERIALIZER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SERIALIZER
SPEC.loader.exec_module(SERIALIZER)


def fixture() -> dict:
    models = []
    for variant in range(10):
        for shape, allowed in enumerate((0, 0, 0, 8, 12, 15)):
            for concave in range(16):
                if concave & ~allowed:
                    continue
                models.append({
                    "variant": variant, "shapeIndex": shape,
                    "exposedSideMask": (15, 11, 5, 3, 1, 0)[shape],
                    "concaveCorners": concave, "object": f"fixture_{variant}_{shape}_{concave}",
                    "vertices": [[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]],
                    "faces": [[0, 1, 2, 3]], "normals": [[0, 0, 1]],
                    "wallCoverage": [[0, 0.25, 0.5, 1]],
                })
    return {"format": 2, "source": {"fixture": "serialization only"}, "models": models}


def encode(document: dict) -> bytes:
    return json.dumps(document).encode("utf-8")


class SerializeGroundCapsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.document = fixture()

    def reject(self, message: str) -> None:
        with self.assertRaisesRegex(SERIALIZER.CapDataError, message):
            SERIALIZER.load_records(encode(self.document))

    def test_all_records_are_sorted_and_literal(self) -> None:
        self.document["models"].reverse()
        self.document["extraProvenance"] = {"author": "fixture"}
        self.document["models"][0]["metadata"] = {"collection": "fixture"}
        source = encode(self.document)
        records = SERIALIZER.load_records(source)
        self.assertEqual(len(records), 250)
        keys = [(record.variant, record.shape_index, record.concave_corners) for record in records]
        self.assertEqual(keys, sorted(keys))
        header = SERIALIZER.serialize_header(records, hashlib.sha256(source).hexdigest())
        self.assertIn(hashlib.sha256(source).hexdigest(), header)
        self.assertEqual(header.count("    GroundTileNativeCapRecord {"), 250)
        self.assertIn("groundTileNativeCapIndices", header)
        self.assertIn("0, 65535, 65535", header)
        self.assertEqual(header, SERIALIZER.serialize_header(records, hashlib.sha256(source).hexdigest()))

    def test_triangle_repeats_last_authored_vertex_and_weight(self) -> None:
        self.document["models"][0]["faces"] = [[0, 1, 2]]
        self.document["models"][0]["wallCoverage"] = [[0, 0.25, 0.75]]
        patch = SERIALIZER.load_records(encode(self.document))[0].patches[0]
        self.assertEqual(patch.vertices[-1], patch.vertices[-2])
        self.assertEqual(patch.wall_coverage, (0, 0.25, 0.75, 0.75))
        self.assertEqual(patch.normal, (0, 0, 1))

    def test_compact_float_literals_preserve_exact_float32_bits(self) -> None:
        generator = random.Random(0xCA9)
        bit_patterns = [0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000,
                        0x3F800000, 0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF]
        bit_patterns.extend(generator.getrandbits(32) for _ in range(5000))
        for pattern in bit_patterns:
            bits = struct.pack("<I", pattern)
            value = struct.unpack("<f", bits)[0]
            if not math.isfinite(value):
                continue
            with self.subTest(pattern=f"{pattern:08x}"):
                literal = SERIALIZER._float_literal(value)
                self.assertTrue(literal.endswith("f"))
                self.assertTrue("." in literal or "e" in literal)
                self.assertEqual(struct.pack("<f", float(literal[:-1])), bits)
        self.assertEqual(SERIALIZER._float_literal(0.0), "0.0f")
        self.assertEqual(SERIALIZER._float_literal(-0.0), "-0.0f")
        self.assertEqual(SERIALIZER._float_literal(1.0), "1.0f")
        self.assertEqual(SERIALIZER._float_literal(0.10000000149011612), "0.1f")

    def test_surface_patches_use_one_designated_initializer_line(self) -> None:
        source = encode(self.document)
        header = SERIALIZER.serialize_header(SERIALIZER.load_records(source),
                                              hashlib.sha256(source).hexdigest())
        patches = [line for line in header.splitlines() if "GroundTileSurfacePatch {" in line]
        self.assertEqual(len(patches), 250)
        self.assertTrue(all(".vertices =" in line and ".wallCoverage =" in line and
                            ".normal =" in line for line in patches))

    def test_duplicate_json_key(self) -> None:
        with self.assertRaisesRegex(SERIALIZER.CapDataError, "duplicate JSON key"):
            SERIALIZER.load_records(b'{"format":1,"format":1}')

    def test_missing_field_and_wrong_format(self) -> None:
        del self.document["models"][0]["normals"]
        self.reject("missing required fields: normals")
        self.document = fixture()
        self.document["format"] = True
        self.reject("expected format 2")

    def test_record_count_and_duplicate_contract_key(self) -> None:
        self.document["models"].pop()
        self.reject("exactly 250")
        self.document = fixture()
        self.document["models"][-1] = copy.deepcopy(self.document["models"][0])
        self.reject("duplicate variant/shape/concave key")

    def test_canonical_masks_and_integral_keys(self) -> None:
        self.document["models"][0]["concaveCorners"] = 8
        self.reject("not a canonical subset")
        self.document = fixture()
        self.document["models"][0]["exposedSideMask"] = 3
        self.reject("not canonical")
        self.document = fixture()
        self.document["models"][0]["variant"] = 0.0
        self.reject("expected an integer")

    def test_invalid_index_and_degenerate_face(self) -> None:
        self.document["models"][0]["faces"] = [[0, 1, 4]]
        self.reject("expected an integer in")
        self.document = fixture()
        self.document["models"][0]["faces"] = [[0, 1, 1, 3]]
        self.reject("repeated vertex indices")
        self.document = fixture()
        self.document["models"][0]["vertices"][2] = [2, 0, 1]
        self.reject("degenerate face triangle")

    def test_nonfinite_and_unrepresentable_coordinates(self) -> None:
        for value, message in ((float("nan"), "nonfinite"), (float("inf"), "nonfinite"),
                               (1e100, "float32"), (1e-100, "underflows"), (True, "finite number")):
            with self.subTest(value=value):
                self.document = fixture()
                self.document["models"][0]["vertices"][0][0] = value
                self.reject(message)
        with self.assertRaisesRegex(SERIALIZER.CapDataError, "nonfinite JSON number"):
            SERIALIZER.load_records(b'{"metadata":1e400}')

    def test_normal_length_winding_and_quad_planarity(self) -> None:
        self.document["models"][0]["normals"] = [[0, 0, 0.5]]
        self.reject("must be normalized")
        self.document = fixture()
        self.document["models"][0]["normals"] = [[0, 0, -1]]
        self.reject("disagrees with face winding")
        self.document = fixture()
        self.document["models"][0]["vertices"][3][2] = 1.01
        self.reject("nonplanar quad")
        self.document = fixture()
        self.document["models"][0]["faces"] = [[0, 2, 1, 3]]
        self.reject("winding")

    def test_face_and_coverage_array_sizes(self) -> None:
        for field, value, message in (
                ("faces", [], "1 to 38"), ("faces", [[0, 1, 2, 3]] * 39, "1 to 38"),
                ("normals", [], "one normal per face"),
                ("wallCoverage", [], "one weight list per face"),
                ("wallCoverage", [[0, 1, 0]], "one weight per face vertex"),
                ("wallCoverage", [[0, 0, 0, 1.001]], "weights must be in")):
            with self.subTest(field=field, value=value):
                self.document = fixture()
                self.document["models"][0][field] = value
                self.reject(message)

    def test_check_mode_never_writes_and_reports_stale_or_missing(self) -> None:
        # Keep fixtures inside the workspace; redirected Windows sandbox TEMP
        # directories can be created but then deny access to their children.
        with tempfile.TemporaryDirectory(prefix="serialize_caps_", dir=Path(__file__).parent) as temporary:
            source = Path(temporary) / "fixture.json"
            output = Path(temporary) / "fixture.hpp"
            source.write_bytes(encode(self.document))
            arguments = ["--input", str(source), "--output", str(output)]
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(SERIALIZER.main(arguments + ["--check"]), 1)
                self.assertFalse(output.exists())
                self.assertEqual(SERIALIZER.main(arguments), 0)
                before = output.read_bytes()
                self.assertEqual(SERIALIZER.main(arguments + ["--check"]), 0)
                self.assertEqual(before, output.read_bytes())
                output.write_bytes(b"stale")
                self.assertEqual(SERIALIZER.main(arguments + ["--check"]), 1)
                self.assertEqual(output.read_bytes(), b"stale")
                self.document["models"][0]["wallCoverage"][0].pop()
                source.write_bytes(encode(self.document))
                self.assertEqual(SERIALIZER.main(arguments), 1)
                self.assertEqual(output.read_bytes(), b"stale")


if __name__ == "__main__":
    unittest.main()
