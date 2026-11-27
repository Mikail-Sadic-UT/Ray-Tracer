#!/usr/bin/env python3
"""Extra benchmarks outside the Core sweep: CPU against GPU on a few other scenes, at the
machine's top thread count only (B = R = 72 on Vista), plus a camera-path walkthrough video.

    extra.py plan --machine vista [--scenes DIRS]
    extra.py run  --machine vista [--scenes DIRS] --rt-render BIN --out DIR [--frames 240]
                  [--only NAME,...] [--backends cpu,cuda] [--no-video] [--keep-frames] [--fresh]

Stills (T_br and T_frame, 1 warm-up and --runs runs, as in a sweep):
    terrain_10k  the bench_10k terrain without spheres (rt_gen --layout terrain), 1920x1080
    map_crew     the Among Us map with crew models, 3840x2160
    dragon_green the XYZ RGB dragon in green, seen from the side, lit by 3 lights, 3840x2160
    showcase     chrome, gold mirror, glass and glossy red spheres on a checkerboard, 3 lights
                 (make_showcase.py), 3840x2160
Walkthrough (rt_render --path, one preparation, then one timed frame per camera):
    house        house.json along house_walk.json, 1920x1080 and 3840x2160

DIRS is an os.pathsep-separated list searched in order for <name>.json; the default is
scenes/generated, scenes/meshes and scenes/extra. terrain_10k is the published scene set
scenes-extra-v1 (`bash scripts/fetch_scenes.sh scenes-extra-v1`, which checks its hashes); the mesh
scenes are local (scenes/meshes is not in git; see USER_GUIDE "Meshes") and have to be copied to
Vista. A scene that is not found fails the run before anything is measured, unless it is left
out with --only.

`run` writes DIR/sweep.csv (the stills, in the sweep format), DIR/walks.csv, and DIR/extra.md,
the CPU-against-GPU tables. Every still is also checked for CPU and GPU byte equality (PFM and
AOVs), and every walkthrough frame for PNG byte equality. With ffmpeg, the GPU frames are
encoded into DIR/videos/<scene>_<W>x<H>.mp4 (as scripts/make_video.sh does); the frames are
then deleted unless --keep-frames, and kept for later encoding when ffmpeg is missing or
cannot run (a compute node can lack a library the login node has) or fails: that costs the
video only, never the measurements.
Finished stills and walks are skipped on resubmission (--fresh measures them again).

Exit status: 0 on success, 1 if a measurement fails or CPU and GPU output differ, 2 for a usage
error.
"""
import argparse
import csv
from dataclasses import dataclass
import filecmp
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

REPO = Path(__file__).resolve().parent.parent.parent
DEFAULT_SCENES = os.pathsep.join(str(REPO / 'scenes' / d) for d in ('generated', 'meshes', 'extra'))
UHD = (3840, 2160)
FPS = 30


@dataclass(frozen=True)
class Still:
    scene: str
    res: tuple


@dataclass(frozen=True)
class Walk:
    scene: str
    path: str  # camera path JSON, looked up like a scene
    resolutions: tuple


STILLS = (Still('terrain_10k', sweep.FULL_HD), Still('map_crew', UHD),
          Still('dragon_green', UHD), Still('showcase', UHD))
WALKS = (Walk('house', 'house_walk', (sweep.FULL_HD, UHD)),)
NAMES = tuple(s.scene for s in STILLS) + tuple(w.scene for w in WALKS)


def still_points(machine, backends, only):
    top = sweep.THREADS[machine][-1]
    gpu_top = sweep.GPU_BUILD_THREADS[machine][-1]
    points = []
    for s in STILLS:
        if only and s.scene not in only:
            continue
        if 'cpu' in backends:
            points.append(sweep.cpu(s.scene, top, res=s.res))
        if 'cuda' in backends:
            points.append(sweep.gpu(s.scene, gpu_top, res=s.res))
    return points


def walk_runs(machine, backends, only):
    """(walk, resolution, backend, build threads, render threads) in run order."""
    top = sweep.THREADS[machine][-1]
    gpu_top = sweep.GPU_BUILD_THREADS[machine][-1]
    runs = []
    for w in WALKS:
        if only and w.scene not in only:
            continue
        for res in w.resolutions:
            # CUDA first: its frames become the video, so they are written before the CPU's.
            if 'cuda' in backends:
                runs.append((w, res, 'cuda', gpu_top, 1))
            if 'cpu' in backends:
                runs.append((w, res, 'cpu', top, top))
    return runs


def walk_name(w, res, backend, build_threads, render_threads):
    threads = f'B{build_threads}' if backend == 'cuda' else f'B{build_threads}R{render_threads}'
    return f'walk_{backend}_{w.scene}_lbvh_{threads}_{res[0]}x{res[1]}'


def walk_command(rt_render, scenes, w, res, backend, b, r, frames, frames_dir):
    command = [str(rt_render), '--scene', str(sweep.scene_path(scenes, w.scene)),
               '--backend', backend, '--bvh', 'lbvh', '--build-threads', str(b),
               '--render-threads', str(r), '--width', str(res[0]), '--height', str(res[1]),
               '--shade', 'lit', '--path', str(sweep.scene_path(scenes, w.path)),
               '--path-frames', str(frames), '--frames', str(frames_dir)]
    if backend == 'cuda':
        command[command.index('--shade'):command.index('--shade')] = ['--pipeline', 'mega']
    return command


PREP_RE = re.compile(r'(\w+) ([\d.e+-]+) ms')
FRAME_RE = re.compile(r'T_frame over the path: median ([\d.e+-]+) ms \(min ([\d.e+-]+), max '
                      r'([\d.e+-]+), spread ([\d.e+-]+), (\d+) frames, total ([\d.e+-]+) ms\)')


def parse_walk(stderr):
    """Preparation components and the frame statistics from rt_render --path output."""
    prep, frame = {}, None
    for line in stderr.splitlines():
        line = line.strip()
        if line.startswith('preparation:'):
            prep = {k: float(v) for k, v in PREP_RE.findall(line)}
        m = FRAME_RE.search(line)
        if m:
            frame = {'median_ms': float(m[1]), 'min_ms': float(m[2]), 'max_ms': float(m[3]),
                     'spread': float(m[4]), 'frames': int(m[5]), 'total_ms': float(m[6])}
    if frame is None:
        raise sweep.SweepError('rt_render printed no "T_frame over the path" line')
    return prep, frame


def build_info(rt_render):
    """Commit and dirty flag of the repo the script runs from (path mode writes no results
    file, so the binary's own build record is not available)."""
    commit = subprocess.run(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], capture_output=True,
                            text=True).stdout.strip()
    dirty = subprocess.run(['git', '-C', str(REPO), 'diff', '--quiet', 'HEAD']).returncode != 0
    return commit, dirty


def measure_walk(args, out, run, log):
    w, res, backend, b, r = run
    name = walk_name(*run)
    d = out / 'walks' / name
    if d.exists():
        shutil.rmtree(d)
    frames_dir = d / 'frames'
    d.mkdir(parents=True)
    command = walk_command(args.rt_render, args.scenes, w, res, backend, b, r, args.frames,
                           frames_dir)
    (d / 'command.txt').write_text(' '.join(command) + '\n')
    begin = time.monotonic()
    p = subprocess.run(command, capture_output=True, text=True)
    wall = time.monotonic() - begin
    (d / 'stderr.txt').write_text(p.stderr)
    if p.returncode != 0:
        tail = ' | '.join(p.stderr.strip().splitlines()[-3:])
        raise sweep.SweepError(f'{name}: rt_render exited {p.returncode}: {tail}')
    prep, frame = parse_walk(p.stderr)
    commit, dirty = build_info(args.rt_render)
    doc = {'name': name, 'scene': w.scene, 'path': w.path, 'backend': backend,
           'build_threads': b, 'render_threads': r, 'width': res[0], 'height': res[1],
           'preparation_ms': prep, 'frame': frame, 'wall_s': wall, 'commit': commit,
           'dirty': dirty, 'host': os.uname().nodename}
    (d / 'walk.json').write_text(json.dumps(doc, indent=2) + '\n')
    print(f'{name}\t{wall:.1f} s', file=log, flush=True)
    return doc


def compare_frames(out, cuda_name, cpu_name):
    """(identical, total) frame PNGs between the two walks; None if either has no frames."""
    a, b = out / 'walks' / cuda_name / 'frames', out / 'walks' / cpu_name / 'frames'
    if not (a.is_dir() and b.is_dir()):
        return None
    names = sorted(p.name for p in a.glob('frame_*.png'))
    if names != sorted(p.name for p in b.glob('frame_*.png')):
        return (0, max(len(names), 1))
    same = sum(filecmp.cmp(a / n, b / n, shallow=False) for n in names)
    return (same, len(names))


def ffmpeg_works():
    """True if `ffmpeg` is on PATH and starts: /usr/bin/ffmpeg on a Vista compute node can fail
    to load a shared library (libunwind.so.8), which `shutil.which` does not notice."""
    exe = shutil.which('ffmpeg')
    if exe is None:
        return False
    try:
        return subprocess.run([exe, '-version'], capture_output=True).returncode == 0
    except OSError:
        return False


def encode(frames_dir, video):
    video.parent.mkdir(parents=True, exist_ok=True)
    # The settings of scripts/make_video.sh.
    p = subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-framerate', str(FPS), '-i',
                        str(frames_dir / 'frame_%04d.png'), '-vf',
                        'pad=ceil(iw/2)*2:ceil(ih/2)*2', '-c:v', 'libx264', '-pix_fmt',
                        'yuv420p', '-crf', '18', str(video)], capture_output=True, text=True)
    if p.returncode != 0:
        raise sweep.SweepError(f'ffmpeg failed for {video}: {p.stderr.strip()[-200:]}')


def still_images_equal(out, cpu_point, gpu_point):
    """True if the CPU and GPU PFM and AOV files are byte-identical; None if either is missing."""
    a, b = sweep.point_dir(out, cpu_point), sweep.point_dir(out, gpu_point)
    files = ('image.pfm', 'image.primid.u32img', 'image.lightmask.u32img')
    if not all((a / f).is_file() and (b / f).is_file() for f in files):
        return None
    return all(filecmp.cmp(a / f, b / f, shallow=False) for f in files)


# ---- report ---------------------------------------------------------------------------------

def report(out, still_rows, walks, equal, frame_checks):
    lines = ['## Extra benchmarks: CPU against GPU', '']
    if still_rows:
        host = still_rows[0]['host']
        lines += [f'Stills on {host}: T_br and T_frame, median of {still_rows[0]["runs"]} runs, '
                  'lbvh, lit. CPU at B = R = top threads, GPU at B = top.', '',
                  '| Scene | Primitives | Resolution | CPU T_br | GPU T_br | CPU / GPU | '
                  'CPU T_frame | GPU T_frame | CPU / GPU | Spread (max) | CPU = GPU |',
                  '|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---|']
        by = {}
        for r in still_rows:
            by.setdefault(r['scene'], {})[(r['backend'], r['quantity'])] = r
        for s in STILLS:
            rows = by.get(s.scene)
            if not rows:
                continue

            def ms(backend, q):
                r = rows.get((backend, q))
                return f"{float(r['median_ms']):.1f}" if r else '—'

            def ratio(q):
                c, g = rows.get(('cpu', q)), rows.get(('cuda', q))
                return f"{float(c['median_ms']) / float(g['median_ms']):.1f}x" if c and g else '—'

            any_row = next(iter(rows.values()))
            spread = max(float(r['spread']) for r in rows.values())
            same = equal.get(s.scene)
            lines.append(
                f"| {s.scene} | {int(any_row['num_primitives']):,} | {any_row['width']}x"
                f"{any_row['height']} | {ms('cpu', 'T_br')} | {ms('cuda', 'T_br')} | "
                f"{ratio('T_br')} | {ms('cpu', 'T_frame')} | {ms('cuda', 'T_frame')} | "
                f"{ratio('T_frame')} | {spread:.0%} | "
                f"{'—' if same is None else 'yes' if same else '**no**'} |")
        lines.append('')
    if walks:
        lines += ['Walkthroughs: one preparation, then one timed frame per camera (render and '
                  'download; PNG writing excluded). Frame time varies along the path by design, '
                  'so min and max describe the path, not noise.', '',
                  '| Scene | Resolution | Frames | CPU frame median (min–max) ms | GPU frame '
                  'median (min–max) ms | CPU / GPU | CPU total s | GPU total s | Frames '
                  'identical |', '|---|---|---:|---:|---:|---:|---:|---:|---|']
        by = {}
        for doc in walks:
            by.setdefault((doc['scene'], doc['width'], doc['height']), {})[doc['backend']] = doc
        for (scene, width, height), docs in by.items():
            c, g = docs.get('cpu'), docs.get('cuda')

            def fr(doc):
                f = doc['frame'] if doc else None
                return (f"{f['median_ms']:.1f} ({f['min_ms']:.1f}–{f['max_ms']:.1f})" if f
                        else '—')

            def tot(doc):
                return f"{doc['frame']['total_ms'] / 1000:.1f}" if doc else '—'

            ratio = (f"{c['frame']['total_ms'] / g['frame']['total_ms']:.1f}x" if c and g
                     else '—')
            check = frame_checks.get((scene, width, height))
            frames = (c or g)['frame']['frames']
            lines.append(f"| {scene} | {width}x{height} | {frames} | {fr(c)} | {fr(g)} | "
                         f"{ratio} | {tot(c)} | {tot(g)} | "
                         f"{'—' if check is None else f'{check[0]} of {check[1]}'} |")
        lines.append('')
    (out / 'extra.md').write_text('\n'.join(lines))
    return '\n'.join(lines)


def write_walks_csv(out, walks):
    columns = ['name', 'scene', 'backend', 'build_threads', 'render_threads', 'width', 'height',
               'frames', 'median_ms', 'min_ms', 'max_ms', 'spread', 'total_ms', 'bounds_ms',
               'build_ms', 'layout_ms', 'alloc_ms', 'upload_ms', 'wall_s', 'host', 'commit',
               'dirty']
    with open(out / 'walks.csv', 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        for d in walks:
            row = {k: d.get(k, '') for k in columns}
            row.update({k: d['frame'][k] for k in ('frames', 'median_ms', 'min_ms', 'max_ms',
                                                    'spread', 'total_ms')})
            for k in ('bounds', 'build', 'layout', 'alloc', 'upload'):
                row[f'{k}_ms'] = d['preparation_ms'].get(k, '')
            writer.writerow(row)


# ---- commands -------------------------------------------------------------------------------

def check_scenes(args):
    """Fails before measuring if a selected scene or camera path is missing."""
    names = [s.scene for s in STILLS if not args.only or s.scene in args.only]
    for w in WALKS:
        if not args.only or w.scene in args.only:
            names += [w.scene, w.path]
    missing = []
    for n in names:
        try:
            sweep.scene_path(args.scenes, n)
        except sweep.SweepError:
            missing.append(n)
    if missing:
        raise sweep.SweepError(
            f'not found in {args.scenes}: {", ".join(missing)}. terrain_10k: bash scripts/'
            'fetch_scenes.sh scenes-extra-v1 (a generated scene is fetched, never regenerated); the mesh scenes '
            'come from scenes/meshes on the laptop (copy them to Vista); or leave them out '
            'with --only')


def cmd_plan(args):
    check_scenes(args)
    for p in still_points(args.machine, args.backends, args.only):
        print(f'{p.name}: rt_render {" ".join(sweep.render_args(p, args.scenes, args.runs))} '
              '--mode prepared')
    for run in walk_runs(args.machine, args.backends, args.only):
        command = walk_command('rt_render', args.scenes, *run, args.frames, 'FRAMES')
        print(f'{walk_name(*run)}: {" ".join(command)}')
    return 0


def cmd_run(args):
    check_scenes(args)
    if not Path(args.rt_render).is_file():
        raise sweep.SweepError(f'rt_render {args.rt_render} not found')
    if not args.no_pin_check:
        sweep.check_pinning()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sweep.check_openmp(sweep.record_identity(out, args.rt_render))
    log = open(out / 'log.txt', 'a')
    failures, mismatches = [], []
    args.images = True  # sweep.measure writes the PFM and AOVs for the CPU = GPU check
    points = still_points(args.machine, args.backends, args.only)
    runs = walk_runs(args.machine, args.backends, args.only)
    total = len(points) + len(runs)
    for i, p in enumerate(points, 1):
        prefix = f'[{i}/{total}] {p.name}'
        if not args.fresh and sweep.point_done(out, p, 1):
            print(f'{prefix}: done earlier, skipped', file=sys.stderr)
            continue
        print(f'{prefix}: measuring', file=sys.stderr)
        try:
            seconds = sweep.measure(p, args, out, log)
            print(f'{prefix}: {seconds:.1f} s', file=sys.stderr)
            print(f'{p.name}\t{seconds:.1f} s', file=log, flush=True)
        except (sweep.SweepError, sweep.rs.ResultsError) as error:
            failures.append(p.name)
            print(f'{prefix}: FAILED: {error}', file=sys.stderr)
    # CPU = GPU on every still; the large image files are kept only when they differ.
    equal = {}
    # Every still of the list, not only those of this run (--only): a finished one keeps its
    # verdict, which is saved next to its results.
    for s in STILLS:
        pair = [p for p in still_points(args.machine, args.backends, None) if p.scene == s.scene]
        if len(pair) == 2:
            cpu_p, gpu_p = sorted(pair, key=lambda p: p.backend != 'cpu')
            same = still_images_equal(out, cpu_p, gpu_p)
            if same is None:  # images deleted by an earlier run: keep its verdict
                verdict = sweep.point_dir(out, cpu_p) / 'cpu_equals_gpu.txt'
                same = verdict.read_text().strip() == 'yes' if verdict.is_file() else None
            else:
                (sweep.point_dir(out, cpu_p) / 'cpu_equals_gpu.txt').write_text(
                    'yes\n' if same else 'no\n')
                if same:
                    for p in pair:
                        for f in sweep.point_dir(out, p).glob('image.*'):
                            f.unlink()
            equal[s.scene] = same
            if same is False:
                mismatches.append(f'{s.scene}: CPU and GPU images differ (kept in '
                                  f'{sweep.point_dir(out, cpu_p)})')
    walks, frame_checks = [], {}
    for i, run in enumerate(runs, len(points) + 1):
        name = walk_name(*run)
        prefix = f'[{i}/{total}] {name}'
        done = out / 'walks' / name / 'walk.json'
        if not args.fresh and done.is_file():
            print(f'{prefix}: done earlier, skipped', file=sys.stderr)
            walks.append(json.loads(done.read_text()))
            continue
        print(f'{prefix}: rendering {args.frames} frames', file=sys.stderr)
        try:
            doc = measure_walk(args, out, run, log)
            walks.append(doc)
            print(f"{prefix}: frame median {doc['frame']['median_ms']:.1f} ms, total "
                  f"{doc['frame']['total_ms'] / 1000:.1f} s", file=sys.stderr)
        except sweep.SweepError as error:
            failures.append(name)
            print(f'{prefix}: FAILED: {error}', file=sys.stderr)
            continue
        w, res, backend, _, _ = run
        if backend != 'cpu':
            continue
        # The CPU walk runs after the CUDA one at the same resolution: compare, encode, clean.
        cuda = next((x for x in runs if x[0] == w and x[1] == res and x[2] == 'cuda'), None)
        cuda_name = walk_name(*cuda) if cuda else None
        key = (w.scene, res[0], res[1])
        check = compare_frames(out, cuda_name, name) if cuda_name else None
        if check is not None:
            frame_checks[key] = check
            (out / 'walks' / name / 'frames_identical.txt').write_text(f'{check[0]} {check[1]}\n')
            if check[0] != check[1]:
                mismatches.append(f'{w.scene} {res[0]}x{res[1]}: {check[1] - check[0]} of '
                                  f'{check[1]} frames differ between CPU and GPU')
        finish_frames(args, out, w, res, cuda_name, name)
    # Walks done earlier: their frame checks were recorded next to the CPU walk.
    for doc in walks:
        f = out / 'walks' / doc['name'] / 'frames_identical.txt'
        if doc['backend'] == 'cpu' and f.is_file():
            same, n = (int(x) for x in f.read_text().split())
            frame_checks.setdefault((doc['scene'], doc['width'], doc['height']), (same, n))
    for run in runs:  # a resolution measured on one backend only still gets its video
        if not any(r[0] == run[0] and r[1] == run[1] and r[2] != run[2] for r in runs):
            finish_frames(args, out, run[0], run[1], walk_name(*run), None)
    log.close()
    rows, warnings = sweep.merge(out)
    write_walks_csv(out, walks)
    print(report(out, rows, walks, equal, frame_checks))
    for w in warnings:
        print(f'warning: {w}', file=sys.stderr)
    for m in mismatches:
        print(f'MISMATCH {m}', file=sys.stderr)
    if failures:
        print(f'{len(failures)} measurement(s) failed: {", ".join(failures)}', file=sys.stderr)
    return 1 if failures or mismatches else 0


def finish_frames(args, out, w, res, video_name, other_name):
    """Encodes the video from `video_name`'s frames, then deletes frames as asked."""
    video_frames = out / 'walks' / video_name / 'frames' if video_name else None
    have_ffmpeg = ffmpeg_works()
    video = out / 'videos' / f'{w.scene}_{res[0]}x{res[1]}.mp4'
    keep_video_frames = args.keep_frames
    if video_frames and video_frames.is_dir() and not args.no_video:
        reason = None if have_ffmpeg else 'ffmpeg is missing or cannot run'
        if have_ffmpeg:
            try:
                encode(video_frames, video)
                print(f'video: {video}', file=sys.stderr)
            except sweep.SweepError as error:
                reason = str(error)
        if reason:
            keep_video_frames = True
            print(f'warning: no video for {w.scene} {res[0]}x{res[1]}: {reason}; frames kept '
                  f'in {video_frames} for encoding later (BENCHMARKING.md "Extra benchmarks")',
                  file=sys.stderr)
    for name, keep in ((video_name, keep_video_frames), (other_name, args.keep_frames)):
        d = out / 'walks' / name / 'frames' if name else None
        if d and d.is_dir() and not keep:
            shutil.rmtree(d)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('plan', 'run'):
        p = sub.add_parser(name)
        p.add_argument('--machine', required=True, choices=sorted(sweep.THREADS),
                       help='gives the top thread counts (72 on Vista)')
        p.add_argument('--scenes', default=DEFAULT_SCENES,
                       help='os.pathsep-separated scene directories (default: scenes/generated, '
                       'scenes/meshes, scenes/extra)')
        p.add_argument('--backends', default='cpu,cuda', type=lambda s: s.split(','))
        p.add_argument('--only', type=lambda s: s.split(','),
                       help=f'comma-separated benchmarks to keep, from {", ".join(NAMES)}')
        p.add_argument('--runs', type=int, default=5, help='T_br and T_frame runs per still')
        p.add_argument('--frames', type=int, default=240,
                       help=f'walkthrough frames (default 240, {240 / FPS:.0f} s at {FPS} fps)')
    run = sub.choices['run']
    run.add_argument('--rt-render', required=True)
    run.add_argument('--out', required=True)
    run.add_argument('--no-video', action='store_true', help='do not encode walkthrough videos')
    run.add_argument('--keep-frames', action='store_true', help='keep the walkthrough PNGs')
    run.add_argument('--fresh', action='store_true', help='measure finished items again')
    run.add_argument('--no-pin-check', action='store_true', help=argparse.SUPPRESS)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.only and set(args.only) - set(NAMES):
        parser.error(f'--only: unknown {", ".join(sorted(set(args.only) - set(NAMES)))}; '
                     f'choose from {", ".join(NAMES)}')
    if args.frames < 2 or args.runs < 1:
        parser.error('--frames must be at least 2 and --runs at least 1')
    args.processes = 1
    try:
        return {'plan': cmd_plan, 'run': cmd_run}[args.command](args)
    except (sweep.SweepError, sweep.rs.ResultsError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
