#!/usr/bin/env python3
"""Independent expected values for the Lambert + point-light shading tests.

Pure Python in double precision; shares no code with src/shade. It implements the
frozen rules of docs/INTERFACES.md section 6 directly:
  rgb = albedo * ambient + sum_i albedo * max(0, dot(ng, l)) * I_i / d2   (unoccluded lights)
where l = (L - p) / |L - p|, d2 = |L - p|^2 and ng faces the viewer.

The printed literals are pasted into tests/test_shade_independent.cpp. Run:
    python3 tests/oracle/shading_expected.py
"""
import math

ALBEDO_SPHERE = (0.8, 0.1, 0.1)
ALBEDO_FLOOR = (0.7, 0.7, 0.7)
AMBIENT = 0.05


def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): n = math.sqrt(dot(a, a)); return tuple(x / n for x in a)


def sphere_blocks(p, light, center, radius):
    """True if the sphere intersects the open segment p -> light."""
    d = sub(light, p); length = math.sqrt(dot(d, d)); d = tuple(x / length for x in d)
    oc = sub(p, center)
    b = dot(oc, d); c = dot(oc, oc) - radius * radius
    disc = b * b - c
    if disc < 0: return False
    t = -b - math.sqrt(disc)
    return 1e-9 < t < length


def shade(p, n, albedo, lights, spheres=()):
    rgb = [a * AMBIENT for a in albedo]; mask = 0
    for i, (lp, inten) in enumerate(lights):
        to_l = sub(lp, p); d2 = dot(to_l, to_l); l = norm(to_l)
        cs = dot(n, l)
        if cs <= 0 or any(sphere_blocks(p, lp, c, r) for c, r in spheres): continue
        for k in range(3): rgb[k] += albedo[k] * cs * inten[k] / d2
        mask |= 1 << i
    return rgb, mask


def main():
    sphere = ((0.0, 1.0, 0.0), 1.0)
    light0 = ((0.0, 4.0, 0.0), (20.0, 20.0, 20.0))

    # Sphere point 60 degrees off the pole, in the x-y plane.
    a = math.radians(60.0)
    n = (math.sin(a), math.cos(a), 0.0)
    p = tuple(c + r for c, r in zip(sphere[0], n))
    print("sphere60 origin", tuple(c + 6.0 * x for c, x in zip(sphere[0], n)), "dir", tuple(-x for x in n))
    print("sphere60", shade(p, n, ALBEDO_SPHERE, [light0], [sphere]))

    # Tilted triangle A=(0,0,0) B=(4,0,0) C=(0,4,4), plane y = z, normal (0,-1,1)/sqrt2.
    A, B, C = (0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (0.0, 4.0, 4.0)
    tn = norm(cross(sub(B, A), sub(C, A)))
    tp = (1.0, 1.0, 1.0)
    alb = (0.6, 0.3, 0.9)
    print("tilted normal", tn)
    print("tilted", shade(tp, tn, alb, [((2.0, 0.0, 3.0), (10.0, 20.0, 30.0))]))

    # Three lights on the floor point (1, 0, 0), as in MultipleLightsAccumulateInOrderWithMask.
    lights = [light0, ((3.0, 4.0, 3.0), (10.0, 5.0, 2.0)), ((-3.0, -4.0, 0.0), (9.0, 9.0, 9.0))]
    print("multi", shade((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), ALBEDO_FLOOR, lights, [sphere]))


if __name__ == "__main__":
    main()
