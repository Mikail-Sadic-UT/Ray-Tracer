"""rt_render --shade lit on each known scene, compared with its committed golden by rt_compare.

The goldens are regression-only: rendered by this repository's renderer (scenes/golden/README.md).
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--render', required=True)
parser.add_argument('--compare', required=True)
parser.add_argument('--scenes', required=True)
parser.add_argument('--goldens', required=True)
args = parser.parse_args()

SCENES = ('known_two_spheres', 'known_single_sphere', 'known_degenerate_triangle')


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, f'{command}: {result.stdout}\n{result.stderr}'
    return result


with tempfile.TemporaryDirectory() as tmp:
    for name in SCENES:
        golden = Path(args.goldens) / f'{name}.pfm'
        for suffix in ('.pfm', '.primid.u32img', '.lightmask.u32img'):
            assert (Path(args.goldens) / f'{name}{suffix}').is_file(), f'missing golden {name}{suffix}'
        for bvh, threads in (('median', '1'), ('median', '4'), ('none', '4')):
            out = Path(tmp) / f'{name}_{bvh}_{threads}.pfm'
            run([args.render, '--scene', str(Path(args.scenes) / f'{name}.json'), '--backend', 'cpu',
                 '--bvh', bvh, '--shade', 'lit', '--render-threads', threads,
                 '--aov', 'primid,lightmask', '--pfm', str(out)])
            result = run([args.compare, str(golden), str(out)])
            assert 'FAIL' not in result.stdout, f'{name} {bvh} {threads}: {result.stdout}'
            print(f'{name} bvh={bvh} threads={threads}: rt_compare ok')
