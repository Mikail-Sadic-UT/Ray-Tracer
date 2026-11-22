"""End-to-end rt_render --backend cuda checks: CPU agreement under rt_compare, repeat
determinism, the results file, and CLI errors. Exits 77 (ctest SKIP) without a CUDA device."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
parser = argparse.ArgumentParser()
parser.add_argument('--render', required=True)
parser.add_argument('--compare', required=True)
parser.add_argument('--scenes', required=True, help='directory with the known_*.json scenes')
args = parser.parse_args()
SCENES = ['known_two_spheres', 'known_single_sphere', 'known_degenerate_triangle']
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
def scene(name):
    return str(Path(args.scenes) / f'{name}.json')
def render(name, out, *extra):
    return run([args.render, '--scene', scene(name), '--aov', 'primid,lightmask',
                '--pfm', str(out), *extra])
def same_files(a, b):
    return all(Path(str(a).replace('.pfm', s)).read_bytes() ==
               Path(str(b).replace('.pfm', s)).read_bytes() for s in AOV_SUFFIXES)
probe = subprocess.run([args.render, '--scene', scene(SCENES[0]), '--backend', 'cuda'],
                       capture_output=True, text=True)
if probe.returncode != 0 and 'no CUDA-capable device' in probe.stderr:
    print('skipped: no CUDA device', file=sys.stderr)
    sys.exit(77)
assert probe.returncode == 0, probe.stderr
with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp)
    for name in SCENES:
        for bvh in ('median', 'lbvh', 'none'):
            for shade in ('lit', 'normal'):
                tag = f'{name}.{bvh}.{shade}'
                cpu, gpu, again = (out / f'{tag}.{k}.pfm' for k in ('cpu', 'cuda', 'again'))
                common = ('--bvh', bvh, '--shade', shade, '--build-threads', '4')
                render(name, cpu, '--backend', 'cpu', '--render-threads', '4', *common)
                render(name, gpu, '--backend', 'cuda', *common)
                render(name, again, '--backend', 'cuda', *common)
                # rt_compare exits non-zero if any check fails and prints its report on stdout.
                result = run([args.compare, str(cpu), str(gpu)], clean_stdout=False)
                assert 'FAIL' not in result.stdout, f'{tag}: {result.stdout}'
                assert same_files(gpu, again), f'{tag}: repeated cuda runs differ'
    # BVH and brute force agree exactly on the device.
    for name in SCENES:
        assert same_files(out / f'{name}.median.lit.cuda.pfm', out / f'{name}.none.lit.cuda.pfm')
    # Results files: app and prepared mode, CUDA components, kernel diagnostics, GPU identity.
    app = out / 'app.json'
    render(SCENES[0], out / 'app.pfm', '--backend', 'cuda', '--results', str(app))
    doc = json.loads(app.read_text())
    assert doc['config']['backend'] == 'cuda' and doc['config']['pipeline'] == 'mega', doc['config']
    assert doc['machine']['gpu_name'] and doc['machine']['gpu_uuid'].startswith('GPU-')
    assert doc['machine']['cuda_runtime'], doc['machine']
    comp = doc['app']['components']
    for key in ('cuda_init', 'alloc', 'upload', 'render', 'download'):
        assert comp[key] > 0, (key, comp)
    prepared = out / 'prepared.json'
    render(SCENES[0], out / 'prepared.pfm', '--backend', 'cuda', '--mode', 'prepared',
           '--runs', '3', '--results', str(prepared))
    doc = json.loads(prepared.read_text())
    for section in ('build_and_render', 'frame'):
        assert len(doc[section]['runs']) == 3
        for r in doc[section]['runs']:
            assert 0 < r['diagnostics']['render_kernel'] <= r['components']['render'], r
            assert r['components']['download'] > 0, r
    for r in doc['build_and_render']['runs']:
        assert r['components']['alloc'] > 0 and r['components']['upload'] > 0, r
    # The cpu backend keeps pipeline "none" and records no kernel time.
    cpu_res = out / 'cpu.json'
    render(SCENES[0], out / 'cpu.pfm', '--backend', 'cpu', '--mode', 'prepared', '--runs', '1',
           '--results', str(cpu_res))
    doc = json.loads(cpu_res.read_text())
    assert doc['config']['pipeline'] == 'none'
    assert doc['frame']['runs'][0]['diagnostics'] == {}, doc['frame']
    # CLI errors.
    err = run([args.render, '--scene', scene(SCENES[0]), '--backend', 'cuda',
               '--pipeline', 'wavefront'], ok=False)
    assert 'E5' in err.stderr, err.stderr
    err = run([args.render, '--scene', scene(SCENES[0]), '--backend', 'cpu',
               '--pipeline', 'mega'], ok=False)
    assert 'only to --backend cuda' in err.stderr, err.stderr
print('rt_render cuda CLI checks passed', file=sys.stderr)
