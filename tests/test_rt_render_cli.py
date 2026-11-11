"""End-to-end rt_render checks: output files, BVH vs brute force, threads, overrides, errors."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--render', required=True)
parser.add_argument('--dump', required=True)
parser.add_argument('--compare', required=True)
parser.add_argument('--scene', required=True)
args = parser.parse_args()

AOV_SUFFIXES = ('.pfm', '.primid.u32img', '.lightmask.u32img')


def run(command, ok=True, clean_stdout=True):
    result = subprocess.run(command, capture_output=True, text=True)
    if ok:
        assert result.returncode == 0, f'{command}: {result.stdout}\n{result.stderr}'
    else:
        assert result.returncode != 0, f'{command} unexpectedly succeeded'
    # INTERFACES §0: rt_render reports progress on stderr only; results go to files.
    if clean_stdout:
        assert result.stdout == '', f'{command}: stdout must stay clean: {result.stdout}'
    return result


def render(out, *extra):
    return run([args.render, '--scene', args.scene, '--shade', 'normal',
                '--aov', 'primid,lightmask', '--pfm', str(out), *extra])


def same_files(a, b):
    for suffix in AOV_SUFFIXES:
        pa, pb = a.with_suffix('').with_suffix(suffix), b.with_suffix('').with_suffix(suffix)
        assert pa.read_bytes() == pb.read_bytes(), f'{pa} differs from {pb}'


with tempfile.TemporaryDirectory(prefix='rt-render-') as directory:
    root = Path(directory)
    # Reference: Po's normal preview (stub S4). rt_render must reproduce it byte for byte.
    run([args.dump, args.scene, '--render-normal', str(root / 'dump.pfm')], clean_stdout=False)
    render(root / 'median_r1.pfm', '--bvh', 'median', '--render-threads', '1')
    render(root / 'median_r4.pfm', '--bvh', 'median', '--render-threads', '4', '--build-threads', '2',
           '--out', str(root / 'median_r4.png'))
    render(root / 'none_r3.pfm', '--bvh', 'none', '--render-threads', '3')
    for other in ('median_r1', 'median_r4', 'none_r3'):
        same_files(root / 'dump.pfm', root / f'{other}.pfm')
    assert (root / 'median_r4.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    compared = run([args.compare, str(root / 'dump.pfm'), str(root / 'median_r4.pfm')],
                   clean_stdout=False)
    assert 'FAIL' not in compared.stdout, compared.stdout

    # Overrides change the output size; only requested AOVs are written.
    run([args.render, '--scene', args.scene, '--shade', 'normal', '--width', '33', '--height', '17',
         '--cam-pos', '0', '1', '6', '--cam-vfov', '50', '--aov', 'primid', '--pfm', str(root / 'small.pfm')])
    header = (root / 'small.pfm').read_bytes().split(b'\n')[1]
    assert header == b'33 17', header
    assert (root / 'small.primid.u32img').exists()
    assert not (root / 'small.lightmask.u32img').exists()

    # Lit shading is the default. BVH and brute force agree byte for byte, rt_compare passes,
    # and the lit image differs from the normal one.
    lit = ['--aov', 'primid,lightmask']
    run([args.render, '--scene', args.scene, *lit, '--pfm', str(root / 'lit_default.pfm')])
    run([args.render, '--scene', args.scene, '--shade', 'lit', '--bvh', 'none', *lit,
         '--pfm', str(root / 'lit_none.pfm')])
    run([args.render, '--scene', args.scene, '--shade', 'lit', '--bvh', 'median', '--render-threads', '4',
         *lit, '--pfm', str(root / 'lit_median.pfm'), '--out', str(root / 'lit.png')])
    same_files(root / 'lit_default.pfm', root / 'lit_none.pfm')
    same_files(root / 'lit_none.pfm', root / 'lit_median.pfm')
    compared = run([args.compare, str(root / 'lit_none.pfm'), str(root / 'lit_median.pfm')],
                   clean_stdout=False)
    assert 'FAIL' not in compared.stdout, compared.stdout
    assert (root / 'lit_none.pfm').read_bytes() != (root / 'dump.pfm').read_bytes()
    assert (root / 'lit.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')

    # Errors exit non-zero with a message on stderr.
    failures = {
        'cuda backend': ['--backend', 'cuda', '--shade', 'normal'],
        'only Median': ['--bvh', 'lbvh', '--shade', 'normal'],
        'vfov_deg': ['--shade', 'normal', '--cam-vfov', '180'],
        '--pfm': ['--shade', 'normal', '--aov', 'primid'],
    }
    for text, extra in failures.items():
        result = run([args.render, '--scene', args.scene, *extra], ok=False)
        assert text in result.stderr, f'{extra}: expected "{text}" in {result.stderr}'
    result = run([args.render, '--scene', str(root / 'missing.json'), '--shade', 'normal'], ok=False)
    assert result.stderr.startswith('error: '), result.stderr

print('rt_render CLI: PASS')
