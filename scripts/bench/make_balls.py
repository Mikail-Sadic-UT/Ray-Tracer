#!/usr/bin/env python3
"""Writes the `balls` extra benchmark scene: randomly scattered spheres of every material kind
(matte, glossy, mirror, glass) on a floor, lit by several point lights.

    make_balls.py [--out scenes/extra/balls.json] [--count 48] [--lights 4] [--seed 7]

The placement is seeded (Python's Mersenne Twister, whose `random()` sequence is fixed across
Python versions), so the same arguments write the same bytes on every machine. Spheres rest on
the floor and do not overlap. The committed scenes/extra/balls.json comes from the defaults;
`--check` exits 1 if regenerating it would change it.
"""
import argparse
import json
import math
from pathlib import Path
import random
import sys

REPO = Path(__file__).resolve().parent.parent.parent
DEFAULT_OUT = REPO / 'scenes' / 'extra' / 'balls.json'

# Same JSON keys as scenes/known/known_showcase.json.
MATERIALS = [
    {"name": "floor", "albedo": [0.55, 0.55, 0.58], "ks": [0, 0, 0], "shininess": 1, "specular": False},
    {"name": "matte red", "albedo": [0.8, 0.12, 0.1], "ks": [0, 0, 0], "shininess": 1, "specular": False},
    {"name": "matte white", "albedo": [0.85, 0.85, 0.82], "ks": [0, 0, 0], "shininess": 1, "specular": False},
    {"name": "glossy green", "albedo": [0.1, 0.6, 0.15], "ks": [0.5, 0.5, 0.5], "shininess": 24, "specular": True},
    {"name": "shiny blue", "albedo": [0.1, 0.15, 0.7], "ks": [0.9, 0.9, 0.9], "shininess": 180, "specular": True},
    {"name": "glossy yellow", "albedo": [0.8, 0.65, 0.1], "ks": [0.6, 0.6, 0.6], "shininess": 60, "specular": True},
    {"name": "chrome", "albedo": [0.03, 0.03, 0.03], "ks": [0.9, 0.9, 0.9], "shininess": 250, "specular": True, "mirror": True},
    {"name": "gold mirror", "albedo": [0.05, 0.04, 0.0], "ks": [0.9, 0.7, 0.3], "shininess": 1, "specular": False, "mirror": True},
    {"name": "clear glass", "albedo": [0, 0, 0], "ks": [0.97, 0.97, 0.97], "ior": 1.45, "specular": False, "glass": True},
    {"name": "amber glass", "albedo": [0, 0, 0], "ks": [0.9, 0.55, 0.15], "ior": 1.5, "specular": False, "glass": True},
    {"name": "blue glass", "albedo": [0, 0, 0], "ks": [0.4, 0.55, 0.95], "ior": 1.5, "specular": False, "glass": True},
]
HALF = 8.0  # spheres go in [-HALF, HALF] in x and z
FLOOR = 60.0


def r4(x):
    """Rounds to 4 decimals so the file stays readable; the scene is defined by these values."""
    return round(x, 4) + 0.0  # + 0.0 turns -0.0 into 0.0


def make(count, lights, seed):
    rng = random.Random(seed)
    spheres, attempts = [], 0
    while len(spheres) < count:
        attempts += 1
        if attempts > 100000:
            raise SystemExit(f'make_balls: cannot place {count} spheres without overlap')
        radius = r4(0.25 + 0.65 * rng.random() ** 2)  # mostly small, a few large
        x = r4((2 * rng.random() - 1) * (HALF - radius))
        z = r4((2 * rng.random() - 1) * (HALF - radius))
        if any(math.hypot(x - s['center'][0], z - s['center'][2]) < radius + s['radius'] + 0.05
               for s in spheres):
            continue
        material = 1 + rng.randrange(len(MATERIALS) - 1)
        spheres.append({"center": [x, radius, z], "radius": radius, "material": material})
    ring = []
    for i in range(lights):  # a ring of warm and cool point lights, total power fixed
        angle = 2 * math.pi * (i + 0.5) / lights
        warm = i % 2 == 0
        tint = [1.0, 0.92, 0.8] if warm else [0.8, 0.88, 1.0]
        power = 120.0 / lights
        ring.append({"type": "point",
                     "position": [r4(7 * math.cos(angle)), 6.5, r4(7 * math.sin(angle))],
                     "intensity": [r4(power * c) for c in tint]})
    return {
        "version": 2,
        "camera": {"position": [0, 6.5, 15.5], "look_at": [0, 0.4, 0], "up": [0, 1, 0],
                   "vfov_deg": 45, "width": 3840, "height": 2160},
        "background": [0.06, 0.07, 0.1],
        "ambient": 0.04,
        "materials": MATERIALS,
        "lights": ring,
        "geometry": {
            "spheres": spheres,
            "triangles": [
                {"v": [[-FLOOR, 0, -FLOOR], [-FLOOR, 0, FLOOR], [FLOOR, 0, FLOOR]], "material": 0},
                {"v": [[-FLOOR, 0, -FLOOR], [FLOOR, 0, FLOOR], [FLOOR, 0, -FLOOR]], "material": 0},
            ],
        },
    }


def dumps(scene):
    """One sphere per line, like the known scenes."""
    head = {k: v for k, v in scene.items() if k != 'geometry'}
    lines = ['{']
    for k, v in head.items():
        if k in ('materials', 'lights'):
            lines.append(f'  "{k}": [')
            lines += [f'    {json.dumps(x)}{"," if i < len(v) - 1 else ""}'
                      for i, x in enumerate(v)]
            lines.append('  ],')
        else:
            lines.append(f'  "{k}": {json.dumps(v)},')
    lines.append('  "geometry": {')
    for key in ('spheres', 'triangles'):
        items = scene['geometry'][key]
        lines.append(f'    "{key}": [')
        lines += [f'      {json.dumps(x)}{"," if i < len(items) - 1 else ""}'
                  for i, x in enumerate(items)]
        lines.append('    ]' + (',' if key == 'spheres' else ''))
    lines += ['  }', '}']
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--count', type=int, default=48)
    parser.add_argument('--lights', type=int, default=4)
    parser.add_argument('--seed', type=int, default=7)
    parser.add_argument('--check', action='store_true',
                        help='do not write; exit 1 if --out differs from what would be written')
    args = parser.parse_args(argv)
    if not 1 <= args.lights <= 32 or args.count < 1:
        parser.error('--count must be at least 1 and --lights in [1, 32]')
    text = dumps(make(args.count, args.lights, args.seed))
    json.loads(text)  # well-formed
    if args.check:
        same = args.out.is_file() and args.out.read_text() == text
        print(f'make_balls: {args.out} {"is up to date" if same else "differs"}', file=sys.stderr)
        return 0 if same else 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    print(f'make_balls: wrote {args.out} ({args.count} spheres, {args.lights} lights)',
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
