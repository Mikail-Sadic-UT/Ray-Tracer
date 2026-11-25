"""scripts/glb2obj.py: node transforms are baked in and connected pieces become OBJ groups."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(sys.argv.pop(1)).resolve()


def make_glb(path, nodes, positions, indices):
    """One mesh with one triangle-list primitive; `nodes` is the glTF node list."""
    binary = b''.join(struct.pack('<3f', *p) for p in positions)
    binary += b''.join(struct.pack('<H', i) for i in indices)
    binary += b'\0' * (-len(binary) % 4)
    doc = {
        'asset': {'version': '2.0'},
        'scenes': [{'nodes': [0]}],
        'nodes': nodes,
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': len(positions), 'type': 'VEC3'},
            {'bufferView': 1, 'componentType': 5123, 'count': len(indices), 'type': 'SCALAR'}],
        'bufferViews': [
            {'buffer': 0, 'byteOffset': 0, 'byteLength': 12 * len(positions)},
            {'buffer': 0, 'byteOffset': 12 * len(positions), 'byteLength': 2 * len(indices)}],
        'buffers': [{'byteLength': len(binary)}],
    }
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary),
                                                                        b'BIN\0') + binary
    path.write_bytes(struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body)


def convert(glb, obj):
    return subprocess.run([sys.executable, '-I', str(SCRIPT), str(glb), str(obj)],
                          capture_output=True, text=True)


def read_obj(path):
    vertices, groups = [], {}
    current = None
    for line in path.read_text().splitlines():
        parts = line.split()
        if not parts or parts[0] == '#':
            continue
        if parts[0] == 'v':
            vertices.append(tuple(float(x) for x in parts[1:4]))
        elif parts[0] == 'o':
            current = groups.setdefault(parts[1], [])
        elif parts[0] == 'f':
            current.append(tuple(int(x) for x in parts[1:4]))
    return vertices, groups


class Glb2ObjTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def test_bakes_transform_and_splits_connected_pieces(self):
        # Two separate triangles; the node scales by 2, turns -90 degrees about x, moves in y.
        s = 2 ** -0.5
        nodes = [{'scale': [2, 2, 2], 'rotation': [-s, 0, 0, s], 'translation': [0, 1, 0],
                  'mesh': 0}]
        positions = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (5, 0, 0), (6, 0, 0), (5, 1, 0)]
        make_glb(self.dir / 'm.glb', nodes, positions, [0, 1, 2, 3, 4, 5])
        result = convert(self.dir / 'm.glb', self.dir / 'm.obj')
        self.assertEqual(result.returncode, 0, result.stderr)
        vertices, groups = read_obj(self.dir / 'm.obj')
        self.assertEqual(sorted(groups), ['part0', 'part1'])
        self.assertEqual(groups['part0'], [(1, 2, 3)])
        self.assertEqual(groups['part1'], [(4, 5, 6)])
        # (x, y, z) -> scale 2 -> rotate -90 about x: (x, z, -y) -> translate: (2x, 2z + 1, -2y).
        expected = [(0, 1, 0), (2, 1, 0), (0, 1, -2)]
        for got, want in zip(vertices[:3], expected):
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, places=5)

    def test_triangles_sharing_a_vertex_are_one_piece(self):
        make_glb(self.dir / 'm.glb', [{'mesh': 0}], [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)],
                 [0, 1, 2, 1, 3, 2])
        self.assertEqual(convert(self.dir / 'm.glb', self.dir / 'm.obj').returncode, 0)
        _, groups = read_obj(self.dir / 'm.obj')
        self.assertEqual(list(groups), ['part0'])
        self.assertEqual(len(groups['part0']), 2)

    def test_rejects_files_that_are_not_glb(self):
        (self.dir / 'bad.glb').write_bytes(b'not a model at all, just text')
        result = convert(self.dir / 'bad.glb', self.dir / 'bad.obj')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not a binary glTF', result.stderr)


if __name__ == '__main__':
    unittest.main()
