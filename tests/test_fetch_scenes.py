"""Retrieve an independently encoded local archive; reject compressed/hash corruption."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

args = argparse.ArgumentParser()
args.add_argument('--scene-dump', required=True)
args.add_argument('--fetch-script', required=True)
args.add_argument('--known-scene', required=True)
options = args.parse_args()

def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

with tempfile.TemporaryDirectory(prefix='rt-fetch-test-') as directory:
    root = Path(directory)
    archive = root / 'archive'
    archive.mkdir()
    document = json.loads(Path(options.known_scene).read_text())
    spheres = document['geometry']['spheres']
    triangles = document['geometry']['triangles']
    sphere_bytes = b''.join(struct.pack('<4fI', *s['center'], s['radius'], s['material']) for s in spheres)
    triangle_bytes = b''.join(struct.pack('<9fI', *(v for vertex in t['v'] for v in vertex), t['material']) for t in triangles)
    content_hash = fnv(struct.pack('<II', len(spheres), len(triangles)) + sphere_bytes + triangle_bytes)
    triangle_offset = (64 + len(sphere_bytes) + 15) & ~15
    binary = struct.pack('<8sIIIIQQQ16s', b'RTSCENE\0', 2, 64, len(spheres), len(triangles),
                         64, triangle_offset, content_hash, bytes(16))
    binary += sphere_bytes + bytes(triangle_offset - 64 - len(sphere_bytes)) + triangle_bytes
    document['geometry'] = {'binary': 'fixture.bin'}
    (archive / 'fixture.json').write_text(json.dumps(document))
    (archive / 'fixture.bin.gz').write_bytes(gzip.compress(binary, mtime=0))
    files = []
    for name in ('fixture.json', 'fixture.bin.gz'):
        data = (archive / name).read_bytes()
        entry = {'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        if name.endswith('.gz'):
            entry['uncompressed_sha256'] = hashlib.sha256(binary).hexdigest()
        files.append(entry)
    manifest_entry = {'set': 'test-v1', 'name': 'fixture', 'content_hash': f'0x{content_hash:016x}',
                      'scene_hash': '0xcbed9151089614c9', 'files': files,
                      'archive': {'release': 'test-v1', 'urls': [(archive / f['path']).as_uri() for f in files]}}
    manifest = root / 'manifest.json'
    manifest.write_text(json.dumps({'scenes': [manifest_entry]}))
    destination = root / 'downloaded'
    env = {**os.environ, 'RT_SCENE_MANIFEST': str(manifest), 'RT_SCENE_DEST': str(destination),
           'RT_SCENE_DUMP': options.scene_dump}
    def fetch():
        return subprocess.run(['bash', options.fetch_script, 'test-v1'], env=env, capture_output=True, text=True)
    result = fetch()
    assert result.returncode == 0, result.stderr
    assert (destination / 'fixture.bin').read_bytes() == binary
    assert fetch().returncode == 0  # validated cache reuse
    # A corrupted source must fail before any files are installed in a fresh destination.
    (archive / 'fixture.bin.gz').write_bytes(b'corrupted')
    env['RT_SCENE_DEST'] = str(root / 'rejected')
    result = fetch()
    assert result.returncode != 0 and 'SHA-256 mismatch' in result.stderr, result.stderr
    assert not list((root / 'rejected').iterdir())
print('Archive retrieval, independent binary decoding, cache verification and corruption rejection pass')
