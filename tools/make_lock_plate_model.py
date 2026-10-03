#!/usr/bin/env python3
"""Generate a tintable rounded-square Lock Plate with a raised padlock."""
import math
from make_rotator_models import Primitive, write_glb, OUTPUT_DIRECTORY


def rounded_square(inset, radius):
    outline = []
    for cx, cy, start in (
        (1 - inset - radius, 1 - inset - radius, 0),
        (inset + radius, 1 - inset - radius, 90),
        (inset + radius, inset + radius, 180),
        (1 - inset - radius, inset + radius, 270),
    ):
        for step in range(13):
            angle = math.radians(start + 90 * step / 12)
            outline.append((cx + radius * math.cos(angle),
                            cy + radius * math.sin(angle)))
    return outline


def add_base(mesh):
    outer = rounded_square(0.0, 0.14)
    top = rounded_square(0.04, 0.10)
    mesh.fan((0.5, 0.5), outer, 0.0, up=False)
    mesh.prism_walls(outer, 0.0, 0.72)
    # A shallow bevel softens the perimeter and catches the scene lighting.
    for i in range(len(outer)):
        j = (i + 1) % len(outer)
        p, q = outer[i], outer[j]
        mesh.quad((*p, 0.72), (*q, 0.72), (*top[j], 1.0), (*top[i], 1.0),
                  (q[1] - p[1], p[0] - q[0], 1.0))
    mesh.fan((0.5, 0.5), top, 1.0, up=True)


def rectangle(mesh, x0, y0, x1, y1, z0, z1):
    outline = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    center = ((x0 + x1) / 2, (y0 + y1) / 2)
    mesh.fan(center, outline, z0, up=False)
    mesh.fan(center, outline, z1, up=True)
    mesh.prism_walls(outline, z0, z1)


def main():
    base, icon, keyhole = Primitive(), Primitive(), Primitive()
    add_base(base)
    # Flat padlock silhouette: shackle toward the back of the tile.
    rectangle(icon, 0.31, 0.43, 0.69, 0.72, 1.0, 1.45)
    rectangle(icon, 0.34, 0.32, 0.40, 0.45, 1.0, 1.45)
    rectangle(icon, 0.60, 0.32, 0.66, 0.45, 1.0, 1.45)
    for i in range(24):
        a, b = math.pi * i / 24, math.pi * (i + 1) / 24
        def p(r, angle):
            return (0.5 + r * math.cos(angle), 0.32 - r * math.sin(angle))
        outline = [p(0.16, a), p(0.16, b), p(0.10, b), p(0.10, a)]
        icon.quad(*[(x, y, 1.45) for x, y in outline], (0, 0, 1))
        icon.quad(*[(x, y, 1.0) for x, y in outline], (0, 0, -1))
        outer_a, outer_b, inner_b, inner_a = outline
        middle = (a + b) / 2
        radial = (math.cos(middle), -math.sin(middle), 0)
        icon.quad((*outer_a, 1.0), (*outer_b, 1.0),
                  (*outer_b, 1.45), (*outer_a, 1.45), radial)
        icon.quad((*inner_a, 1.0), (*inner_b, 1.0),
                  (*inner_b, 1.45), (*inner_a, 1.45),
                  (-radial[0], -radial[1], 0))
    # Pale keyhole contrasts with the dark lock body and takes the link tint.
    circle = [(0.5 + 0.045 * math.cos(i * math.tau / 24),
               0.545 + 0.045 * math.sin(i * math.tau / 24)) for i in range(24)]
    keyhole.fan((0.5, 0.545), circle, 1.48, up=True)
    keyhole.prism_walls(circle, 1.45, 1.48)
    rectangle(keyhole, 0.477, 0.55, 0.523, 0.64, 1.45, 1.48)
    write_glb(OUTPUT_DIRECTORY / 'lock_plate.glb',
              [('Plate', base), ('Icon', icon), ('Plate', keyhole)])


if __name__ == '__main__':
    main()
