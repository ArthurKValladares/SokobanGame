"""Check the skipping rock's runtime geometry and painted material contract."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from pbr_gltf import Document, palette_sample


class RoundRockModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = Document(ROOT / "assets/custom/models/round_rock.glb")
        cls.data = cls.document.data
        cls.primitive = cls.data["meshes"][0]["primitives"][0]

    def test_single_closed_round_mass_has_valid_runtime_streams(self):
        self.assertEqual(len(self.data["meshes"]), 1)
        self.assertEqual(len(self.data["meshes"][0]["primitives"]), 1)
        self.assertEqual(self.data["nodes"], [{"mesh": 0, "name": "RoundRock"}])
        self.assertFalse(self.data.get("animations"))
        self.assertFalse(self.data.get("skins"))
        p = self.primitive
        positions = self.document.accessor(p["attributes"]["POSITION"])
        normals = self.document.accessor(p["attributes"]["NORMAL"])
        uvs = self.document.accessor(p["attributes"]["TEXCOORD_0"])
        indices = self.document.accessor(p["indices"]).reshape(-1)
        self.assertEqual(len(positions), len(normals))
        self.assertEqual(len(positions), len(uvs))
        self.assertTrue(np.isfinite(positions).all())
        self.assertTrue(np.isfinite(normals).all())
        self.assertTrue(np.isfinite(uvs).all())
        self.assertTrue(np.allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-5))
        self.assertTrue(((uvs >= 0) & (uvs <= 1)).all())
        self.assertTrue((indices < len(positions)).all())
        self.assertLess(len(indices) // 3, 7000)
        triangles = positions[indices.reshape(-1, 3)]
        areas = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                        triangles[:, 2] - triangles[:, 0]), axis=1)
        self.assertTrue((areas > 1e-10).all())
        # UV and normal seams duplicate runtime vertices: weld by position to
        # inspect the real shell instead of treating the atlas as open edges.
        welded = [tuple(round(float(v), 6) for v in point) for point in positions]
        edge_counts = {}
        for a, b, c in indices.reshape(-1, 3):
            for start, end in ((a, b), (b, c), (c, a)):
                edge = tuple(sorted((welded[start], welded[end])))
                edge_counts[edge] = edge_counts.get(edge, 0) + 1
        self.assertTrue(all(count == 2 for count in edge_counts.values()))
        dimensions = positions.max(axis=0) - positions.min(axis=0)
        self.assertLess(max(dimensions) / min(dimensions), 1.12)
        self.assertGreater(max(dimensions) / min(dimensions), 1.03)
        self.assertAlmostEqual(float(positions[:, 1].min()), 0, places=6)
        self.assertLess(float(positions[:, 1].max()), .95)

    def test_material_retains_regular_rock_palette_in_embedded_texture(self):
        self.assertEqual(len(self.data["materials"]), 1)
        material = self.data["materials"][0]["pbrMetallicRoughness"]
        self.assertEqual(material["metallicFactor"], 0)
        self.assertGreater(material["roughnessFactor"], .9)
        self.assertTrue(all("bufferView" in image for image in self.data["images"]))
        image = self.document.image(material["baseColorTexture"]["index"])
        self.assertEqual(image.size, (1024, 1024))
        positions = self.document.accessor(self.primitive["attributes"]["POSITION"])
        uvs = self.document.accessor(self.primitive["attributes"]["TEXCOORD_0"])
        colors = palette_sample(image, uvs).astype(float)
        crown = np.median(colors[positions[:, 1] > .70], axis=0)
        base = np.median(colors[positions[:, 1] < .25], axis=0)
        self.assertGreater(crown[0], crown[2] * 1.25)
        self.assertGreater(base[2], base[0] * 1.25)

    def test_manifest_matches_regular_rock_scale_and_retains_art(self):
        manifest = json.loads((ROOT / "assets/manifest.json").read_text())
        regular = next(m for m in manifest["models"] if m["name"] == "Stone")
        model = next(m for m in manifest["models"] if m["name"] == "RoundRock")
        self.assertEqual(model["path"], "custom/models/round_rock.glb")
        self.assertEqual(model["preserveAspectRatio"], regular["preserveAspectRatio"])
        tile = next(t for t in manifest["tiles"] if t["tile"] == "Round Rock")
        regular_tile = next(t for t in manifest["tiles"] if t["tile"] == "Rock")
        self.assertEqual(tile["model"], "RoundRock")
        self.assertEqual(tile["scale"], regular_tile["scale"])
        ownership = json.loads((ROOT / "tools/pbr_art_pass.json").read_text())
        retained = next(m for m in ownership["models"] if m["name"] == "RoundRock")
        self.assertEqual(retained["ownership"], "retained")
        self.assertEqual(retained["source"], model["path"])


if __name__ == "__main__":
    unittest.main()
