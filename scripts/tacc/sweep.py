#!/usr/bin/env python3
"""Run the benchmark sweeps of docs/BENCHMARKING.md "Sweeps" and merge them into one CSV.

A sweep is a list of points. Each point is one rt_render configuration (backend, scene, BVH,
threads, resolution) measured either in prepared mode (T_br and T_frame, one process) or as
T_app (fresh processes through scripts/bench/run_tapp.py). Presets vary one dimension at a time
around the baseline: bench_1m, 1920x1080, lbvh, mega, explicit, B = R = max.

    sweep.py plan  --preset core --machine vista --scenes DIR
    sweep.py run   --preset core --machine vista --scenes DIR --rt-render BIN --out DIR
    sweep.py merge --out DIR

`run` records the machine identity, checks that rt_render loads GCC's libgomp, measures every
point, validates each results file against schema v2, and finally runs `merge`, which writes
DIR/sweep.csv. A point whose valid results already exist is skipped, so a job that hit its time
limit can be resubmitted with the same command (`--fresh` measures everything again).

Exit status: 0 on success, 1 if a point or a results file fails, 2 for a usage error.
"""
import argparse
import csv
from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

BENCH = Path(__file__).resolve().parent.parent / 'bench'
sys.path.insert(0, str(BENCH))
import results_schema as rs  # noqa: E402
import run_tapp  # noqa: E402

# BENCHMARKING.md "Sweeps": B and R together, and B alone for GPU runs.
THREADS = {
    'vista': [1, 2, 4, 8, 16, 32, 64, 72],
    'laptop': [1, 2, 4, 8, 16, 24, 32],
}
GPU_BUILD_THREADS = {
    'vista': [1, 8, 72],
    'laptop': [1, 8, 32],
}
SIZES = ['bench_10k_s1', 'bench_100k_s1', 'bench_1m_s1']
BASELINE_SCENE = 'bench_1m_s1'
FULL_HD = (1920, 1080)
VGA = (640, 480)
SPREAD_WARNING = 0.10


class SweepError(Exception):
    """A point or results file that cannot be measured or merged."""


@dataclass(frozen=True)
class Point:
    """One rt_render configuration. `mode` is `prepared` (T_br, T_frame) or `app` (T_app)."""
    backend: str
    scene: str
    bvh: str
    build_threads: int
    render_threads: int
    width: int
    height: int
    mode: str = 'prepared'

    @property
    def name(self):
        threads = (f'B{self.build_threads}' if self.backend == 'cuda' else
                   f'B{self.build_threads}R{self.render_threads}')
        return (f'{self.mode}_{self.backend}_{self.scene}_{self.bvh}_{threads}_'
                f'{self.width}x{self.height}')


def cpu(scene, threads, bvh='lbvh', res=FULL_HD, mode='prepared'):
    return Point('cpu', scene, bvh, threads, threads, res[0], res[1], mode)


def gpu(scene, build_threads, bvh='lbvh', res=FULL_HD, mode='prepared'):
    # The GPU renders; R is not used (BENCHMARKING "Threads") and is recorded as 1.
    return Point('cuda', scene, bvh, build_threads, 1, res[0], res[1], mode)


def preset_points(preset, machine):
    """The points of a named preset, in run order, without duplicates."""
    threads = THREADS[machine]
    top = threads[-1]
    gpu_top = GPU_BUILD_THREADS[machine][-1]
    presets = {
        # B = R together on the CPU; T = 1 is the 1-core baseline.
        'cpu-threads': lambda: [cpu(BASELINE_SCENE, t) for t in threads],
        # B alone for GPU runs.
        'gpu-build-threads': lambda: [gpu(BASELINE_SCENE, b)
                                      for b in GPU_BUILD_THREADS[machine]],
        # Scene size, with the 1-core baseline at each size for the target ratios.
        'size': lambda: [p for s in SIZES for p in (cpu(s, 1), cpu(s, top), gpu(s, gpu_top))],
        'bvh': lambda: [p for m in ('median', 'lbvh')
                        for p in (cpu(BASELINE_SCENE, top, m), gpu(BASELINE_SCENE, gpu_top, m))],
        'resolution': lambda: [p for r in (VGA, FULL_HD)
                               for p in (cpu(BASELINE_SCENE, top, res=r),
                                         gpu(BASELINE_SCENE, gpu_top, res=r))],
        # T_app: fresh processes, at every size.
        'tapp': lambda: [p for s in SIZES
                         for p in (cpu(s, 1, mode='app'), cpu(s, top, mode='app'),
                                   gpu(s, gpu_top, mode='app'))],
        # Small and fast: checks a node and the scripts end to end on the known scene.
        'smoke': lambda: [cpu('known_two_spheres', 1, res=(160, 120)),
                          cpu('known_two_spheres', 2, res=(160, 120)),
                          gpu('known_two_spheres', 2, res=(160, 120)),
                          cpu('known_two_spheres', 2, res=(160, 120), mode='app'),
                          gpu('known_two_spheres', 2, res=(160, 120), mode='app')],
    }
    # G2: first T_app / T_br / T_frame at 3 sizes. Core: every Core dimension.
    presets['g2'] = lambda: presets['size']() + presets['tapp']()
    presets['core'] = lambda: (presets['cpu-threads']() + presets['gpu-build-threads']() +
                               presets['size']() + presets['bvh']() + presets['resolution']() +
                               presets['tapp']())
    if preset not in presets:
        raise SweepError(f'unknown preset {preset!r}; choose from {", ".join(sorted(presets))}')
    seen, points = set(), []
    for p in presets[preset]():
        if p.name not in seen:
            seen.add(p.name)
            points.append(p)
    return points


PRESETS = ('core', 'g2', 'cpu-threads', 'gpu-build-threads', 'size', 'bvh', 'resolution',
           'tapp', 'smoke')


KNOWN_SCENES = Path(__file__).resolve().parent.parent.parent / 'scenes' / 'known'


def scene_path(scenes, name):
    """`scenes/<name>.json`; the hand-written known_* scenes always come from the repo."""
    path = (KNOWN_SCENES if name.startswith('known_') else Path(scenes)) / f'{name}.json'
    if not path.is_file():
        raise SweepError(f'scene {path} not found (fetch it with scripts/fetch_scenes.sh '
                         'scenes-v1, or pass --scenes)')
    return path


def render_args(point, scenes, runs=None, images_dir=None):
    """rt_render arguments for `point`, without the binary, --mode and --results."""
    args = ['--scene', str(scene_path(scenes, point.scene)), '--backend', point.backend,
            '--bvh', point.bvh, '--build-threads', str(point.build_threads),
            '--render-threads', str(point.render_threads),
            '--width', str(point.width), '--height', str(point.height), '--shade', 'lit']
    if point.backend == 'cuda':
        args += ['--pipeline', 'mega']
    if runs is not None:
        args += ['--runs', str(runs)]
    if images_dir is not None:
        args += ['--pfm', str(Path(images_dir) / 'image.pfm'), '--aov', 'primid,lightmask']
    return args


# ---- machine identity and environment checks -----------------------------------------------

IDENTITY_COMMANDS = {
    'gpu.csv': ['nvidia-smi', '--query-gpu=name,uuid,driver_version,memory.total',
                '--format=csv'],
    'lscpu.txt': ['lscpu'],
    'gcc.txt': ['gcc', '--version'],
    'nvcc.txt': ['nvcc', '--version'],
    'hostname.txt': ['hostname', '-f'],
}


def record_identity(out, rt_render):
    """Writes machine identity (BENCHMARKING "Machine identity") into out/identity/.

    A missing tool is recorded as missing, not an error: the laptop has no `module`, and a
    CPU-only node has no `nvidia-smi`.
    """
    ident = out / 'identity'
    ident.mkdir(parents=True, exist_ok=True)
    for name, command in IDENTITY_COMMANDS.items():
        try:
            r = subprocess.run(command, capture_output=True, text=True)
            text = r.stdout + r.stderr
        except OSError as error:
            text = f'{command[0]}: not available ({error.strerror})\n'
        (ident / name).write_text(text)
    # `module list` is a shell function; sbatch jobs pass its output in RT_MODULE_LIST.
    (ident / 'modules.txt').write_text(os.environ.get('RT_MODULE_LIST', 'not recorded\n'))
    env = {k: os.environ.get(k, '') for k in ('OMP_PROC_BIND', 'OMP_PLACES', 'OMP_NUM_THREADS',
                                               'CUDA_VISIBLE_DEVICES', 'SLURM_JOB_ID',
                                               'SLURM_JOB_PARTITION', 'SLURM_JOB_NODELIST')}
    (ident / 'env.json').write_text(json.dumps(env, indent=2) + '\n')
    ldd = subprocess.run(['ldd', str(rt_render)], capture_output=True, text=True).stdout
    (ident / 'ldd.txt').write_text(ldd)
    return ldd


def check_openmp(ldd):
    """BUILD.md "Vista setup": the binary must load GCC's libgomp, not an NVIDIA runtime."""
    lines = [line for line in ldd.splitlines() if 'omp' in line.lower() or 'nvhpc' in line]
    if not any('libgomp' in line for line in lines):
        raise SweepError('rt_render does not load libgomp; see identity/ldd.txt and BUILD.md '
                         '"Vista setup"')
    bad = [line.strip() for line in lines if 'libgomp' not in line or 'nvhpc' in line.lower()
           or 'nvidia' in line.lower()]
    if bad:
        raise SweepError('rt_render loads a wrong OpenMP runtime: ' + '; '.join(bad))


def check_pinning(environ=os.environ):
    """BENCHMARKING "Thread pinning": every job exports OMP_PROC_BIND=close OMP_PLACES=cores."""
    if environ.get('OMP_PROC_BIND') != 'close' or environ.get('OMP_PLACES') != 'cores':
        raise SweepError('export OMP_PROC_BIND=close OMP_PLACES=cores before a sweep '
                         '(BENCHMARKING.md "Thread pinning")')


# ---- measuring --------------------------------------------------------------------------------

def point_dir(out, point):
    return out / 'points' / point.name


def point_done(out, point, processes):
    """True if `point` already has valid results of the expected shape."""
    d = point_dir(out, point)
    try:
        if point.mode == 'prepared':
            doc = rs.load_results(d / 'prepared.json')
            return 'build_and_render' in doc and 'frame' in doc
        files = [d / f'tapp_{i}.json' for i in range(processes)]
        if not all(f.is_file() for f in files):
            return False
        run_tapp.aggregate(run_tapp.load_all([str(f) for f in files]), [str(f) for f in files])
        return True
    except (rs.ResultsError, run_tapp.TappError, OSError):
        return False


def measure(point, args, out, log):
    """Measures one point; returns the elapsed seconds."""
    d = point_dir(out, point)
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    images = d if args.images else None
    begin = time.monotonic()
    if point.mode == 'prepared':
        results = d / 'prepared.json'
        command = [str(args.rt_render), *render_args(point, args.scenes, args.runs, images),
                   '--mode', 'prepared', '--results', str(results)]
        (d / 'command.txt').write_text(' '.join(command) + '\n')
        r = subprocess.run(command, capture_output=True, text=True)
        (d / 'stderr.txt').write_text(r.stderr)
        if r.returncode != 0:
            tail = ' | '.join(r.stderr.strip().splitlines()[-3:])
            raise SweepError(f'{point.name}: rt_render exited {r.returncode}: {tail}')
        rs.load_results(results)  # validates schema v2
    else:
        extra = render_args(point, args.scenes, None, images)
        (d / 'command.txt').write_text(
            ' '.join([str(args.rt_render), '--mode', 'app', *extra]) + '\n')
        try:
            paths = run_tapp.run_processes(args.rt_render, d, args.processes, extra, log=log)
            run_tapp.aggregate(run_tapp.load_all(paths), paths)
        except run_tapp.TappError as error:
            raise SweepError(f'{point.name}: {error}') from error
    return time.monotonic() - begin


# ---- merging ----------------------------------------------------------------------------------

COMPONENT_COLUMNS = ('args', 'load', 'cuda_init', 'bounds', 'build', 'layout', 'convert',
                     'alloc', 'upload', 'render', 'download')
CSV_COLUMNS = (['point', 'quantity', 'backend', 'pipeline', 'scene', 'scene_hash',
                'num_primitives', 'bvh', 'build_threads', 'render_threads', 'width', 'height',
                'runs', 'median_ms', 'min_ms', 'max_ms', 'spread', 'median_run_index'] +
               [f'{c}_ms' for c in COMPONENT_COLUMNS] +
               ['render_kernel_ms', 'unattributed_ms', 'host', 'cpu_model', 'gpu_name',
                'commit', 'dirty'])


def _row(point_name, quantity, doc, stats, runs, median_run):
    c = doc['config']
    m = doc['machine']
    row = {
        'point': point_name, 'quantity': quantity, 'backend': c['backend'],
        'pipeline': c['pipeline'], 'scene': Path(doc['scene']['json']).stem,
        'scene_hash': doc['scene']['hash'], 'num_primitives': doc['scene']['num_primitives'],
        'bvh': c['bvh'], 'build_threads': c['build_threads'],
        'render_threads': c['render_threads'], 'width': c['width'], 'height': c['height'],
        'runs': runs, 'median_ms': stats['median_ms'], 'min_ms': stats['min_ms'],
        'max_ms': stats['max_ms'], 'spread': stats['spread'],
        'median_run_index': stats['median_run_index'],
        'render_kernel_ms': median_run.get('diagnostics', {}).get('render_kernel', ''),
        'unattributed_ms': median_run['unattributed_ms'], 'host': m['host'],
        'cpu_model': m['cpu_model'], 'gpu_name': m['gpu_name'],
        'commit': doc['build']['commit'], 'dirty': doc['build']['dirty'],
    }
    for name in COMPONENT_COLUMNS:
        row[f'{name}_ms'] = median_run['components'].get(name, '')
    return row


def merge(out):
    """Validates every point under out/points and writes out/sweep.csv; returns (rows, warnings).

    Each prepared point gives a T_br and a T_frame row, each T_app point one T_app row. The
    component columns come from the run whose total is the median (`median_run_index`), never
    from component medians (BENCHMARKING "Timing quantities").
    """
    rows, warnings = [], []
    points = sorted((out / 'points').glob('*')) if (out / 'points').is_dir() else []
    for d in points:
        prepared = d / 'prepared.json'
        if prepared.is_file():
            doc = rs.load_results(prepared)
            for quantity, section in (('T_br', 'build_and_render'), ('T_frame', 'frame')):
                s = doc[section]
                rows.append(_row(d.name, quantity, doc, s, len(s['runs']),
                                 s['runs'][s['median_run_index']]))
            continue
        files = sorted(str(f) for f in d.glob('tapp_*.json'))
        if not files:
            warnings.append(f'{d.name}: no results files (unfinished point)')
            continue
        try:
            docs = run_tapp.load_all(files)
            summary = run_tapp.aggregate(docs, files)
        except run_tapp.TappError as error:
            raise SweepError(f'{d.name}: {error}') from error
        warnings += [f'{d.name}: {w}' for w in summary['warnings']]
        rows.append(_row(d.name, 'T_app', docs[0], summary, summary['processes'],
                         summary['median_run']))
    if any(row['dirty'] for row in rows):
        warnings.append('some results come from a binary built with uncommitted changes '
                        '(build.dirty); rebuild from a clean commit for reported numbers')
    for row in rows:
        # run_tapp's own summary already warns about T_app spread.
        if row['quantity'] != 'T_app' and row['spread'] > SPREAD_WARNING:
            warnings.append(f"{row['point']} {row['quantity']}: spread {row['spread']:.1%} is "
                            f'above {SPREAD_WARNING:.0%}; rerun and look into it')
    with open(out / 'sweep.csv', 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        writer.writerows(rows)
    return rows, warnings


# ---- command line -----------------------------------------------------------------------------

def select(points, backends):
    return [p for p in points if p.backend in backends]


def cmd_plan(args):
    points = select(preset_points(args.preset, args.machine), args.backends)
    for p in points:
        extra = render_args(p, args.scenes, args.runs if p.mode == 'prepared' else None)
        mode = (['--mode', 'prepared'] if p.mode == 'prepared' else
                [f'(T_app: {args.processes} processes) --mode app'])
        print(f'{p.name}: rt_render {" ".join(extra + mode)}')
    print(f'{len(points)} points', file=sys.stderr)
    return 0


def cmd_run(args):
    points = select(preset_points(args.preset, args.machine), args.backends)
    for p in points:
        scene_path(args.scenes, p.scene)  # fail before spending node time
    if not Path(args.rt_render).is_file():
        raise SweepError(f'rt_render {args.rt_render} not found')
    if not args.no_pin_check:
        check_pinning()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    check_openmp(record_identity(out, args.rt_render))
    (out / 'plan.json').write_text(json.dumps(
        {'preset': args.preset, 'machine': args.machine, 'runs': args.runs,
         'processes': args.processes, 'scenes': str(args.scenes),
         'rt_render': str(args.rt_render), 'points': [asdict(p) | {'name': p.name}
                                                      for p in points]}, indent=2) + '\n')
    log = open(out / 'log.txt', 'a')
    failed = []
    for i, p in enumerate(points, 1):
        prefix = f'[{i}/{len(points)}] {p.name}'
        if not args.fresh and point_done(out, p, args.processes):
            print(f'{prefix}: done earlier, skipped', file=sys.stderr)
            continue
        print(f'{prefix}: measuring', file=sys.stderr)
        try:
            seconds = measure(p, args, out, log)
            print(f'{prefix}: {seconds:.1f} s', file=sys.stderr)
            print(f'{p.name}\t{seconds:.1f} s', file=log, flush=True)
        except (SweepError, rs.ResultsError) as error:
            failed.append(p.name)
            print(f'{prefix}: FAILED: {error}', file=sys.stderr)
            print(f'{p.name}\tFAILED\t{error}', file=log, flush=True)
            if not args.keep_going:
                break
    log.close()
    rows, warnings = merge(out)
    for w in warnings:
        print(f'warning: {w}', file=sys.stderr)
    print(f'{len(rows)} rows in {out / "sweep.csv"}', file=sys.stderr)
    if failed:
        print(f'{len(failed)} point(s) failed: {", ".join(failed)}', file=sys.stderr)
        return 1
    return 0


def cmd_merge(args):
    rows, warnings = merge(Path(args.out))
    for w in warnings:
        print(f'warning: {w}', file=sys.stderr)
    print(f'{len(rows)} rows in {Path(args.out) / "sweep.csv"}', file=sys.stderr)
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('plan', 'run'):
        p = sub.add_parser(name)
        p.add_argument('--preset', required=True, choices=PRESETS)
        p.add_argument('--machine', required=True, choices=sorted(THREADS),
                       help='selects the thread counts of BENCHMARKING.md "Sweeps"')
        p.add_argument('--scenes', required=True, help='directory with the scene JSON files')
        p.add_argument('--backends', default='cpu,cuda', type=lambda s: s.split(','),
                       help='comma-separated backends to keep (default cpu,cuda)')
        p.add_argument('--runs', type=int, default=5, help='T_br and T_frame runs per point')
        p.add_argument('--processes', type=int, default=run_tapp.DEFAULT_PROCESSES,
                       help='fresh processes per T_app point')
    run = sub.choices['run']
    run.add_argument('--rt-render', required=True, help='rt_render binary')
    run.add_argument('--out', required=True, help='output directory')
    run.add_argument('--images', action='store_true',
                     help='also write each point\'s PFM and AOVs (large at 1080p)')
    run.add_argument('--fresh', action='store_true', help='measure points that are done again')
    run.add_argument('--keep-going', action='store_true', help='continue after a failed point')
    run.add_argument('--no-pin-check', action='store_true',
                     help='skip the OMP_PROC_BIND / OMP_PLACES check (tests only)')
    merge_p = sub.add_parser('merge')
    merge_p.add_argument('--out', required=True, help='output directory of a run')
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        return {'plan': cmd_plan, 'run': cmd_run, 'merge': cmd_merge}[args.command](args)
    except (SweepError, rs.ResultsError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
