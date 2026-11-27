#!/usr/bin/env python3
"""Writes the `showcase` scene: six hand-placed spheres close together on a checkerboard floor, lit by
three point lights: two mirrors (chrome, gold), two glass balls (clear, emerald), a matte red one and
a glossy blue one, so one picture shows every material. A picture of
what the renderer does (mirror and glass bounces, tinted shadows, highlights), not a benchmark.

    make_showcase.py [--out scenes/extra/showcase.json] [--check]

No random numbers: the same arguments write the same bytes. The committed
scenes/extra/showcase.json comes from the defaults; `--check` exits 1 if regenerating it would
change it. Same JSON keys as scenes/known/known_showcase.json.
"""
import argparse
import json
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parent.parent.parent
DEFAULT_OUT = REPO / 'scenes' / 'extra' / 'showcase.json'

MATERIALS = [
    {"name": "floor light", "albedo": [0.82, 0.82, 0.8], "ks": [0, 0, 0], "shininess": 1, "specular": False},
    {"name": "floor dark", "albedo": [0.1, 0.1, 0.13], "ks": [0, 0, 0], "shininess": 1, "specular": False},
    {"name": "chrome", "albedo": [0.03, 0.03, 0.03], "ks": [0.9, 0.9, 0.9], "shininess": 250, "specular": True, "mirror": True},
    {"name": "gold mirror", "albedo": [0.05, 0.04, 0.0], "ks": [0.9, 0.7, 0.3], "shininess": 1, "specular": False, "mirror": True},
    {"name": "clear glass", "albedo": [0, 0, 0], "ks": [0.97, 0.97, 0.97], "ior": 1.45, "specular": False, "glass": True},
    {"name": "emerald glass", "albedo": [0, 0, 0], "ks": [0.45, 0.85, 0.5], "ior": 1.5, "specular": False, "glass": True},
    {"name": "matte red", "albedo": [0.8, 0.12, 0.1], "ks": [0, 0, 0], "shininess": 1, "specular": False},
    {"name": "glossy blue", "albedo": [0.1, 0.15, 0.7], "ks": [0.7, 0.7, 0.7], "shininess": 90, "specular": True},
]
# (x, z, radius, material); each sphere rests on the floor and no two touch.
SPHERES = [
    (0.0, -0.5, 1.0, 2),    # chrome, the centerpiece
    (2.3, 0.3, 0.8, 3),     # gold mirror
    (-2.3, 0.7, 0.8, 4),    # clear glass, bends the checkerboard
    (-1.05, 2.0, 0.55, 5),  # emerald glass, in front of the chrome and the clear glass
    (0.5, 2.3, 0.5, 6),     # matte red, in front of the chrome
    (1.85, 3.6, 0.5, 7),    # glossy blue, well in front of the gold so it hides nothing
]
TILES = 40      # 40 x 40 squares
TILE = 1.0
LIGHTS = [
    {"type": "point", "position": [-5.0, 7.0, 5.0], "intensity": [95.0, 84.0, 66.0]},   # warm key
    {"type": "point", "position": [6.0, 5.0, 3.0], "intensity": [34.0, 44.0, 62.0]},    # cool fill
    {"type": "point", "position": [0.0, 6.0, -6.0], "intensity": [40.0, 40.0, 46.0]},   # rim
]


def make():
    half = TILES * TILE / 2
    triangles = []
    for i in range(TILES):
        for j in range(TILES):
            x0, z0 = -half + i * TILE, -half + j * TILE
            x1, z1 = x0 + TILE, z0 + TILE
            m = (i + j) % 2
            # Counterclockwise seen from above (+y): normal up, like the benchmark terrain.
            triangles.append({"v": [[x0, 0, z0], [x0, 0, z1], [x1, 0, z1]], "material": m})
            triangles.append({"v": [[x0, 0, z0], [x1, 0, z1], [x1, 0, z0]], "material": m})
    return {
        "version": 2,
        "camera": {"position": [0.5, 2.6, 8.5], "look_at": [0.0, 0.7, 0.5], "up": [0, 1, 0],
                   "vfov_deg": 40, "width": 3840, "height": 2160},
        "background": [0.05, 0.06, 0.09],
        "ambient": 0.05,
        "materials": MATERIALS,
        "lights": LIGHTS,
        "geometry": {
            "spheres": [{"center": [x, r, z], "radius": r, "material": m} for x, z, r, m in SPHERES],
            "triangles": triangles,
        },
    }


def dumps(scene):
    """One sphere and one triangle per line, like the known scenes."""
    lines = ['{']
    for k, v in scene.items():
        if k == 'geometry':
            continue
        if k in ('materials', 'lights'):
            lines.append(f'  "{k}": [')
            lines += [f'    {json.dumps(x)}{"," if i < len(v) - 1 else ""}' for i, x in enumerate(v)]
            lines.append('  ],')
        else:
            lines.append(f'  "{k}": {json.dumps(v)},')
    lines.append('  "geometry": {')
    for key in ('spheres', 'triangles'):
        items = scene['geometry'][key]
        lines.append(f'    "{key}": [')
        lines += [f'      {json.dumps(x)}{"," if i < len(items) - 1 else ""}' for i, x in enumerate(items)]
        lines.append('    ]' + (',' if key == 'spheres' else ''))
    lines += ['  }', '}']
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--check', action='store_true',
                        help='do not write; exit 1 if --out differs from what would be written')
    args = parser.parse_args(argv)
    text = dumps(make())
    json.loads(text)  # well-formed
    if args.check:
        same = args.out.is_file() and args.out.read_text() == text
        print(f'make_showcase: {args.out} {"is up to date" if same else "differs"}', file=sys.stderr)
        return 0 if same else 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    print(f'make_showcase: wrote {args.out} ({len(SPHERES)} spheres, {len(LIGHTS)} lights)',
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
