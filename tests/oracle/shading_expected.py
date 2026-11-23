#!/usr/bin/env python3
"""Independent expected values for the Lambert + point-light shading tests, and for the E2
extension (Blinn-Phong highlights and directional lights).

Pure Python in double precision; shares no code with src/shade. It implements the
frozen rules of docs/INTERFACES.md section 6 directly:
  rgb = albedo * ambient + sum_i albedo * max(0, dot(ng, l)) * I_i / d2   (unoccluded lights)
where l = (L - p) / |L - p|, d2 = |L - p|^2 and ng faces the viewer. E2 adds, for specular
materials, ks * max(0, dot(ng, h))^shininess * I_i / d2 with h = normalize(l + wo); a
directional light uses l = normalize(-dir), d2 = 1 and an unbounded shadow ray. A mirror
(MAT_MIRROR) adds ks times the shading seen along the reflected ray d - 2 * dot(d, n) * n.
Glass (MAT_GLASS) splits into a Snell refraction and a reflection weighted by Schlick's Fresnel
term, absorbs ks^distance inside, and stops after 4 bounces; glass_trace below is a separate
recursive implementation of those rules for a single sphere.

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


def sphere_blocks(p, light, center, radius, direction=None):
    """True if the sphere intersects the open segment p -> light, or the ray p + t * direction."""
    if direction is None:
        d = sub(light, p); length = math.sqrt(dot(d, d)); d = tuple(x / length for x in d)
    else:
        d, length = direction, math.inf
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


def shade_e2(p, n, wo, albedo, ks, shininess, lights, spheres=()):
    """E2 shading. lights: ('point', position, I) or ('directional', travel direction, I)."""
    rgb = [a * AMBIENT for a in albedo]; mask = 0
    for i, (kind, v, inten) in enumerate(lights):
        if kind == 'point':
            to_l = sub(v, p); d2 = dot(to_l, to_l); l = norm(to_l)
            blocked = any(sphere_blocks(p, v, c, r) for c, r in spheres)
        else:
            l = norm(tuple(-x for x in v)); d2 = 1.0
            blocked = any(sphere_blocks(p, None, c, r, l) for c, r in spheres)
        cs = dot(n, l)
        if cs <= 0 or blocked: continue
        h = norm(tuple(a + b for a, b in zip(l, wo)))
        spec = max(0.0, dot(n, h)) ** shininess
        for k in range(3): rgb[k] += (albedo[k] * cs + ks[k] * spec) * inten[k] / d2
        mask |= 1 << i
    return rgb, mask


def ray_sphere(o, d, center, radius):
    """Nearest t > 0 where the unit-direction ray o + t * d meets the sphere, or None."""
    oc = sub(o, center)
    b = dot(oc, d); c = dot(oc, oc) - radius * radius
    disc = b * b - c
    if disc < 0: return None
    for t in (-b - math.sqrt(disc), -b + math.sqrt(disc)):
        if t > 1e-9: return t
    return None


def glass_trace(o, d, weight, depth, inside, center, radius, ior, absorb, background):
    """Recursive Whitted trace through one glass sphere (albedo 0, nothing else in the scene).

    Same rules as src/shade, written independently: Snell refraction, Schlick reflectance with
    the outside cosine, Beer-Lambert absorb^t inside, at most 4 rays after the primary hit.
    """
    t = ray_sphere(o, d, center, radius)
    if t is None:
        return [w * b for w, b in zip(weight, background)]
    if inside:
        weight = [w * a ** t for w, a in zip(weight, absorb)]
    p = tuple(oi + t * di for oi, di in zip(o, d))
    return glass_split(p, d, weight, depth, inside, center, radius, ior, absorb, background)


def glass_split(p, d, weight, depth, inside, center, radius, ior, absorb, background):
    """The reflected and refracted rays leaving glass surface point p (albedo 0 adds nothing)."""
    total = [0.0, 0.0, 0.0]
    if depth >= 4:
        return total
    n = norm(sub(p, center))
    front = dot(n, d) < 0
    ng = n if front else tuple(-x for x in n)
    eta = 1.0 / ior if front else ior
    cos_i = -dot(d, ng)
    k = 1.0 - eta * eta * (1.0 - cos_i * cos_i)
    refl_w = 1.0
    if k > 0:
        cos_t = math.sqrt(k)
        c = cos_i if front else cos_t
        r0 = ((1.0 - ior) / (1.0 + ior)) ** 2
        refl_w = r0 + (1.0 - r0) * (1.0 - c) ** 5
        tdir = norm(tuple(eta * di + (eta * cos_i - cos_t) * ni for di, ni in zip(d, ng)))
        part = glass_trace(p, tdir, [w * (1.0 - refl_w) for w in weight], depth + 1, front,
                           center, radius, ior, absorb, background)
        total = [a + b for a, b in zip(total, part)]
    rdir = norm(tuple(di - 2.0 * dot(d, ng) * ni for di, ni in zip(d, ng)))
    part = glass_trace(p, rdir, [w * refl_w for w in weight], depth + 1, inside,
                       center, radius, ior, absorb, background)
    return [a + b for a, b in zip(total, part)]


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

    # E2: specular floor point (1, 0, 0) seen from (1, 5, 10). A point light, a directional
    # light (direction given unnormalized), and a directional light the sphere blocks.
    p = (1.0, 0.0, 0.0)
    wo = norm(sub((1.0, 5.0, 10.0), p))
    e2_lights = [('point', (3.0, 4.0, 3.0), (10.0, 5.0, 2.0)),
                 ('directional', (-1.0, -2.0, -0.5), (0.3, 0.6, 0.9)),
                 ('directional', (1.0, -1.0, 0.0), (5.0, 5.0, 5.0))]
    print("e2", shade_e2(p, (0.0, 1.0, 0.0), wo, ALBEDO_FLOOR, (0.5, 0.4, 0.3), 20.5,
                         e2_lights, [sphere]))

    # Mirror floor point (2, 0, 0) seen from (4, 1.5, 0). The reflected ray hits the sphere,
    # which is Lambert, so the path ends there.
    floor_albedo, floor_ks = (0.1, 0.1, 0.1), (0.8, 0.7, 0.6)
    mirror_light = ((3.0, 4.0, 2.0), (20.0, 20.0, 20.0))
    p = (2.0, 0.0, 0.0); n = (0.0, 1.0, 0.0)
    d = norm(sub(p, (4.0, 1.5, 0.0)))
    floor_rgb, floor_mask = shade(p, n, floor_albedo, [mirror_light], [sphere])
    r = norm(tuple(di - 2.0 * dot(d, n) * ni for di, ni in zip(d, n)))
    # The reflected ray starts at the offset origin (INTERFACES section 6, offset_ray_origin):
    # within 1/32 of zero a coordinate moves by 2^-16 along the normal, which here shifts the
    # sphere hit enough to matter at 1e-5. x = 2 is far from zero, and the integer-ulp offset
    # there moves along the normal's zero x and z components, so only y changes.
    start = (2.0, 2.0 ** -16, 0.0)
    t = ray_sphere(start, r, *sphere)
    q = tuple(pi + t * ri for pi, ri in zip(start, r))
    sphere_rgb, _ = shade(q, norm(sub(q, sphere[0])), ALBEDO_SPHERE, [mirror_light], [sphere])
    print("mirror", [f + k * c for f, k, c in zip(floor_rgb, floor_ks, sphere_rgb)], floor_mask)

    # Glass sphere (ior 1.5, absorb (0.5, 0.8, 0.9) per unit) hit off center by a ray from
    # (0.3, 0.2, 5) along -z, against background (0.2, 0.4, 0.6); albedo 0 and no lights.
    o, d = (0.3, 0.2, 5.0), (0.0, 0.0, -1.0)
    t = ray_sphere(o, d, (0.0, 0.0, 0.0), 1.0)
    p = tuple(oi + t * di for oi, di in zip(o, d))
    print("glass", glass_split(p, d, [1.0, 1.0, 1.0], 0, False, (0.0, 0.0, 0.0), 1.0, 1.5,
                               (0.5, 0.8, 0.9), (0.2, 0.4, 0.6)))


if __name__ == "__main__":
    main()
