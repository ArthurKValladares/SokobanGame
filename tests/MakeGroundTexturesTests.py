"""New generated masks use four material weights without changing old paint."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "make_ground_textures", ROOT / "tools/make_ground_textures.py")
ground = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ground)


class GroundMaskTests(unittest.TestCase):
    def test_generated_rgba_mask_keeps_grass_rock_mix_and_zero_extra_layers(self):
        width, height = 12, 8
        rows = ground.generate_splat(width, height, 123)
        self.assertEqual(len(rows), height)
        values = set()
        for row in rows:
            self.assertEqual(len(row), width * 4)
            for offset in range(0, len(row), 4):
                red, green, blue, alpha = row[offset:offset + 4]
                self.assertEqual(red + green, 255)
                self.assertEqual((blue, alpha), (0, 0))
                values.add(green)
        self.assertGreater(len(values), 8)
        with tempfile.TemporaryDirectory(dir=ROOT / "out") as directory:
            path = Path(directory) / "mask.png"
            ground.write_png(path, rows, width, height, rgba=True)
            with Image.open(path) as image:
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(image.size, (width, height))
                self.assertEqual(image.tobytes(), b"".join(rows))


if __name__ == "__main__":
    unittest.main()
