"""Tests for scripts/tacc/sweep.py: presets, environment checks, and a small real sweep.

The sweep runs the real rt_render on the CPU backend at a tiny resolution, so it works in the
CPU-only build and in CI. Timing values are only checked for shape, never for size.
"""
import argparse
import csv
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

parser = argparse.ArgumentParser()
parser.add_argument('--scripts', required=True, help='scripts/tacc')
parser.add_argument('--render', required=True)
args, remaining = parser.parse_known_args()
sys.path.insert(0, args.scripts)
import sweep  # noqa: E402

SCRIPT = Path(args.scripts) / 'sweep.py'


class Presets(unittest.TestCase):
    def test_thread_lists_follow_benchmarking(self):
        names = [p.build_threads for p in sweep.preset_points('cpu-threads', 'vista')]
        self.assertEqual(names, [1, 2, 4, 8, 16, 32, 64, 72])
        laptop = sweep.preset_points('cpu-threads', 'laptop')
        self.assertEqual([p.render_threads for p in laptop], [1, 2, 4, 8, 16, 24, 32])
        gpu = sweep.preset_points('gpu-build-threads', 'vista')
        self.assertEqual([(p.backend, p.build_threads, p.render_threads) for p in gpu],
                         [('cuda', 1, 1), ('cuda', 8, 1), ('cuda', 72, 1)])

    def test_baseline_is_1m_full_hd_lbvh(self):
        for p in sweep.preset_points('cpu-threads', 'vista'):
            self.assertEqual((p.scene, p.width, p.height, p.bvh, p.mode),
                             ('bench_1m_s1', 1920, 1080, 'lbvh', 'prepared'))

    def test_size_includes_one_core_baseline_at_each_size(self):
        points = sweep.preset_points('size', 'vista')
        for s in sweep.SIZES:
            kinds = {(p.backend, p.build_threads) for p in points if p.scene == s}
            self.assertEqual(kinds, {('cpu', 1), ('cpu', 72), ('cuda', 72)})

    def test_g2_has_all_three_quantities_at_three_sizes(self):
        points = sweep.preset_points('g2', 'vista')
        for s in sweep.SIZES:
            modes = {p.mode for p in points if p.scene == s and p.backend == 'cuda'}
            self.assertEqual(modes, {'prepared', 'app'})

    def test_core_has_no_duplicates_and_contains_every_dimension(self):
        core = sweep.preset_points('core', 'vista')
        names = [p.name for p in core]
        self.assertEqual(len(names), len(set(names)))
        for preset in ('cpu-threads', 'gpu-build-threads', 'size', 'bvh', 'resolution', 'tapp'):
            for p in sweep.preset_points(preset, 'vista'):
                self.assertIn(p.name, names, preset)

    def test_unknown_preset(self):
        with self.assertRaises(sweep.SweepError):
            sweep.preset_points('nope', 'vista')

    def test_point_names_are_unique_per_configuration(self):
        a = sweep.cpu('bench_1m_s1', 8)
        self.assertNotEqual(a.name, sweep.cpu('bench_1m_s1', 8, mode='app').name)
        self.assertNotEqual(a.name, sweep.cpu('bench_1m_s1', 8, bvh='median').name)
        self.assertNotEqual(a.name, sweep.cpu('bench_1m_s1', 8, res=sweep.VGA).name)


class Environment(unittest.TestCase):
    GOOD = '\tlibgomp.so.1 => /opt/apps/gcc/13.2.0/lib64/libgomp.so.1 (0x0000)\n'

    def test_openmp_runtime(self):
        sweep.check_openmp(self.GOOD)
        for bad in ('\tlibc.so.6 => /lib64/libc.so.6\n',
                    '\tlibomp.so => /opt/nvidia/hpc_sdk/lib/libomp.so\n',
                    self.GOOD + '\tlibnvomp.so => /opt/nvidia/nvhpc/lib/libnvomp.so\n'):
            with self.assertRaises(sweep.SweepError, msg=bad):
                sweep.check_openmp(bad)

    def test_pinning(self):
        sweep.check_pinning({'OMP_PROC_BIND': 'close', 'OMP_PLACES': 'cores'})
        with self.assertRaises(sweep.SweepError):
            sweep.check_pinning({'OMP_PROC_BIND': 'close'})


def run_script(*argv, env=None):
    return subprocess.run([sys.executable, str(SCRIPT), *argv], capture_output=True, text=True,
                          env=env)


class RealSweep(unittest.TestCase):
    """The smoke preset on the CPU backend, end to end, with 2 runs and 2 processes."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls.tmp.name) / 'sweep'
        cls.argv = ['--preset', 'smoke', '--machine', 'laptop', '--scenes', cls.tmp.name,
                    '--backends', 'cpu', '--runs', '2', '--processes', '2']
        env = dict(os.environ, OMP_PROC_BIND='close', OMP_PLACES='cores')
        cls.env = env
        cls.first = run_script('run', *cls.argv, '--rt-render', args.render, '--out',
                               str(cls.out), env=env)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_run_succeeds_with_clean_stdout(self):
        self.assertEqual(self.first.returncode, 0, self.first.stderr)
        self.assertEqual(self.first.stdout, '')

    def test_merged_csv(self):
        with open(self.out / 'sweep.csv') as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(sorted(r['quantity'] for r in rows),
                         ['T_app', 'T_br', 'T_br', 'T_frame', 'T_frame'])
        for r in rows:
            self.assertEqual(r['backend'], 'cpu')
            self.assertEqual(r['scene'], 'known_two_spheres')
            self.assertEqual((r['width'], r['height']), ('160', '120'))
            self.assertEqual(r['runs'], '2')
            self.assertGreater(float(r['median_ms']), 0.0)
            self.assertLessEqual(float(r['min_ms']), float(r['median_ms']))
            self.assertGreater(float(r['render_ms']), 0.0)
        app = next(r for r in rows if r['quantity'] == 'T_app')
        self.assertEqual(app['render_threads'], '2')

    def test_identity_and_plan_recorded(self):
        for name in ('lscpu.txt', 'ldd.txt', 'env.json', 'gcc.txt'):
            self.assertTrue((self.out / 'identity' / name).is_file(), name)
        self.assertIn('libgomp', (self.out / 'identity' / 'ldd.txt').read_text())
        self.assertTrue((self.out / 'plan.json').is_file())

    def test_rerun_skips_finished_points(self):
        again = run_script('run', *self.argv, '--rt-render', args.render, '--out',
                           str(self.out), env=self.env)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(again.stderr.count('done earlier, skipped'), 3, again.stderr)

    def test_merge_alone(self):
        r = run_script('merge', '--out', str(self.out))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('5 rows', r.stderr)

    def test_plan_lists_points(self):
        r = run_script('plan', *self.argv)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(len(r.stdout.splitlines()), 3)

    def test_requires_pinning(self):
        env = {k: v for k, v in self.env.items() if k not in ('OMP_PROC_BIND', 'OMP_PLACES')}
        r = run_script('run', *self.argv, '--rt-render', args.render, '--out',
                       str(Path(self.tmp.name) / 'nopin'), env=env)
        self.assertEqual(r.returncode, 1)
        self.assertIn('OMP_PROC_BIND', r.stderr)

    def test_missing_scene_fails_before_running(self):
        r = run_script('run', '--preset', 'size', '--machine', 'laptop', '--scenes',
                       self.tmp.name, '--rt-render', args.render, '--out',
                       str(Path(self.tmp.name) / 'missing'), env=self.env)
        self.assertEqual(r.returncode, 1)
        self.assertIn('fetch_scenes.sh', r.stderr)
        self.assertFalse((Path(self.tmp.name) / 'missing').exists())

    def test_failed_point_is_reported(self):
        out = Path(self.tmp.name) / 'broken'
        r = run_script('run', *self.argv, '--rt-render', str(SCRIPT), '--out', str(out),
                       env=self.env)  # not rt_render: ldd finds no libgomp
        self.assertEqual(r.returncode, 1)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
