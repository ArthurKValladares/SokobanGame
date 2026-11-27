"""The dust puff sprites are what tools/make_dust_puffs.py draws."""
import importlib.util
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "make_dust_puffs", ROOT / "tools/make_dust_puffs.py")
dust = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dust)


class DustPuffTests(unittest.TestCase):
    def test_shipped_sprites_are_the_ones_the_script_draws(self):
        self.assertEqual(len(dust.SEEDS), 4)
        for number, seed in enumerate(dust.SEEDS, start=1):
            with Image.open(dust.TEXTURES / f"dust_puff_{number:02d}.png") as image:
                self.assertEqual(image.mode, "RGBA")
                shipped = np.asarray(image).astype(int)
            drawn = dust.puff(seed).astype(int)
            self.assertEqual(shipped.shape, drawn.shape)
            # One level of slack: a rounding that falls the other way on
            # another machine's maths library is not a different sprite.
            self.assertLessEqual(int(np.abs(shipped - drawn).max()), 1, number)

    def test_a_puff_fills_most_of_its_quad_and_stops_short_of_its_edge(self):
        for seed in dust.SEEDS:
            rgba = dust.puff(seed)
            alpha = rgba[..., 3]
            self.assertEqual(rgba.shape, (dust.SIZE, dust.SIZE, 4))
            for edge in (alpha[0], alpha[-1], alpha[:, 0], alpha[:, -1]):
                self.assertEqual(int(edge.max()), 0, seed)
            self.assertEqual(int(alpha.max()), 255, seed)
            rows = np.flatnonzero(alpha.max(axis=1) > 127)
            columns = np.flatnonzero(alpha.max(axis=0) > 127)
            # The particle's size is the quad's: a sprite that left half of
            # it empty would be drawn at half the size its effect asks for.
            self.assertGreater(rows[-1] - rows[0], dust.SIZE * 0.65, seed)
            self.assertGreater(columns[-1] - columns[0], dust.SIZE * 0.65, seed)

    def test_a_puff_is_white_for_the_particle_to_tint(self):
        for seed in dust.SEEDS:
            rgba = dust.puff(seed)
            self.assertTrue(np.array_equal(rgba[..., 0], rgba[..., 1]), seed)
            self.assertTrue(np.array_equal(rgba[..., 0], rgba[..., 2]), seed)
            body = rgba[..., 0][rgba[..., 3] > 0]
            self.assertGreaterEqual(int(body.min()), 200, seed)
            self.assertEqual(int(body.max()), 255, seed)

    def test_no_two_puffs_are_the_same_shape(self):
        shapes = [dust.puff(seed)[..., 3] > 127 for seed in dust.SEEDS]
        for first in range(len(shapes)):
            for second in range(first + 1, len(shapes)):
                differing = np.count_nonzero(shapes[first] != shapes[second])
                self.assertGreater(differing, dust.SIZE * dust.SIZE * 0.03, (first, second))


if __name__ == "__main__":
    unittest.main()
