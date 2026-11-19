"""Verify the authored lily pad contract without starting Blender or the game."""
from __future__ import annotations

import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_lily_pad_model as MODEL


def glb_chunks(raw):
    """Split a GLB into its parsed JSON document and its binary payload."""
    length, _ = struct.unpack_from("<II", raw, 12)
    return json.loads(raw[20:20 + length]), raw[28 + length:]


class LilyPadModelTests(unittest.TestCase):
    def assert_same_document(self, rebuilt, published, path="$"):
        """Equal JSON, with numbers compared to the last few digits.

        The accessor bounds are written as Python doubles straight from
        trigonometry, and math libraries differ in the final digits between
        platforms. The asset was published from Windows; a rebuild on Linux
        prints one bound a few units in the last place away.
        """
        if isinstance(published, float) or isinstance(rebuilt, float):
            self.assertIsInstance(rebuilt, (int, float), path)
            self.assertIsInstance(published, (int, float), path)
            self.assertTrue(
                math.isclose(rebuilt, published, rel_tol=1e-12, abs_tol=1e-15),
                f"{path}: {rebuilt!r} != {published!r}")
        elif isinstance(published, dict):
            self.assertIsInstance(rebuilt, dict, path)
            self.assertEqual(sorted(rebuilt), sorted(published), path)
            for key, value in published.items():
                self.assert_same_document(rebuilt[key], value, f"{path}.{key}")
        elif isinstance(published, list):
            self.assertIsInstance(rebuilt, list, path)
            self.assertEqual(len(rebuilt), len(published), path)
            for index, value in enumerate(published):
                self.assert_same_document(rebuilt[index], value, f"{path}[{index}]")
        else:
            self.assertEqual(rebuilt, published, path)

    def test_rebuild_matches_published_runtime_asset(self):
        meshes = MODEL.geometry()
        MODEL.validate_geometry(meshes)
        temporary_parent = ROOT / "out/lily-pad-tests"
        temporary_parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_parent) as directory:
            output = Path(directory) / "lily_pad.glb"
            MODEL.write_glb(meshes, output)
            rebuilt_document, rebuilt_binary = glb_chunks(output.read_bytes())
        published_document, published_binary = glb_chunks(MODEL.OUTPUT.read_bytes())
        # Every vertex, index and texel stream is stored as 32-bit values and
        # has to match exactly.
        self.assertEqual(rebuilt_binary, published_binary)
        self.assert_same_document(rebuilt_document, published_document)

    def test_runtime_glb_preserves_dimensions_materials_and_valid_streams(self):
        raw = MODEL.OUTPUT.read_bytes()
        self.assertEqual(struct.unpack_from("<III", raw), (0x46546C67, 2, len(raw)))
        length, kind = struct.unpack_from("<II", raw, 12)
        self.assertEqual(kind, 0x4E4F534A)
        document = json.loads(raw[20:20 + length])
        binary_length, binary_kind = struct.unpack_from("<II", raw, 20 + length)
        self.assertEqual(binary_kind, 0x004E4942)
        binary = raw[28 + length:]
        self.assertEqual(binary_length, len(binary))
        self.assertEqual(document["buffers"][0]["byteLength"], len(binary))

        def stream(index, width, format="f"):
            accessor = document["accessors"][index]
            view = document["bufferViews"][accessor["bufferView"]]
            offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            end = offset + accessor["count"] * width * 4
            self.assertLessEqual(end, len(binary))
            return list(struct.iter_unpack(f"<{width}{format}", binary[offset:end]))

        positions = []
        for primitive in document["meshes"][0]["primitives"]:
            self.assertEqual(primitive["mode"], 4)
            vertices = stream(primitive["attributes"]["POSITION"], 3)
            positions.extend((x, -z, y) for x, y, z in vertices)
            normals = stream(primitive["attributes"]["NORMAL"], 3)
            uvs = stream(primitive["attributes"]["TEXCOORD_0"], 2)
            self.assertEqual(len(vertices), len(normals))
            self.assertEqual(len(vertices), len(uvs))
            self.assertTrue(all(math.isclose(sum(v * v for v in n), 1, abs_tol=1e-6) for n in normals))
            self.assertTrue(all(0 <= uv[0] <= 1 and 0 <= uv[1] <= 1 for uv in uvs))
            self.assertTrue(all(i[0] < len(vertices) for i in stream(primitive["indices"], 1, "I")))
        self.assertAlmostEqual(min(p[2] for p in positions), 0)
        self.assertLess(max(p[2] for p in positions), .09)
        self.assertTrue(any(abs(x - .5) < 1e-6 and abs(y - .5) < 1e-6 and
                            abs(z - .07) < 1e-6 for x, y, z in positions))
        self.assertTrue(all(0 < x < 1 and 0 < y < 1 for x, y, z in positions))
        self.assertGreater(max(p[0] for p in positions) - min(p[0] for p in positions), .88)
        colors = {m["name"]: m["pbrMetallicRoughness"]["baseColorFactor"]
                  for m in document["materials"]}
        self.assertGreater(colors["Pale leaf veins"][1], colors["Leaf jade"][1])
        self.assertLess(colors["Dark underside"][1], colors["Leaf jade"][1])

    def test_notch_is_open_and_manifest_keeps_authored_scale(self):
        meshes = MODEL.geometry()
        # No solid-body triangle crosses the removed south-facing wedge.
        for name, mesh in meshes.items():
            if name == "Pale leaf veins":
                continue
            for index in range(0, len(mesh.positions), 3):
                triangle = mesh.positions[index:index + 3]
                x = sum(p[0] for p in triangle) / 3 - .5
                y = sum(p[1] for p in triangle) / 3 - .5
                self.assertFalse(y > .10 and abs(x) < y * math.tan(math.radians(20)))
        manifest = json.loads((ROOT / "assets/manifest.json").read_text())
        model = next(m for m in manifest["models"] if m["name"] == "LilyPad")
        self.assertEqual(model["path"], "custom/models/lily_pad.glb")
        self.assertTrue(model["preserveSourceScale"])
        tile = next(t for t in manifest["tiles"] if t["tile"] == "Lily Pad")
        self.assertEqual(tile["model"], "LilyPad")
        plan = json.loads((ROOT / "tools/pbr_art_pass.json").read_text())
        retained = next(m for m in plan["models"] if m["name"] == "LilyPad")
        self.assertEqual(retained["ownership"], "retained")
        self.assertEqual(retained["source"], model["path"])


if __name__ == "__main__":
    unittest.main()
