"""Exercise actual CLI output files, thread determinism, corruption exits and repeatability."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--dump', required=True)
parser.add_argument('--render', required=True)
parser.add_argument('--compare', required=True)
parser.add_argument('--generator', required=True)
parser.add_argument('--scene', required=True)
args = parser.parse_args()

def run(command, expected=0, cwd=None):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    assert result.returncode == expected, f'{command}: {result.stdout}\n{result.stderr}'
    return result

with tempfile.TemporaryDirectory(prefix='rt-cli-') as directory:
    root = Path(directory)
    for threads in (1, 4):
        pfm = root / f't{threads}.pfm'
        run([args.render, '--scene', args.scene, '--backend', 'cpu', '--shade', 'lit',
             '--pfm', str(pfm), '--out', str(pfm.with_suffix('.png')),
             '--aov', 'primid,lightmask', '--render-threads', str(threads)])
    for extension in ('.pfm', '.primid.u32img', '.lightmask.u32img'):
        assert (root / ('t1' + extension)).read_bytes() == (root / ('t4' + extension)).read_bytes()
    assert (root / 't1.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    compared = run([args.compare, str(root / 't1.pfm'), str(root / 't4.pfm')])
    assert compared.stdout.count('PASS ') == 5 and '100%' in compared.stdout
    pfm = root / 't4.pfm'
    contents = bytearray(pfm.read_bytes())
    payload = 0
    for _ in range(3):
        payload = contents.index(b'\n', payload) + 1
    contents[payload:payload + 4] = struct.pack('<f', float('nan'))
    pfm.write_bytes(contents)
    failed = run([args.compare, str(root / 't1.pfm'), str(pfm)], expected=1)
    assert 'FAIL nonfinite: 1 pixels' in failed.stdout
    run([args.compare, str(root / 't1.pfm'), str(pfm), '--tau-abs', '-1'], expected=1)
    prefix = root / 'repeat'
    command = [args.generator, '--layout', 'uniform', '--primitives', '17', '--seed', '42', '--out', str(prefix)]
    run(command)
    def hashes():
        return [hashlib.sha256(prefix.with_suffix(suffix).read_bytes()).digest() for suffix in ('.json', '.bin')]
    before = hashes()
    run(command)
    assert before == hashes()
    metadata = json.loads(prefix.with_suffix('.json').read_text())['generator']
    assert metadata['terrain_source'] == 'stub-S3'
    assert metadata['num_spheres'] + metadata['num_triangles'] == 17
    assert metadata['command'] == f'rt_gen --layout uniform --primitives 17 --seed 42 --out {prefix}'
    # The same arguments through a different invocation path and working directory must
    # give the same bytes: the manifest records each file's SHA-256.
    generator = Path(args.generator).resolve()
    # Resolve /var -> /private/var before counting parent components on macOS.
    relative = Path(os.path.relpath(generator, prefix.parent.resolve()))
    run([str(relative), '--layout', 'uniform', '--primitives', '17', '--seed', '42', '--out', str(prefix)],
        cwd=prefix.parent)
    assert before == hashes()
    run([args.dump, '--verify', str(prefix.with_suffix('.json'))])
print('CPU files, 1/N determinism, comparator failure exits and generator bytes verified')
