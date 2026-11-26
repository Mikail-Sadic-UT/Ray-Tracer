"""scripts/bake_glb.py: a textured glb becomes a scene of flat palette materials.

Builds a tiny glb with an embedded 2x2 PNG, a flat-color material and a fully transparent cutout
material, bakes it, and checks the groups, the texture orientation (glTF's v axis points down),
that the cutout is dropped, and that the result loads. Skipped (exit 77) without Pillow and numpy.
"""
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

try:
    import numpy  # noqa: F401
    from PIL import Image
except ImportError:
    print('bake_glb test skipped: Pillow or numpy is not installed')
    sys.exit(77)

SCRIPT, OBJ2BIN, SCENE_DUMP, TEMPLATE = (Path(sys.argv.pop(1)).resolve() for _ in range(4))


def png(pixels):
    image = Image.new('RGBA', (2, 2))
    image.putdata(pixels)
    out = io.BytesIO()
    image.save(out, 'PNG')
    return out.getvalue()


def make_glb(path, primitives, materials, images):
    """primitives: (positions, uvs, indices, material index) each; images: PNG bytes."""
    chunks, views, accessors = [], [], []

    def add(data):
        views.append({'buffer': 0, 'byteOffset': sum(len(c) for c in chunks),
                      'byteLength': len(data)})
        chunks.append(data + b'\0' * (-len(data) % 4))
        return len(views) - 1

    def accessor(data, component, count, kind):
        accessors.append({'bufferView': add(data), 'componentType': component, 'count': count,
                          'type': kind})
        return len(accessors) - 1

    prims = []
    for positions, uvs, indices, material in primitives:
        pos = accessor(b''.join(struct.pack('<3f', *p) for p in positions), 5126, len(positions),
                       'VEC3')
        uv = accessor(b''.join(struct.pack('<2f', *u) for u in uvs), 5126, len(uvs), 'VEC2')
        idx = accessor(b''.join(struct.pack('<H', i) for i in indices), 5123, len(indices),
                       'SCALAR')
        prims.append({'attributes': {'POSITION': pos, 'TEXCOORD_0': uv}, 'indices': idx,
                      'material': material})
    image_defs = [{'bufferView': add(data), 'mimeType': 'image/png'} for data in images]
    binary = b''.join(chunks)
    doc = {'asset': {'version': '2.0'}, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
           'meshes': [{'primitives': prims}], 'accessors': accessors, 'bufferViews': views,
           'buffers': [{'byteLength': len(binary)}], 'materials': materials,
           'textures': [{'source': i} for i in range(len(images))], 'images': image_defs}
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    body = (struct.pack('<I4s', len(text), b'JSON') + text +
            struct.pack('<I4s', len(binary), b'BIN\0') + binary)
    path.write_bytes(struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body)


RED, GREEN, BLUE, WHITE = (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255,) * 4
CLEAR = (255, 255, 255, 0)


class BakeGlbTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        quad = ([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],
                [(0, 0), (1, 0), (1, 1), (0, 1)], [0, 1, 2, 0, 2, 3], 0)
        # A triangle whose UVs all lie in the top-left texel, which is red (v points down).
        corner = ([(6, 0, 0), (7, 0, 0), (6, 1, 0)], [(0.05, 0.05), (0.2, 0.05), (0.05, 0.2)],
                  [0, 1, 2], 0)
        plain = ([(2, 0, 0), (3, 0, 0), (2, 1, 0)], [(0, 0)] * 3, [0, 1, 2], 1)
        leaf = ([(4, 0, 0), (5, 0, 0), (4, 1, 0)], [(0, 0), (1, 0), (0, 1)], [0, 1, 2], 2)
        materials = [
            {'name': 'wall', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}},
            {'name': 'plain', 'pbrMetallicRoughness': {'baseColorFactor': [0.1, 0.2, 0.3, 1.0]}},
            {'name': 'leaf', 'alphaMode': 'BLEND',
             'pbrMetallicRoughness': {'baseColorTexture': {'index': 1}}}]
        make_glb(self.dir / 'model.glb', [quad, plain, leaf, corner], materials,
                 [png([RED, GREEN, BLUE, WHITE]), png([CLEAR] * 4)])
        self.result = subprocess.run(
            [sys.executable, str(SCRIPT), str(self.dir / 'model.glb'), 'baked', '--out-dir',
             str(self.dir), '--obj2bin', str(OBJ2BIN), '--template', str(TEMPLATE)],
            capture_output=True, text=True)

    def groups(self):
        return [line.split()[1] for line in (self.dir / 'baked.obj').read_text().splitlines()
                if line.startswith('o ')]

    def test_bakes_and_the_scene_loads(self):
        self.assertEqual(self.result.returncode, 0, self.result.stdout + self.result.stderr)
        dump = subprocess.run([str(SCENE_DUMP), '--verify', str(self.dir / 'baked.json')],
                              capture_output=True, text=True)
        self.assertEqual(dump.returncode, 0, dump.stderr)
        self.assertIn('validation: PASS', dump.stdout)
        # quad (2) + corner (1) + plain (1); the transparent leaf contributes nothing
        self.assertIn('triangles: 4', dump.stdout)
        scene = json.loads((self.dir / 'baked.json').read_text())
        template = json.loads(TEMPLATE.read_text())
        added = len(scene['materials']) - len(template['materials'])
        self.assertGreaterEqual(added, 3)   # at least: the red corner, a quad color, plain

    def test_groups_are_named_after_materials_and_colors(self):
        groups = self.groups()
        self.assertIn('plain_flat', groups)                       # no texture: its factor
        self.assertTrue(any(g.startswith('wall_') for g in groups))
        self.assertFalse(any(g.startswith('leaf') for g in groups), groups)   # cut out entirely

    def test_texture_v_axis_points_down(self):
        # Red is the top-left texel; if v pointed up the corner triangle would sample blue.
        self.assertIn('wall_v00', self.groups())

    def test_flat_material_keeps_its_factor(self):
        scene = json.loads((self.dir / 'baked.json').read_text())
        near = [m for m in scene['materials']
                if all(abs(a - b) < 1e-4 for a, b in zip(m['albedo'], [0.1, 0.2, 0.3]))]
        self.assertEqual(len(near), 1, near)


if __name__ == '__main__':
    unittest.main()
