#!/usr/bin/env python3
"""Converts a binary glTF (.glb) to a Wavefront OBJ that obj2bin can read.

    glb2obj.py MODEL.glb OUT.obj

Only geometry is kept: positions and triangle indices of every triangle-list primitive, with
the node transforms (translation, rotation, scale, or a 4x4 matrix) baked in. Normals, texture
coordinates, colors and materials are dropped, because the renderer shades flat and has no
textures. Skinned and animated models are not handled (the bind pose is written as stored).

Each connected piece of surface (triangles that share a position) becomes its own OBJ object,
`part0`, `part1`, ..., ordered by the first triangle they contain, so obj2bin can give the
pieces different materials with --group-material. The piece list and sizes are printed.

Uses only the standard library. Run it as `python3 -I scripts/glb2obj.py ...` on files you
did not write.
"""
import json
import math
import struct
import sys

COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4),
             5126: ('f', 4)}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def fail(message):
    sys.exit(f'glb2obj: {message}')


def read_glb(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 20 or data[:4] != b'glTF':
        fail(f'{path} is not a binary glTF (.glb) file')
    version, length = struct.unpack_from('<II', data, 4)
    if version != 2:
        fail(f'glTF version {version} is not supported (need 2)')
    if length > len(data):
        fail('file is shorter than its header says')
    doc, binary, offset = None, None, 12
    while offset + 8 <= length:
        size, kind = struct.unpack_from('<I4s', data, offset)
        body = data[offset + 8:offset + 8 + size]
        if len(body) != size:
            fail('a chunk runs past the end of the file')
        if kind == b'JSON':
            doc = json.loads(body)
        elif kind == b'BIN\x00' and binary is None:
            binary = body
        offset += 8 + size
    if doc is None or binary is None:
        fail('the file needs a JSON chunk and a BIN chunk')
    return doc, binary


def accessor(doc, binary, index):
    """Returns the elements of an accessor as a list of tuples."""
    acc = doc['accessors'][index]
    if 'bufferView' not in acc or 'sparse' in acc:
        fail('sparse or empty accessors are not supported')
    view = doc['bufferViews'][acc['bufferView']]
    fmt, size = COMPONENT[acc['componentType']]
    width = WIDTH[acc['type']]
    stride = view.get('byteStride') or size * width
    base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    out = []
    for i in range(acc['count']):
        out.append(struct.unpack_from('<' + fmt * width, binary, base + i * stride))
    return out


def quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def node_matrix(node):
    """The node's local transform as a 3x4 row-major matrix (rotation/scale | translation)."""
    if 'matrix' in node:
        m = node['matrix']  # column-major 4x4
        return [[m[0], m[4], m[8], m[12]], [m[1], m[5], m[9], m[13]], [m[2], m[6], m[10], m[14]]]
    r = quat_matrix(node.get('rotation', [0, 0, 0, 1]))
    s = node.get('scale', [1, 1, 1])
    t = node.get('translation', [0, 0, 0])
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)]


def compose(a, b):
    """a * b for 3x4 affine matrices."""
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0)
             for j in range(4)] for i in range(3)]


def instances(doc):
    """Yields (mesh index, world matrix) for every mesh instance in the default scene."""
    nodes = doc.get('nodes', [])
    identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]
    roots = doc['scenes'][doc.get('scene', 0)]['nodes'] if doc.get('scenes') else range(len(nodes))
    stack = [(n, identity) for n in roots]
    while stack:
        index, parent = stack.pop()
        world = compose(parent, node_matrix(nodes[index]))
        if 'mesh' in nodes[index]:
            yield nodes[index]['mesh'], world
        stack.extend((c, world) for c in nodes[index].get('children', []))


def main():
    if len(sys.argv) != 3:
        fail('usage: glb2obj.py MODEL.glb OUT.obj')
    doc, binary = read_glb(sys.argv[1])
    vertices, faces = [], []
    for mesh_index, m in instances(doc):
        for prim in doc['meshes'][mesh_index]['primitives']:
            if prim.get('mode', 4) != 4:
                fail('only triangle-list primitives are supported')
            positions = accessor(doc, binary, prim['attributes']['POSITION'])
            if 'indices' in prim:
                indices = [i[0] for i in accessor(doc, binary, prim['indices'])]
            else:
                indices = list(range(len(positions)))
            if len(indices) % 3:
                fail('an index list is not a multiple of 3')
            base = len(vertices)
            for p in positions:
                vertices.append(tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3]
                                      for i in range(3)))
            faces += [(base + indices[k], base + indices[k + 1], base + indices[k + 2])
                      for k in range(0, len(indices), 3)]
    if not faces:
        fail('the model has no triangles')

    # Connected pieces: triangles sharing a position (seams duplicate vertices for UVs).
    weld, rep = {}, []
    for v in vertices:
        rep.append(weld.setdefault(tuple(round(c, 5) for c in v), len(weld)))
    parent = list(range(len(weld)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b, c in faces:
        parent[find(rep[a])] = find(rep[b])
        parent[find(rep[b])] = find(rep[c])
    order, parts = {}, []
    for f in faces:
        root = find(rep[f[0]])
        if root not in order:
            order[root] = len(order)
            parts.append([])
        parts[order[root]].append(f)

    with open(sys.argv[2], 'w') as out:
        out.write(f'# from {sys.argv[1]} by glb2obj.py: {len(faces)} triangles, '
                  f'{len(parts)} parts\n')
        for v in vertices:
            out.write('v %.9g %.9g %.9g\n' % v)
        for i, part in enumerate(parts):
            out.write(f'o part{i}\n')
            for a, b, c in part:
                out.write(f'f {a + 1} {b + 1} {c + 1}\n')
    for i, part in enumerate(parts):
        pts = [vertices[v] for f in part for v in f]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        print(f'part{i}: {len(part)} triangles, box '
              f'[{lo[0]:.3f} {lo[1]:.3f} {lo[2]:.3f}] to [{hi[0]:.3f} {hi[1]:.3f} {hi[2]:.3f}]')
    print(f'wrote {sys.argv[2]}: {len(vertices)} vertices, {len(faces)} triangles')


if __name__ == '__main__':
    main()
