"""Tests for scripts/tacc/extra.py and scripts/bench/make_balls.py.

The real extra scenes are large or local (scenes/meshes is not in git), so the end-to-end run
swaps in a known scene at a tiny resolution and a two-waypoint camera path, on the CPU backend,
which works in the CPU-only build and in CI.
"""
import argparse
import csv
import io
import json
import os
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--scripts', required=True, help='scripts/')
parser.add_argument('--render', required=True)
args, remaining = parser.parse_known_args()
sys.path.insert(0, str(Path(args.scripts) / 'tacc'))
import extra  # noqa: E402
import sweep  # noqa: E402

MAKE_BALLS = Path(args.scripts) / 'bench' / 'make_balls.py'


class Plan(unittest.TestCase):
    def test_stills_run_cpu_and_gpu_at_the_top_thread_count(self):
        points = extra.still_points('vista', ['cpu', 'cuda'], None)
        self.assertEqual(len(points), 2 * len(extra.STILLS))
        for p in points:
            self.assertEqual(p.build_threads, 72)
            self.assertEqual(p.render_threads, 72 if p.backend == 'cpu' else 1)
        sizes = {p.scene: (p.width, p.height) for p in points}
        self.assertEqual(sizes['terrain_10k'], (1920, 1080))
        for name in ('map_crew', 'dragon_gold', 'balls'):
            self.assertEqual(sizes[name], (3840, 2160))

    def test_walks_render_cuda_before_cpu_at_each_resolution(self):
        runs = extra.walk_runs('vista', ['cpu', 'cuda'], None)
        self.assertEqual([(r[1], r[2]) for r in runs],
                         [((1920, 1080), 'cuda'), ((1920, 1080), 'cpu'),
                          ((3840, 2160), 'cuda'), ((3840, 2160), 'cpu')])
        self.assertEqual(extra.walk_runs('vista', ['cpu'], ['balls']), [])

    def test_parse_walk(self):
        stderr = ('rt_render: cpu, bvh lbvh, 160x90, lit, lights 1, B=2 R=2, path 3 frames -> d\n'
                  '  preparation: bounds 0.5 ms, build 1.25 ms, layout 2 ms, convert 0 ms, '
                  'alloc 0 ms, upload 0 ms\n'
                  '  T_frame over the path: median 4.5 ms (min 3, max 6.5, spread 0.777, '
                  '3 frames, total 14 ms)\n')
        prep, frame = extra.parse_walk(stderr)
        self.assertEqual(prep['build'], 1.25)
        self.assertEqual(frame, {'median_ms': 4.5, 'min_ms': 3.0, 'max_ms': 6.5,
                                 'spread': 0.777, 'frames': 3, 'total_ms': 14.0})
        with self.assertRaises(sweep.SweepError):
            extra.parse_walk('no timing here\n')

    def test_missing_scenes_fail_before_measuring(self):
        with tempfile.TemporaryDirectory() as d:
            err = io.StringIO()
            with redirect_stderr(err):
                code = extra.main(['plan', '--machine', 'laptop', '--scenes', d])
            self.assertEqual(code, 1)
            self.assertIn('map_crew', err.getvalue())
            self.assertIn('--layout terrain', err.getvalue())

    def test_unknown_only(self):
        with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
            extra.main(['plan', '--machine', 'laptop', '--only', 'nope'])


class FakeFfmpeg(unittest.TestCase):
    """A missing, unrunnable or failing ffmpeg costs the video, never the run."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.old_path = os.environ['PATH']
        os.environ['PATH'] = f'{self.bin}{os.pathsep}/usr/bin:/bin'
        walk = self.root / 'walks' / 'w' / 'frames'
        walk.mkdir(parents=True)
        (walk / 'frame_0000.png').write_bytes(b'x')
        self.frames = walk
        self.args = argparse.Namespace(no_video=False, keep_frames=False)
        self.walk = extra.Walk('house', 'house_walk', ((8, 8),))

    def tearDown(self):
        os.environ['PATH'] = self.old_path
        self.tmp.cleanup()

    def fake(self, body):
        exe = self.bin / 'ffmpeg'
        exe.write_text(f'#!/bin/sh\n{body}\n')
        exe.chmod(0o755)

    def finish(self):
        err = io.StringIO()
        with redirect_stderr(err):
            extra.finish_frames(self.args, self.root, self.walk, (8, 8), 'w', None)
        return err.getvalue()

    def test_missing(self):
        # No ffmpeg in the fake bin; the system PATH tail may have one, so hide it.
        os.environ['PATH'] = str(self.bin)
        self.assertFalse(extra.ffmpeg_works())
        self.assertIn('frames kept', self.finish())
        self.assertTrue(self.frames.is_dir())

    def test_installed_but_cannot_start(self):
        # What Vista compute nodes do: the binary exists, the dynamic loader fails (exit 127).
        self.fake('echo "error while loading shared libraries: libunwind.so.8" >&2; exit 127')
        self.assertFalse(extra.ffmpeg_works())
        self.assertIn('cannot run', self.finish())
        self.assertTrue(self.frames.is_dir())

    def test_encoding_fails(self):
        self.fake('[ "$1" = "-version" ] && exit 0; echo "boom" >&2; exit 1')
        self.assertTrue(extra.ffmpeg_works())
        out = self.finish()  # must not raise
        self.assertIn('ffmpeg failed', out)
        self.assertTrue(self.frames.is_dir(), 'frames are kept for encoding later')

    def test_encoding_works(self):
        self.fake('for last; do :; done; [ "$1" = "-version" ] || : > "$last"; exit 0')
        self.assertIn('video:', self.finish())
        self.assertTrue((self.root / 'videos' / 'house_8x8.mp4').is_file())
        self.assertFalse(self.frames.exists(), 'frames are deleted after a good encode')


class Balls(unittest.TestCase):
    def test_committed_scene_is_up_to_date(self):
        r = subprocess.run([sys.executable, str(MAKE_BALLS), '--check'], capture_output=True,
                           text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_spheres_rest_on_the_floor_without_overlap(self):
        scene = json.loads(extra.sweep.scene_path(extra.DEFAULT_SCENES, 'balls').read_text())
        spheres = scene['geometry']['spheres']
        self.assertEqual(len(spheres), 48)
        self.assertEqual(len(scene['lights']), 4)
        kinds = {('mirror' in m) * 1 + ('glass' in m) * 2
                 for i, m in enumerate(scene['materials'])
                 if any(s['material'] == i for s in spheres)}
        self.assertEqual(kinds, {0, 1, 2}, 'matte or glossy, mirror and glass balls')
        for i, a in enumerate(spheres):
            self.assertEqual(a['center'][1], a['radius'])
            for b in spheres[i + 1:]:
                gap = ((a['center'][0] - b['center'][0]) ** 2 +
                       (a['center'][2] - b['center'][2]) ** 2) ** 0.5
                self.assertGreater(gap, a['radius'] + b['radius'])


class RealRun(unittest.TestCase):
    """extra.py run with a tiny still and walk on the CPU backend."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        (tmp / 'tiny_path.json').write_text(json.dumps({'waypoints': [
            {'position': [0, 1, 5], 'look_at': [0, 0.5, 0]},
            {'position': [1, 1.5, 4], 'look_at': [0, 0.5, 0]}]}))
        cls.saved = extra.STILLS, extra.WALKS, extra.NAMES
        extra.STILLS = (extra.Still('known_two_spheres', (160, 90)),)
        extra.WALKS = (extra.Walk('known_single_sphere', 'tiny_path', ((96, 54),)),)
        extra.NAMES = ('known_two_spheres', 'known_single_sphere')
        cls.out = tmp / 'out'
        os.environ.update(OMP_PROC_BIND='close', OMP_PLACES='cores')
        cls.argv = ['run', '--machine', 'laptop', '--scenes', str(tmp), '--backends', 'cpu',
                    '--runs', '2', '--frames', '3', '--rt-render', args.render,
                    '--out', str(cls.out), '--no-video']
        cls.stdout, cls.stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(cls.stdout), redirect_stderr(cls.stderr):
            cls.code = extra.main(cls.argv)

    @classmethod
    def tearDownClass(cls):
        extra.STILLS, extra.WALKS, extra.NAMES = cls.saved
        cls.tmp.cleanup()

    def test_succeeds(self):
        self.assertEqual(self.code, 0, self.stderr.getvalue())

    def test_still_rows_in_sweep_csv(self):
        with open(self.out / 'sweep.csv') as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(sorted(r['quantity'] for r in rows), ['T_br', 'T_frame'])
        self.assertEqual({(r['width'], r['height']) for r in rows}, {('160', '90')})

    def test_walk_recorded_with_total(self):
        with open(self.out / 'walks.csv') as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(len(rows), 1)
        r = rows[0]
        self.assertEqual((r['backend'], r['frames'], r['width']), ('cpu', '3', '96'))
        self.assertGreaterEqual(float(r['total_ms']), float(r['max_ms']))
        self.assertGreater(float(r['build_ms']), 0.0)
        # --no-video and no --keep-frames: the frames are gone once measured.
        self.assertFalse((self.out / 'walks' / r['name'] / 'frames').exists())

    def test_report(self):
        text = (self.out / 'extra.md').read_text()
        self.assertIn('| known_two_spheres |', text)
        self.assertIn('| known_single_sphere | 96x54 | 3 |', text)
        self.assertEqual(text.rstrip('\n'), self.stdout.getvalue().rstrip('\n'))

    def test_rerun_skips_finished_items(self):
        err = io.StringIO()
        with redirect_stdout(io.StringIO()), redirect_stderr(err):
            code = extra.main(self.argv)
        self.assertEqual(code, 0, err.getvalue())
        self.assertEqual(err.getvalue().count('done earlier, skipped'), 2, err.getvalue())


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
