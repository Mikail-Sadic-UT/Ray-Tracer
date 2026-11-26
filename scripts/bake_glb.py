#!/usr/bin/env python3
"""Bakes a glb model's base-color textures into flat per-piece colors and builds a scene.

    python3 scripts/bake_glb.py MODEL.glb NAME [options]    ->  scenes/meshes/NAME.json

The renderer has no textures. Each material's triangles are split into 4^L pieces (L per
material: as many levels as the piece budget, the texture resolution and --max-level allow),
every piece gets the average of the base-color texture around its UVs (times the material's
base color factor), quantized to 5 bits per channel, and each palette entry becomes an
ordinary flat material. Materials without a texture keep their base color factor. Pieces whose
texture alpha is under one half are dropped for BLEND and MASK materials (leaves, grilles).
The renderer is not touched, so CPU and GPU stay byte-identical.

Options: --budget N (pieces per material, default 250000), --max-level L (default 6),
--camera X Y Z, --look X Y Z, --fov DEG, --light "X Y Z R G B" (repeat), --ambient A,
--background R G B, --rotate-y DEG, --size S (default: the model's largest extent, so 1:1),
--drop-instance K (leave out the K-th mesh instance of the scene graph; repeat),
--out-dir DIR (default scenes/meshes), --obj2bin PATH (default build/apps/obj2bin/obj2bin),
--template SCENE.json (default scenes/templates/mesh_stage.json: the materials, lights, camera
and background the baked materials are appended to).
Needs the built obj2bin, and Pillow and numpy (`sudo apt install python3-pil python3-numpy`).
"""
import argparse
import collections
import importlib.util
import io
import json
import math
import subprocess
from pathlib import Path

import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as error:           # an optional tool: the rest of the repo needs neither
    sys.exit(f'bake_glb: {error}; install them with: sudo apt install python3-pil python3-numpy')

Image.MAX_IMAGE_PIXELS = None
ROOT = Path(__file__).resolve().parents[1]

spec = importlib.util.spec_from_file_location('glb2obj', ROOT / 'scripts' / 'glb2obj.py')
glb2obj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(glb2obj)

DTYPE = {5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
KEYCHARS = '0123456789abcdefghijklmnopqrstuv'


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(np.asarray(c, dtype=np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


LUT = srgb_to_linear(np.arange(256) / 255.0)


def accessor(doc, binary, index):
    acc = doc['accessors'][index]
    if 'bufferView' not in acc or 'sparse' in acc:
        raise SystemExit('bake_glb: sparse or empty accessors are not supported')
    view = doc['bufferViews'][acc['bufferView']]
    dtype = np.dtype(DTYPE[acc['componentType']])
    width = WIDTH[acc['type']]
    base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = view.get('byteStride') or dtype.itemsize * width
    if stride == dtype.itemsize * width:
        return np.frombuffer(binary, dtype=dtype, count=acc['count'] * width,
                             offset=base).reshape(acc['count'], width)
    return np.ndarray(shape=(acc['count'], width), dtype=dtype, buffer=binary, offset=base,
                      strides=(stride, dtype.itemsize))


def subdivide(P, UV, T):
    """One 1-to-4 split of every triangle; edge midpoints are shared, winding is kept."""
    V, N = len(P), len(T)
    edges = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]])
    edges.sort(axis=1)
    uniq, inverse = np.unique(edges[:, 0] * V + edges[:, 1], return_inverse=True)
    a, b = uniq // V, uniq % V
    Pn = np.concatenate([P, 0.5 * (P[a] + P[b])])
    UVn = np.concatenate([UV, 0.5 * (UV[a] + UV[b])])
    m01, m12, m20 = V + inverse[:N], V + inverse[N:2 * N], V + inverse[2 * N:]
    v0, v1, v2 = T[:, 0], T[:, 1], T[:, 2]
    Tn = np.concatenate([np.stack([v0, m01, m20], 1), np.stack([v1, m12, m01], 1),
                         np.stack([v2, m20, m12], 1), np.stack([m01, m12, m20], 1)])
    return Pn, UVn, Tn


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument('model')
    ap.add_argument('name')
    ap.add_argument('--budget', type=int, default=250000)
    ap.add_argument('--max-level', type=int, default=6)
    ap.add_argument('--camera', type=float, nargs=3)
    ap.add_argument('--look', type=float, nargs=3)
    ap.add_argument('--fov', type=float, default=60.0)
    ap.add_argument('--light', type=float, nargs=6, action='append', metavar='V')
    ap.add_argument('--ambient', type=float, default=0.10)
    ap.add_argument('--background', type=float, nargs=3, default=[0.02, 0.02, 0.03])
    ap.add_argument('--rotate-y', type=float, default=0.0)
    ap.add_argument('--drop-instance', type=int, action='append', default=[],
                    help='leave out mesh instance K (its order in the glb scene graph; repeat)')
    ap.add_argument('--size', type=float)
    ap.add_argument('--out-dir', type=Path, default=ROOT / 'scenes' / 'meshes')
    ap.add_argument('--obj2bin', type=Path, default=ROOT / 'build' / 'apps' / 'obj2bin' / 'obj2bin')
    ap.add_argument('--template', type=Path, default=ROOT / 'scenes' / 'templates' / 'mesh_stage.json')
    ap.add_argument('--width', type=int, default=1280)
    ap.add_argument('--height', type=int, default=720)
    args = ap.parse_args()

    doc, binary = glb2obj.read_glb(args.model)
    materials = doc.get('materials', [])
    images = {}

    def image(index):
        if index not in images:
            view = doc['bufferViews'][doc['images'][index]['bufferView']]
            raw = binary[view.get('byteOffset', 0):view.get('byteOffset', 0) + view['byteLength']]
            images[index] = np.asarray(Image.open(io.BytesIO(raw)).convert('RGBA'))
        return images[index]

    # ---- gather the world-space triangles of every material ----
    per_material = collections.OrderedDict()
    for instance, (mesh_index, M) in enumerate(glb2obj.instances(doc)):
        if instance in args.drop_instance:
            continue
        M = np.array(M, dtype=np.float64)
        for prim in doc['meshes'][mesh_index]['primitives']:
            if prim.get('mode', 4) != 4:
                continue
            P = accessor(doc, binary, prim['attributes']['POSITION']).astype(np.float64)
            P = P @ M[:, :3].T + M[:, 3]
            uv = prim['attributes'].get('TEXCOORD_0')
            UV = (accessor(doc, binary, uv).astype(np.float64) if uv is not None
                  else np.zeros((len(P), 2)))
            if 'indices' in prim:
                T = accessor(doc, binary, prim['indices']).reshape(-1, 3).astype(np.int64)
            else:
                T = np.arange(len(P), dtype=np.int64).reshape(-1, 3)
            entry = per_material.setdefault(prim.get('material', -1), {'P': [], 'UV': [], 'T': [], 'n': 0})
            entry['P'].append(P)
            entry['UV'].append(UV)
            entry['T'].append(T + entry['n'])
            entry['n'] += len(P)
    all_P = np.concatenate([np.concatenate(e['P']) for e in per_material.values()])
    lo, hi = all_P.min(axis=0), all_P.max(axis=0)
    print(f'{sum(sum(len(t) for t in e["T"]) for e in per_material.values())} triangles in '
          f'{len(per_material)} materials; extent {np.round(hi - lo, 2)}')

    # ---- bake each material ----
    vertex_blocks, group_faces, palette = [], {}, {}   # palette: group -> (albedo, ks, shininess)
    voffset = 0
    for mat_index, entry in per_material.items():
        P, UV, T = np.concatenate(entry['P']), np.concatenate(entry['UV']), np.concatenate(entry['T'])
        info = materials[mat_index] if mat_index >= 0 else {}
        pbr = info.get('pbrMetallicRoughness', {})
        factor = np.array(pbr.get('baseColorFactor', [1, 1, 1, 1]), dtype=np.float64)
        texinfo = pbr.get('baseColorTexture')
        tex = None
        if texinfo is not None and texinfo.get('texCoord', 0) == 0:
            source = doc['textures'][texinfo['index']].get('source')
            if source is not None:
                tex = image(source)
        metallic = pbr.get('metallicFactor', 0.0)
        roughness = max(pbr.get('roughnessFactor', 0.6), 0.05)
        ks = round(0.08 + 0.3 * float(metallic), 3)
        shininess = float(np.clip(2.0 / roughness ** 2 - 2.0, 8.0, 120.0))
        label = str(info.get('name', f'mat{mat_index}')).replace(' ', '_')
        cutout = info.get('alphaMode') in ('BLEND', 'MASK')
        if tex is None:
            name = f'{label}_flat'
            group_faces.setdefault(name, []).append(T + voffset)
            palette[name] = (factor[:3], ks, shininess)
            vertex_blocks.append(P)
            voffset += len(P)
            print(f'  {label:22} {len(T):8} triangles, flat color')
            continue
        h, w = tex.shape[:2]
        # levels: the piece budget, the texture (pieces stay at least ~2 texels), --max-level
        uv_edge = np.linalg.norm(UV[T[:, 0]] - UV[T[:, 1]], axis=1)
        texel_edge = float(np.median(uv_edge)) * max(w, h)
        by_texture = int(math.floor(math.log2(max(texel_edge / 2.0, 1.0))))
        by_budget = int(math.floor(math.log(max(args.budget / len(T), 1.0), 4)))
        level = max(0, min(by_texture, by_budget, args.max_level))
        for _ in range(level):
            P, UV, T = subdivide(P, UV, T)
        uv = UV[T]                                   # (n, 3, 2)
        centroid = uv.mean(axis=1)
        acc, alpha = np.zeros((len(T), 3)), np.zeros(len(T))
        samples = [centroid] + [0.5 * (centroid + uv[:, k]) for k in range(3)]
        for q in samples:
            x = np.clip(np.floor((q[:, 0] % 1.0) * w).astype(np.int64), 0, w - 1)
            y = np.clip(np.floor((q[:, 1] % 1.0) * h).astype(np.int64), 0, h - 1)   # glTF v points down
            texel = tex[y, x]
            acc += LUT[texel[:, :3]]
            alpha += texel[:, 3] / 255.0
        lin = acc / len(samples) * factor[:3]
        keep = np.ones(len(T), dtype=bool)
        if cutout:
            keep = (alpha / len(samples) * factor[3]) >= 0.5
        q5 = np.rint(linear_to_srgb(lin) * 31).astype(np.int64)
        key = (q5[:, 0] << 10) | (q5[:, 1] << 5) | q5[:, 2]
        key[~keep] = -1
        for k in np.unique(key):
            if k < 0:
                continue
            name = f'{label}_{KEYCHARS[(k >> 10) & 31]}{KEYCHARS[(k >> 5) & 31]}{KEYCHARS[k & 31]}'
            group_faces.setdefault(name, []).append(T[key == k] + voffset)
            rgb = srgb_to_linear(np.array([(k >> 10) & 31, (k >> 5) & 31, k & 31]) / 31.0)
            palette[name] = (rgb, ks, shininess)
        vertex_blocks.append(P)
        voffset += len(P)
        print(f'  {label:22} {len(T) // 4 ** level:8} triangles x {4 ** level:5} = {len(T):8} pieces '
              f'(level {level}; {int((~keep).sum())} cut), texture {w}x{h}')
    print(f'{len(palette)} palette colors')

    # ---- OBJ ----
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    obj_path = out_dir / f'{args.name}.obj'
    with open(obj_path, 'w') as out:
        out.write('# baked by bake_glb.py\n')
        for block in vertex_blocks:
            out.write('\n'.join('v %.5f %.5f %.5f' % tuple(r) for r in block.tolist()))
            out.write('\n')
        for name, parts in group_faces.items():
            out.write(f'o {name}\n')
            T = np.concatenate(parts) + 1
            out.write('\n'.join('f %d %d %d' % tuple(r) for r in T.tolist()))
            out.write('\n')

    # ---- stage, materials, obj2bin ----
    stage = json.loads(args.template.read_text())
    stage['geometry'] = {'spheres': [], 'triangles': []}
    stage['background'] = args.background
    stage['ambient'] = args.ambient
    center = 0.5 * (lo + hi)
    extent = float((hi - lo).max())
    height = float(hi[1] - lo[1])          # obj2bin puts the box's bottom centre at the origin
    camera = args.camera or [0.0, 0.5 * height + 0.35 * extent, 0.5 * float(hi[2] - lo[2]) + 1.1 * extent]
    look = args.look or [0.0, 0.4 * height, 0.0]
    stage['camera'].update(position=list(camera), look_at=list(look), vfov_deg=args.fov,
                           width=args.width, height=args.height)
    lights = args.light or [[0, 0.9 * (hi[1] - lo[1]), 0, 8, 8, 7]]
    stage['lights'] = [{'type': 'point', 'position': v[:3], 'intensity': v[3:]} for v in lights]
    ids = {}
    for name, (albedo, ks, shin) in palette.items():
        ids[name] = len(stage['materials'])
        stage['materials'].append({'name': name, 'albedo': [round(float(x), 5) for x in albedo],
                                   'ks': [ks] * 3, 'shininess': round(shin, 2), 'specular': ks > 0.0})
    stage_path = out_dir / f'{args.name}_stage.json'
    stage_path.write_text(json.dumps(stage))
    entries = [f'{n}={i}' for n, i in ids.items()]
    cmd = [str(args.obj2bin), '--obj', str(obj_path), '--template', str(stage_path),
           '--out', str(out_dir / f'{args.name}.json'), '--material', '0',
           '--size', str(args.size or extent), '--rotate-y', str(args.rotate_y)]
    for k in range(0, len(entries), 2500):          # keep each argument under the OS limit
        cmd += ['--group-material', ','.join(entries[k:k + 2500])]
    subprocess.run(cmd, check=True)


if __name__ == '__main__':
    main()
