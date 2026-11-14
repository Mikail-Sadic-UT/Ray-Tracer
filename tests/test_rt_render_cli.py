"""End-to-end rt_render checks: output files, BVH vs brute force, threads, overrides, errors."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--render', required=True)
parser.add_argument('--compare', required=True)
parser.add_argument('--generator', required=True)
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


COMPONENTS = {
    'frame': ['render', 'download'],
    'build_and_render': ['bounds', 'build', 'layout', 'convert', 'alloc', 'upload', 'render',
                         'download'],
}
COMPONENTS['app'] = ['args', 'load', 'cuda_init'] + COMPONENTS['build_and_render']


def pfm_rgb_hash(path):
    """FNV-1a 64 over the little-endian bits of the RGB floats, top row first (INTERFACES §8),
    computed from the PFM independently of rt_bench."""
    data = path.read_bytes()
    header, rest = data.split(b'\n', 1)
    dims, rest = rest.split(b'\n', 1)
    scale, pixels = rest.split(b'\n', 1)
    assert header == b'PF' and float(scale) < 0, 'expected a little-endian RGB PFM'
    width, height = map(int, dims.split())
    row = width * 3 * 4
    rows = [pixels[y * row:(y + 1) * row] for y in range(height)][::-1]  # PFM is bottom-up
    h = 0xcbf29ce484222325
    for byte in b''.join(rows):
        h = ((h ^ byte) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f'0x{h:016x}'


def check_run(run, section):
    names = COMPONENTS[section]
    assert sorted(run['components']) == sorted(names), run['components']
    expected = run['total_ms'] - sum(run['components'][n] for n in names)
    assert abs(run['unattributed_ms'] - expected) < 1e-6, (run, expected)
    assert 0 <= run['unattributed_ms'] < run['total_ms'], run


def check_results(path, sections):
    doc = json.loads(path.read_text())
    assert doc['schema_version'] == 2
    present = [k for k in ('app', 'build_and_render', 'frame') if k in doc]
    assert present == sections, present
    if 'app' in doc:
        check_run(doc['app'], 'app')
        assert doc['app']['components']['render'] > 0
    for section in ('build_and_render', 'frame'):
        if section not in doc:
            continue
        s = doc[section]
        totals = [r['total_ms'] for r in s['runs']]
        for r in s['runs']:
            check_run(r, section)
            assert r['diagnostics'] == {}, 'the CPU backend records no diagnostics'
        assert s['min_ms'] == min(totals) and s['max_ms'] == max(totals)
        assert s['min_ms'] <= s['median_ms'] <= s['max_ms']
        assert 0 <= s['median_run_index'] < len(totals)
    assert doc['machine']['cpu_cores'] > 0 and doc['build']['commit']
    return doc


def same_files(a, b):
    for suffix in AOV_SUFFIXES:
        pa, pb = a.with_suffix('').with_suffix(suffix), b.with_suffix('').with_suffix(suffix)
        assert pa.read_bytes() == pb.read_bytes(), f'{pa} differs from {pb}'


with tempfile.TemporaryDirectory(prefix='rt-render-') as directory:
    root = Path(directory)
    # Brute-force reference; the C++ CPU tests also check each pixel against render_pixel.
    render(root / 'reference.pfm', '--bvh', 'none', '--render-threads', '1')
    render(root / 'median_r1.pfm', '--bvh', 'median', '--render-threads', '1')
    render(root / 'median_r4.pfm', '--bvh', 'median', '--render-threads', '4', '--build-threads', '2',
           '--out', str(root / 'median_r4.png'))
    render(root / 'none_r3.pfm', '--bvh', 'none', '--render-threads', '3')
    for other in ('median_r1', 'median_r4', 'none_r3'):
        same_files(root / 'reference.pfm', root / f'{other}.pfm')
    assert (root / 'median_r4.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    compared = run([args.compare, str(root / 'reference.pfm'), str(root / 'median_r4.pfm')],
                   clean_stdout=False)
    assert 'FAIL' not in compared.stdout, compared.stdout

    # LBVH is supported and preserves the brute-force RGB and AOV bytes.
    render(root / 'lbvh.pfm', '--bvh', 'lbvh', '--build-threads', '8', '--max-leaf-size', '1')
    same_files(root / 'reference.pfm', root / 'lbvh.pfm')

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
    assert (root / 'lit_none.pfm').read_bytes() != (root / 'reference.pfm').read_bytes()
    assert (root / 'lit.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')

    run([args.render, '--scene', args.scene, '--shade', 'lit', '--bvh', 'lbvh',
         '--build-threads', '8', '--render-threads', '4', *lit,
         '--pfm', str(root / 'lit_lbvh.pfm')])
    same_files(root / 'lit_none.pfm', root / 'lit_lbvh.pfm')

    # --mode app (default): one T_app record. The image is the same as without --results.
    run([args.render, '--scene', args.scene, *lit, '--pfm', str(root / 'app.pfm'),
         '--results', str(root / 'app.json')])
    same_files(root / 'lit_default.pfm', root / 'app.pfm')
    doc = check_results(root / 'app.json', ['app'])
    assert doc['image']['rgb_hash'] == pfm_rgb_hash(root / 'app.pfm'), doc['image']
    assert doc['image']['path'] == str(root / 'app.pfm')
    assert doc['scene']['json'] == args.scene and doc['scene']['generator_command'] == ''
    assert doc['scene']['num_primitives'] == doc['scene']['num_spheres'] + doc['scene']['num_triangles']
    assert doc['config']['query'] == 'bvh' and doc['config']['shade'] == 'lit'
    assert doc['config']['aovs'] == ['primid', 'lightmask'] and doc['config']['camera']['overridden'] == []
    assert 'bvh_stats' in doc

    # --mode prepared: --runs runs of T_br and T_frame, and the same image bytes as app mode.
    run([args.render, '--scene', args.scene, *lit, '--mode', 'prepared', '--runs', '3',
         '--render-threads', '4', '--pfm', str(root / 'prep.pfm'), '--results', str(root / 'prep.json')])
    same_files(root / 'app.pfm', root / 'prep.pfm')
    doc = check_results(root / 'prep.json', ['build_and_render', 'frame'])
    assert len(doc['build_and_render']['runs']) == 3 and len(doc['frame']['runs']) == 3
    assert doc['image']['rgb_hash'] == pfm_rgb_hash(root / 'prep.pfm')
    assert doc['config']['render_threads'] == 4

    run([args.render, '--scene', args.scene, *lit, '--bvh', 'lbvh', '--build-threads', '8',
         '--mode', 'prepared', '--runs', '2', '--pfm', str(root / 'prep_lbvh.pfm'),
         '--results', str(root / 'prep_lbvh.json')])
    same_files(root / 'app.pfm', root / 'prep_lbvh.pfm')
    lbvh_doc = check_results(root / 'prep_lbvh.json', ['build_and_render', 'frame'])
    assert lbvh_doc['config']['bvh'] == 'lbvh'
    assert lbvh_doc['bvh_stats']['fallbacks'] == 0

    # Brute force has no BVH section; overrides are recorded.
    run([args.render, '--scene', args.scene, '--bvh', 'none', '--width', '40', '--height', '30',
         '--cam-vfov', '50', '--out', str(root / 'brute.png'), '--results', str(root / 'brute.json')])
    doc = check_results(root / 'brute.json', ['app'])
    assert 'bvh_stats' not in doc and doc['config']['query'] == 'brute'
    assert doc['config']['camera']['overridden'] == ['width', 'height', 'vfov_deg']
    assert (doc['config']['width'], doc['config']['height']) == (40, 30)

    # A generated scene records the exact rt_gen command it came from.
    run([args.generator, '--primitives', '200', '--seed', '3', '--out', str(root / 'gen')],
        clean_stdout=False)
    command = json.loads((root / 'gen.json').read_text())['generator']['command']
    run([args.render, '--scene', str(root / 'gen.json'), '--width', '32', '--height', '18',
         '--mode', 'prepared', '--runs', '1', '--results', str(root / 'gen_results.json')])
    doc = check_results(root / 'gen_results.json', ['build_and_render', 'frame'])
    assert doc['scene']['generator_command'] == command, doc['scene']
    assert doc['scene']['num_primitives'] == 200

    # Errors exit non-zero with a message on stderr.
    failures = {
        '--runs applies only': ['--runs', '3'],
        'cannot open': ['--results', str(root / 'missing_dir' / 'r.json')],
        'embree backend': ['--backend', 'embree', '--shade', 'normal'],
        'only Median': ['--bvh', 'sah', '--shade', 'normal'],
        'vfov_deg': ['--shade', 'normal', '--cam-vfov', '180'],
        '--pfm': ['--shade', 'normal', '--aov', 'primid'],
    }
    for text, extra in failures.items():
        result = run([args.render, '--scene', args.scene, *extra], ok=False)
        assert text in result.stderr, f'{extra}: expected "{text}" in {result.stderr}'
    result = run([args.render, '--scene', str(root / 'missing.json'), '--shade', 'normal'], ok=False)
    assert result.stderr.startswith('error: '), result.stderr

print('rt_render CLI: PASS')
