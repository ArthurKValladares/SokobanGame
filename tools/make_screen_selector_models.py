#!/usr/bin/env python3
"""Author the two overworld star meshes and their three progress finishes."""

import math
from pathlib import Path

from make_energy_plate_models import band, material, write_glb
from make_rotator_models import Primitive


OUTPUT = Path(__file__).resolve().parent.parent / "assets/custom/pbr/models"
CENTER = (.5, .5)
UP = (0, 0, 1)


def polar(radius, angle):
    return (.5 + radius * math.cos(angle), .5 + radius * math.sin(angle))


def star_outline(rays, radius, valley, rotation):
    return [
        polar(radius if i % 2 == 0 else radius * valley,
              rotation + math.pi * i / rays)
        for i in range(2 * rays)
    ]


def star_geometry(finale):
    rays, radius = (12, .47) if finale else (8, .35)
    valley = .62 if finale else .54
    rotation = math.pi / 2
    base, trim, light, shade = (Primitive() for _ in range(4))

    outer = star_outline(rays, radius, valley, rotation)
    bevel = star_outline(rays, radius * .94, valley, rotation)
    face = star_outline(rays, radius * (.80 if finale else .83), valley, rotation)
    base.prism_walls(outer, .012, .028)
    base.fan(CENTER, outer, .012, up=False)
    band(base, outer, bevel, .028, .052)
    band(trim, bevel, face, .052, .058)

    hub = [polar(radius * .29, rotation + math.pi * i / rays)
           for i in range(2 * rays)]
    for i, tip in enumerate(face):
        next_i = (i + 1) % len(face)
        facet = light if i % 2 == 0 else shade
        facet.quad((*tip, .058), (*face[next_i], .058),
                   (*hub[next_i], .079), (*hub[i], .079), UP)

    if finale:
        inset = star_outline(rays, radius * .22, .74, rotation)
        band(trim, hub, inset, .079, .105)
        light.fan(CENTER, inset, .105, up=True)
        # Raised inlays between the rays form a second ring of small diamonds.
        for i in range(rays):
            angle = rotation + (2 * i + 1) * math.pi / rays
            diamond = [
                polar(radius * .49, angle),
                polar(radius * .395, angle + .065),
                polar(radius * .33, angle),
                polar(radius * .395, angle - .065),
            ]
            trim.quad(*[(*point, .084) for point in diamond], UP)
    else:
        light.fan(CENTER, hub, .079, up=True)
    return {"Base": base, "Trim": trim, "Light": light, "Shade": shade}


def finishes(status):
    colors = {
        "Playable": ((.08, .64, .055), (.025, .34, .025), .14),
        "Unavailable": ((.24, .24, .24), (.13, .13, .13), 0),
        "Solved": ((.92, .96, 1), (.58, .65, .74), 2.1),
    }
    light, shade, glow = colors[status]
    result = {
        "Base": material("Dark metal", (.105, .12, .14), .68, .47),
        "Trim": material("Beveled silver", (.45, .48, .52), .65, .36),
        "Light": material("Light facet", light, .10, .30, glow),
        "Shade": material("Dark facet", shade, .18, .40, glow * .52),
    }
    if status == "Playable":
        for key in ("Light", "Shade"):
            result[key]["emissiveFactor"] = [.11, 1, .08]
    return result


def main():
    for variant in ("A", "B"):
        geometry = star_geometry(finale=variant == "B")
        for status in ("Playable", "Unavailable", "Solved"):
            write_glb(OUTPUT / f"ScreenSelector{variant}{status}.glb",
                      geometry, finishes(status), "tools/make_screen_selector_models.py")


if __name__ == "__main__":
    main()
