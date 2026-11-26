#!/usr/bin/env python3
"""Run the benchmark sweeps of docs/BENCHMARKING.md "Sweeps" and merge them into one CSV.

A sweep is a list of points. Each point is one rt_render configuration (backend, scene, BVH,
threads, resolution) measured either in prepared mode (T_br and T_frame, one process) or as
T_app (fresh processes through scripts/bench/run_tapp.py). Presets vary one dimension at a time
around the baseline: bench_1m, 1920x1080, lbvh, mega, explicit, B = R = max.

    sweep.py plan  --preset core --machine vista --scenes DIR
    sweep.py run   --preset core --machine vista --scenes DIR --rt-render BIN --out DIR
    sweep.py merge --out DIR
    sweep.py final --sweep DIR/sweep.csv --machine vista --scenes DIR --rt-render BIN --out DIR2

`final` is the report's measurement (ROADMAP C14): it picks the 1-core baseline, CPU*, GPU* and
the fixed top-thread configurations from a sweep, reruns them in interleaved rounds, and writes
DIR2/final.csv; `--dry-run` prints the selection and a time estimate without measuring.

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
LIGHT_COUNTS = [1, 2, 4]  # BENCHMARKING "Sweeps": light count L


class SweepError(Exception):
    """A point or results file that cannot be measured or merged."""


@dataclass(frozen=True)
class Point:
    """One rt_render configuration. `mode` is `prepared` (T_br, T_frame) or `app` (T_app).

    `lights` other than 1 runs `rt_render --lights N` (N copies of the scene's light) and adds
    `_L<N>` to the name; one-light points keep their names, so earlier sweeps still resume.
    """
    backend: str
    scene: str
    bvh: str
    build_threads: int
    render_threads: int
    width: int
    height: int
    mode: str = 'prepared'
    lights: int = 1

    @property
    def name(self):
        threads = (f'B{self.build_threads}' if self.backend == 'cuda' else
                   f'B{self.build_threads}R{self.render_threads}')
        lights = f'_L{self.lights}' if self.lights != 1 else ''
        return (f'{self.mode}_{self.backend}_{self.scene}_{self.bvh}_{threads}_'
                f'{self.width}x{self.height}{lights}')


def cpu(scene, threads, bvh='lbvh', res=FULL_HD, mode='prepared', lights=1):
    return Point('cpu', scene, bvh, threads, threads, res[0], res[1], mode, lights)


def gpu(scene, build_threads, bvh='lbvh', res=FULL_HD, mode='prepared', lights=1):
    # The GPU renders; R is not used (BENCHMARKING "Threads") and is recorded as 1.
    return Point('cuda', scene, bvh, build_threads, 1, res[0], res[1], mode, lights)


def preset_points(preset, machine):
    """The points of a named preset, in run order, without duplicates."""
    threads = THREADS[machine]
    top = threads[-1]
    gpu_top = GPU_BUILD_THREADS[machine][-1]
    presets = {
        # B = R together on the CPU; T = 1 is the 1-core baseline. Every size, so CPU* comes
        # from a real thread sweep at each size, not only from 1 and the top count (#70).
        'cpu-threads': lambda: [cpu(s, t) for s in SIZES for t in threads],
        # B alone for GPU runs, at every size for the same reason.
        'gpu-build-threads': lambda: [gpu(s, b) for s in SIZES
                                      for b in GPU_BUILD_THREADS[machine]],
        # Scene size, with the 1-core baseline at each size for the target ratios.
        'size': lambda: [p for s in SIZES for p in (cpu(s, 1), cpu(s, top), gpu(s, gpu_top))],
        'bvh': lambda: [p for m in ('median', 'lbvh')
                        for p in (cpu(BASELINE_SCENE, top, m), gpu(BASELINE_SCENE, gpu_top, m))],
        'resolution': lambda: [p for r in (VGA, FULL_HD)
                               for p in (cpu(BASELINE_SCENE, top, res=r),
                                         gpu(BASELINE_SCENE, gpu_top, res=r))],
        # Light count L at the baseline; L = 1 is the same point as in `size`.
        'lights': lambda: [p for n in LIGHT_COUNTS
                           for p in (cpu(BASELINE_SCENE, top, lights=n),
                                     gpu(BASELINE_SCENE, gpu_top, lights=n))],
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
                               presets['lights']() + presets['tapp']())
    if preset not in presets:
        raise SweepError(f'unknown preset {preset!r}; choose from {", ".join(sorted(presets))}')
    seen, points = set(), []
    for p in presets[preset]():
        if p.name not in seen:
            seen.add(p.name)
            points.append(p)
    return points


PRESETS = ('core', 'g2', 'cpu-threads', 'gpu-build-threads', 'size', 'bvh', 'resolution',
           'lights', 'tapp', 'smoke')


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
    if point.lights != 1:
        args += ['--lights', str(point.lights)]
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


def point_done(out, point, processes, d=None):
    """True if `point` already has valid results of the expected shape (in `d`, by default its
    sweep directory)."""
    d = d or point_dir(out, point)
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


def measure(point, args, out, log, d=None, processes=None):
    """Measures one point into `d` (by default its sweep directory) with `processes` T_app
    processes (by default --processes); returns the elapsed seconds."""
    d = d or point_dir(out, point)
    processes = processes or args.processes
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
            paths = run_tapp.run_processes(args.rt_render, d, processes, extra, log=log)
            run_tapp.aggregate(run_tapp.load_all(paths), paths)
        except run_tapp.TappError as error:
            raise SweepError(f'{point.name}: {error}') from error
    return time.monotonic() - begin


# ---- merging ----------------------------------------------------------------------------------

COMPONENT_COLUMNS = ('args', 'load', 'cuda_init', 'bounds', 'build', 'layout', 'convert',
                     'alloc', 'upload', 'render', 'download')
CSV_COLUMNS = (['point', 'quantity', 'backend', 'pipeline', 'scene', 'scene_hash',
                'num_primitives', 'num_lights', 'bvh', 'build_threads', 'render_threads', 'width', 'height',
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
        'num_lights': doc['scene']['num_lights'],
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


# ---- final runs (ROADMAP C14) -----------------------------------------------------------------
#
# BENCHMARKING "Targets": pick CPU* and GPU* from a sweep, then rerun the 1-core baseline, CPU*,
# GPU* and the fixed top-thread configurations in one job, interleaved, and report the headline
# ratios from those reruns (the minimum of noisy sweep medians is biased low).
#
# A round measures every selected configuration once, in a rotated order, so drift during the
# job (clocks, other jobs on the node) reaches every configuration alike. Per round a prepared
# configuration is one rt_render process (1 warm-up and --runs measured runs, as in a sweep) and
# a T_app configuration is one fresh process. The reported value is the median over the rounds:
# of the per-process medians for T_br and T_frame, of the process totals for T_app. Spread is
# over the rounds. Components come from the median run of the median round.

ROLES = ('one_core', 'cpu_star', 'gpu_star', 'cpu_fixed', 'gpu_fixed')
REQUIRED_ROLES = ('one_core', 'cpu_star', 'gpu_star')
QUANTITY_MODE = {'T_br': 'prepared', 'T_frame': 'prepared', 'T_app': 'app'}
DEFAULT_ROUNDS = 5
FINAL_COLUMNS = CSV_COLUMNS + ['rounds', 'median_round', 'inner_spread_max', 'roles']


def load_sweep_csv(path):
    try:
        with open(path, newline='') as f:
            rows = list(csv.DictReader(f))
    except OSError as error:
        raise SweepError(f'cannot read {path}: {error.strerror}') from error
    if not rows:
        raise SweepError(f'{path}: no rows')
    return rows


def final_selection(rows, machine, bvh='lbvh', width=1920, scenes=None,
                    backends=('cpu', 'cuda')):
    """The configurations of the final runs, chosen from sweep rows.

    Returns a list of {scene, quantity, role, point, sweep_median_ms, tested}, one per scene,
    quantity and role (ROLES). CPU* and GPU* are the lowest sweep medians among the tested thread
    counts (BENCHMARKING "Targets"); the fixed roles use the machine's top thread counts and are
    left out if the sweep did not measure them. Only one-light rows count. Without `cuda` in
    `backends` the GPU roles are dropped (a CPU-only machine).
    """
    top, gpu_top = THREADS[machine][-1], GPU_BUILD_THREADS[machine][-1]
    rows = [r for r in rows if r['bvh'] == bvh and int(r['width']) == width
            and int(r.get('num_lights') or 1) == 1]
    size = {}
    for r in rows:
        size[r['scene']] = int(r['num_primitives'])
    found = sorted(size, key=size.get)
    if scenes:
        missing = [s for s in scenes if s not in size]
        if missing:
            raise SweepError(f'the sweep has no {bvh} {width}-wide rows for {", ".join(missing)}')
        found = [s for s in found if s in scenes]
    selection, problems = [], []
    for s in found:
        for quantity, mode in QUANTITY_MODE.items():
            cpu = [r for r in rows if r['scene'] == s and r['quantity'] == quantity
                   and r['backend'] == 'cpu']
            gpu = [r for r in rows if r['scene'] == s and r['quantity'] == quantity
                   and r['backend'] == 'cuda']
            if not cpu and not gpu:
                continue  # e.g. a sweep without T_app points

            def at(points, threads):
                return next((r for r in points if int(r['build_threads']) == threads), None)

            def best(points):
                return min(points, key=lambda r: float(r['median_ms'])) if points else None

            chosen = {'one_core': at(cpu, 1), 'cpu_star': best(cpu), 'gpu_star': best(gpu),
                      'cpu_fixed': at(cpu, top), 'gpu_fixed': at(gpu, gpu_top)}
            if 'cuda' not in backends:
                chosen.update(gpu_star=None, gpu_fixed=None)
            required = [role for role in REQUIRED_ROLES
                        if 'cuda' in backends or role != 'gpu_star']
            problems += [f'{s} {quantity}: no {role} row' for role in required
                         if chosen[role] is None]
            for role in ROLES:
                r = chosen[role]
                if r is None:
                    continue
                selection.append({
                    'scene': s, 'quantity': quantity, 'role': role,
                    'point': Point(r['backend'], s, bvh, int(r['build_threads']),
                                   int(r['render_threads']), int(r['width']),
                                   int(r['height']), mode),
                    'sweep_median_ms': float(r['median_ms']),
                    'tested': len(gpu if r['backend'] == 'cuda' else cpu),
                    'commit': r.get('commit', ''),
                })
    if problems:
        raise SweepError('the sweep cannot give the final configurations: ' + '; '.join(problems))
    if not selection:
        raise SweepError(f'the sweep has no {bvh} rows {width} px wide with one light')
    return selection


def final_points(selection):
    """The distinct configurations of a selection, in selection order."""
    seen, points = set(), []
    for sel in selection:
        if sel['point'].name not in seen:
            seen.add(sel['point'].name)
            points.append(sel['point'])
    return points


def round_order(points, r):
    """Round r measures the configurations rotated by r places."""
    k = r % len(points)
    return points[k:] + points[:k]


def round_dir(out, point, r):
    return point_dir(out, point) / f'round_{r}'


def final_estimate_s(selection, runs, rounds):
    """Rough wall time of the final runs from the sweep medians, in seconds."""
    medians = {}
    for sel in selection:
        medians[(sel['point'].name, sel['quantity'])] = sel['sweep_median_ms']
    total_ms = 0.0
    for p in final_points(selection):
        if p.mode == 'app':
            total_ms += medians.get((p.name, 'T_app'), 0.0)
        else:  # 1 warm-up and `runs` runs of each quantity; T_frame may be missing for this point
            per_run = medians.get((p.name, 'T_br'), 0.0) + medians.get((p.name, 'T_frame'), 0.0)
            total_ms += (runs + 1) * per_run
    return rounds * total_ms / 1000.0


def merge_final(out):
    """Combines the rounds under out/points into out/final.csv; returns (rows, warnings).

    One row per configuration and quantity, with the statistics over the rounds and the roles
    (ROLES) the configuration plays for that quantity.
    """
    plan = json.loads((out / 'final_plan.json').read_text())
    roles = {}
    for sel in plan['selection']:
        roles.setdefault((sel['point'], sel['quantity']), []).append(sel['role'])
    rows, warnings = [], []
    for p in plan['points']:
        name = p['name']
        point = Point(**{k: v for k, v in p.items() if k != 'name'})
        done = [r for r in range(plan['rounds'])
                if point_done(out, point, 1, round_dir(out, point, r))]
        if len(done) < plan['rounds']:
            warnings.append(f'{name}: {len(done)} of {plan["rounds"]} rounds finished')
        if not done:
            continue
        if point.mode == 'prepared':
            files = [str(round_dir(out, point, r) / 'prepared.json') for r in done]
        else:
            files = [str(round_dir(out, point, r) / 'tapp_0.json') for r in done]
        docs = [rs.load_results(f) for f in files]
        try:
            run_tapp.check_same_run(docs, files)
        except run_tapp.TappError as error:
            raise SweepError(f'{name}: {error}') from error
        sections = ((('T_br', 'build_and_render'), ('T_frame', 'frame'))
                    if point.mode == 'prepared' else (('T_app', 'app'),))
        for quantity, section in sections:
            if point.mode == 'prepared':
                values = [doc[section]['median_ms'] for doc in docs]
            else:
                values = [doc['app']['total_ms'] for doc in docs]
            stats = rs.summarize(values)
            k = stats['median_run_index']
            doc = docs[k]
            if point.mode == 'prepared':
                s = doc[section]
                row = _row(name, quantity, doc, stats, len(s['runs']),
                           s['runs'][s['median_run_index']])
                inner = max(d[section]['spread'] for d in docs)
            else:
                row = _row(name, quantity, doc, stats, 1, doc['app'])
                inner = ''
            row.update(rounds=len(docs), median_round=done[k], inner_spread_max=inner,
                       roles=' '.join(roles.get((name, quantity), [])))
            rows.append(row)
            if stats['spread'] > SPREAD_WARNING:
                warnings.append(f'{name} {quantity}: spread over rounds {stats["spread"]:.1%} is '
                                f'above {SPREAD_WARNING:.0%}')
            if inner != '' and inner > SPREAD_WARNING:
                warnings.append(f'{name} {quantity}: spread inside a round up to {inner:.1%}')
    commits = {row['commit'] for row in rows}
    sweep_commits = {c for c in plan.get('sweep_commits', []) if c}
    if len(commits) > 1:
        warnings.append(f'the final runs come from several commits: {", ".join(sorted(commits))}')
    if sweep_commits and commits and sweep_commits != commits:
        warnings.append('the sweep the configurations were chosen from was measured at '
                        f'{", ".join(sorted(sweep_commits))}, the final runs at '
                        f'{", ".join(sorted(commits))}; the choice of CPU* and GPU* may be stale')
    if any(row['dirty'] for row in rows):
        warnings.append('some results come from a binary built with uncommitted changes '
                        '(build.dirty); rebuild from a clean commit for reported numbers')
    with open(out / 'final.csv', 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=FINAL_COLUMNS)
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


def print_selection(selection, runs, rounds):
    for sel in selection:
        print(f"{sel['scene']} {sel['quantity']} {sel['role']}: {sel['point'].name} "
              f"(sweep median {sel['sweep_median_ms']:.1f} ms, {sel['tested']} tested)")
    points = final_points(selection)
    print(f'{len(points)} configurations x {rounds} rounds, about '
          f'{final_estimate_s(selection, runs, rounds) / 60:.0f} min of measuring',
          file=sys.stderr)


def cmd_final(args):
    selection = final_selection(load_sweep_csv(args.sweep), args.machine, args.bvh, args.width,
                                args.sizes, args.backends)
    points = final_points(selection)
    for p in points:
        scene_path(args.scenes, p.scene)  # fail before spending node time
    if args.dry_run:
        print_selection(selection, args.runs, args.rounds)
        for p in points:
            extra = render_args(p, args.scenes, args.runs if p.mode == 'prepared' else None)
            print(f'{p.name}: rt_render {" ".join(extra)} --mode {p.mode}')
        return 0
    if not args.rt_render or not Path(args.rt_render).is_file():
        raise SweepError(f'rt_render {args.rt_render} not found')
    if not args.no_pin_check:
        check_pinning()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    check_openmp(record_identity(out, args.rt_render))
    plan_path = out / 'final_plan.json'
    plan = {'sweep': str(Path(args.sweep).resolve()), 'machine': args.machine, 'bvh': args.bvh,
            'width': args.width, 'runs': args.runs, 'rounds': args.rounds,
            'scenes': str(args.scenes), 'rt_render': str(args.rt_render),
            'sweep_commits': sorted({sel['commit'] for sel in selection}),
            'points': [asdict(p) | {'name': p.name} for p in points],
            'selection': [{k: (v.name if k == 'point' else v) for k, v in sel.items()}
                          for sel in selection]}
    if plan_path.is_file() and not args.fresh:
        old = json.loads(plan_path.read_text())
        if (old.get('points'), old.get('runs')) != (plan['points'], plan['runs']):
            raise SweepError(f'{out} holds final runs of other configurations; use another '
                             '--out, or --fresh to measure everything again')
    plan_path.write_text(json.dumps(plan, indent=2) + '\n')
    print_selection(selection, args.runs, args.rounds)
    log = open(out / 'log.txt', 'a')
    failed = []
    total = len(points) * args.rounds
    i = 0
    for r in range(args.rounds):
        for p in round_order(points, r):
            i += 1
            d = round_dir(out, p, r)
            prefix = f'[{i}/{total}] round {r} {p.name}'
            if not args.fresh and point_done(out, p, 1, d):
                print(f'{prefix}: done earlier, skipped', file=sys.stderr)
                continue
            print(f'{prefix}: measuring', file=sys.stderr)
            try:
                seconds = measure(p, args, out, log, d=d, processes=1)
                print(f'{prefix}: {seconds:.1f} s', file=sys.stderr)
                print(f'round {r} {p.name}\t{seconds:.1f} s', file=log, flush=True)
            except (SweepError, rs.ResultsError) as error:
                failed.append(f'round {r} {p.name}')
                print(f'{prefix}: FAILED: {error}', file=sys.stderr)
                print(f'round {r} {p.name}\tFAILED\t{error}', file=log, flush=True)
                if not args.keep_going:
                    break
        if failed and not args.keep_going:
            break
    log.close()
    rows, warnings = merge_final(out)
    for w in warnings:
        print(f'warning: {w}', file=sys.stderr)
    print(f'{len(rows)} rows in {out / "final.csv"}; tables: python3 scripts/tacc/ratios.py '
          f'{out / "final.csv"}', file=sys.stderr)
    if failed:
        print(f'{len(failed)} measurement(s) failed: {", ".join(failed)}', file=sys.stderr)
        return 1
    return 0


def cmd_merge(args):
    out = Path(args.out)
    if (out / 'final_plan.json').is_file():
        rows, warnings = merge_final(out)
        for w in warnings:
            print(f'warning: {w}', file=sys.stderr)
        print(f'{len(rows)} rows in {out / "final.csv"}', file=sys.stderr)
        return 0
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
    merge_p.add_argument('--out', required=True, help='output directory of a run (sweep or final)')
    final = sub.add_parser('final', help='interleaved reruns of the configurations a sweep '
                           'selects (ROADMAP C14)')
    final.add_argument('--sweep', required=True, help='sweep.csv to choose CPU* and GPU* from')
    final.add_argument('--machine', required=True, choices=sorted(THREADS),
                       help='gives the fixed top thread counts (72 on Vista)')
    final.add_argument('--scenes', required=True, help='directory with the scene JSON files')
    final.add_argument('--rt-render', help='rt_render binary (not needed with --dry-run)')
    final.add_argument('--out', help='output directory (not needed with --dry-run)')
    final.add_argument('--bvh', default='lbvh')
    final.add_argument('--width', type=int, default=FULL_HD[0])
    final.add_argument('--sizes', type=lambda s: s.split(','),
                       help='comma-separated scenes to keep (default: every scene in the sweep)')
    final.add_argument('--backends', default='cpu,cuda', type=lambda s: s.split(','),
                       help='comma-separated backends to rerun (default cpu,cuda)')
    final.add_argument('--rounds', type=int, default=DEFAULT_ROUNDS,
                       help=f'interleaved rounds (default {DEFAULT_ROUNDS})')
    final.add_argument('--runs', type=int, default=5,
                       help='T_br and T_frame runs per process (default 5)')
    final.add_argument('--dry-run', action='store_true',
                       help='print the selection, the commands and a time estimate, then stop')
    final.add_argument('--fresh', action='store_true', help='measure finished rounds again')
    final.add_argument('--keep-going', action='store_true', help='continue after a failure')
    final.add_argument('--images', action='store_true', help=argparse.SUPPRESS)
    final.add_argument('--no-pin-check', action='store_true',
                       help='skip the OMP_PROC_BIND / OMP_PLACES check (tests only)')
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command == 'final':
        if args.rounds < 1 or args.runs < 1:
            parser.error('--rounds and --runs must be at least 1')
        if not args.dry_run and not (args.rt_render and args.out):
            parser.error('final needs --rt-render and --out (or --dry-run)')
    try:
        return {'plan': cmd_plan, 'run': cmd_run, 'merge': cmd_merge,
                'final': cmd_final}[args.command](args)
    except (SweepError, rs.ResultsError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
