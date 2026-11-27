"""Tests for scripts/tacc/sweep.py: presets, environment checks, and a small real sweep.

The sweep runs the real rt_render on the CPU backend at a tiny resolution, so it works in the
CPU-only build and in CI. Timing values are only checked for shape, never for size.
"""
import argparse
import csv
import io
import json
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
        for s in sweep.SIZES:
            names = [p.build_threads for p in sweep.preset_points('cpu-threads', 'vista')
                     if p.scene == s]
            self.assertEqual(names, [1, 2, 4, 8, 16, 32, 64, 72], s)
            laptop = [p for p in sweep.preset_points('cpu-threads', 'laptop') if p.scene == s]
            self.assertEqual([p.render_threads for p in laptop], [1, 2, 4, 8, 16, 24, 32], s)
            gpu = [p for p in sweep.preset_points('gpu-build-threads', 'vista') if p.scene == s]
            self.assertEqual([(p.backend, p.build_threads, p.render_threads) for p in gpu],
                             [('cuda', 1, 1), ('cuda', 8, 1), ('cuda', 72, 1)], s)

    def test_thread_sweeps_cover_every_size_at_full_hd_lbvh(self):
        for preset in ('cpu-threads', 'gpu-build-threads'):
            points = sweep.preset_points(preset, 'vista')
            self.assertEqual({p.scene for p in points}, set(sweep.SIZES), preset)
            for p in points:
                self.assertEqual((p.width, p.height, p.bvh, p.mode),
                                 (1920, 1080, 'lbvh', 'prepared'))

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
        for preset in ('cpu-threads', 'gpu-build-threads', 'size', 'bvh', 'resolution', 'lights',
                       'tapp'):
            for p in sweep.preset_points(preset, 'vista'):
                self.assertIn(p.name, names, preset)

    def test_lights_preset_varies_only_the_light_count(self):
        points = sweep.preset_points('lights', 'vista')
        self.assertEqual(sorted({p.lights for p in points}), sweep.LIGHT_COUNTS)
        self.assertEqual({(p.scene, p.bvh, p.width) for p in points},
                         {(sweep.BASELINE_SCENE, 'lbvh', 1920)})
        one = [p for p in points if p.lights == 1]
        size = {p.name for p in sweep.preset_points('size', 'vista')}
        self.assertTrue(all(p.name in size for p in one), 'L = 1 reuses the size points')
        four = next(p for p in points if p.lights == 4)
        self.assertTrue(four.name.endswith('_L4'))
        with tempfile.TemporaryDirectory() as scenes:
            (Path(scenes) / f'{sweep.BASELINE_SCENE}.json').write_text('{}')
            args = sweep.render_args(four, scenes)
            self.assertEqual(args[args.index('--lights') + 1], '4')
            self.assertNotIn('--lights', sweep.render_args(one[0], scenes))

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


def sweep_row(scene, quantity, backend, threads, median, lights=1, prims=1000):
    return {'scene': scene, 'quantity': quantity, 'backend': backend, 'bvh': 'lbvh',
            'width': '1920', 'height': '1080', 'num_lights': str(lights),
            'num_primitives': str(prims), 'build_threads': str(threads),
            'render_threads': str(threads if backend == 'cpu' else 1),
            'median_ms': str(median), 'commit': 'abc'}


class FinalSelection(unittest.TestCase):
    """sweep.py final: which configurations the final runs rerun (BENCHMARKING "Targets")."""

    ROWS = ([sweep_row('s', 'T_br', 'cpu', t, m) for t, m in ((1, 900), (64, 20), (72, 25))] +
            [sweep_row('s', 'T_br', 'cuda', b, m) for b, m in ((1, 50), (8, 9), (72, 10))] +
            [sweep_row('s', 'T_frame', 'cpu', t, m) for t, m in ((1, 800), (64, 19), (72, 18))] +
            [sweep_row('s', 'T_frame', 'cuda', b, m) for b, m in ((1, 5), (8, 6), (72, 6))] +
            [sweep_row('s', 'T_app', 'cpu', t, m) for t, m in ((1, 950), (72, 60))] +
            [sweep_row('s', 'T_app', 'cuda', 72, 300)] +
            # A light-count row that is faster than every one-light row: must not be chosen.
            [sweep_row('s', 'T_br', 'cpu', 72, 1, lights=4)])

    def roles(self, selection, quantity):
        return {sel['role']: sel['point'] for sel in selection if sel['quantity'] == quantity}

    def test_lowest_sweep_median_per_quantity(self):
        sel = sweep.final_selection(self.ROWS, 'vista')
        br = self.roles(sel, 'T_br')
        self.assertEqual(br['one_core'].build_threads, 1)
        self.assertEqual(br['cpu_star'].build_threads, 64)
        self.assertEqual(br['cpu_fixed'].build_threads, 72)
        self.assertEqual((br['gpu_star'].backend, br['gpu_star'].build_threads), ('cuda', 8))
        self.assertEqual(br['gpu_fixed'].build_threads, 72)
        self.assertEqual(br['cpu_star'].lights, 1)
        frame = self.roles(sel, 'T_frame')
        self.assertEqual(frame['cpu_star'].build_threads, 72)
        self.assertEqual(frame['gpu_star'].build_threads, 1)
        app = self.roles(sel, 'T_app')
        self.assertEqual({p.mode for p in app.values()}, {'app'})
        self.assertEqual(app['cpu_star'].build_threads, 72)

    def test_points_are_sweep_points_without_duplicates(self):
        sel = sweep.final_selection(self.ROWS, 'vista')
        points = sweep.final_points(sel)
        names = [p.name for p in points]
        self.assertEqual(len(names), len(set(names)))
        self.assertIn('prepared_cpu_s_lbvh_B64R64_1920x1080', names)
        self.assertIn('app_cuda_s_lbvh_B72_1920x1080', names)
        # T_br and T_frame share a process: B72 CPU is one point for both quantities.
        self.assertEqual(names.count('prepared_cpu_s_lbvh_B72R72_1920x1080'), 1)

    def test_rounds_measure_every_point_once_in_rotated_order(self):
        points = sweep.final_points(sweep.final_selection(self.ROWS, 'vista'))
        orders = [sweep.round_order(points, r) for r in range(5)]
        for order in orders:
            self.assertEqual(sorted(p.name for p in order), sorted(p.name for p in points))
        self.assertEqual(len({order[0].name for order in orders}), 5)

    def test_missing_required_role_fails(self):
        rows = [r for r in self.ROWS if not (r['backend'] == 'cpu' and r['build_threads'] == '1')]
        with self.assertRaisesRegex(sweep.SweepError, 'no one_core row'):
            sweep.final_selection(rows, 'vista')

    def test_cpu_only(self):
        rows = [r for r in self.ROWS if r['backend'] == 'cpu']
        with self.assertRaisesRegex(sweep.SweepError, 'gpu_star'):
            sweep.final_selection(rows, 'vista')
        sel = sweep.final_selection(rows, 'vista', backends=['cpu'])
        self.assertEqual({s['point'].backend for s in sel}, {'cpu'})

    def test_unknown_scene(self):
        with self.assertRaisesRegex(sweep.SweepError, 'nope'):
            sweep.final_selection(self.ROWS, 'vista', scenes=['nope'])

    def test_estimate_counts_warm_up_and_rounds(self):
        sel = sweep.final_selection(self.ROWS, 'vista')
        one = sweep.final_estimate_s(sel, runs=5, rounds=1)
        self.assertAlmostEqual(sweep.final_estimate_s(sel, runs=5, rounds=3), 3 * one)
        self.assertGreater(one, 6 * (900 + 800) / 1000.0)


class RealFinal(unittest.TestCase):
    """sweep.py final on a smoke sweep (CPU backend), 2 rounds of 2 runs, then ratios.py."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        cls.env = dict(os.environ, OMP_PROC_BIND='close', OMP_PLACES='cores')
        common = ['--machine', 'laptop', '--scenes', cls.tmp.name, '--backends', 'cpu',
                  '--runs', '2']
        cls.sweep = run_script('run', '--preset', 'smoke', *common, '--processes', '2',
                               '--rt-render', args.render, '--out', str(tmp / 'sweep'),
                               env=cls.env)
        # The smoke preset has no 1-core T_app point; add its row so every role has a
        # configuration (final measures each configuration itself, so the value does not matter).
        with open(tmp / 'sweep' / 'sweep.csv', newline='') as f:
            reader = csv.DictReader(f)
            fields, rows = reader.fieldnames, list(reader)
        app = next(r for r in rows if r['quantity'] == 'T_app')
        rows.append(dict(app, build_threads='1', render_threads='1',
                         median_ms=str(float(app['median_ms']) + 1.0)))
        with open(tmp / 'sweep.csv', 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        cls.argv = ['final', '--sweep', str(tmp / 'sweep.csv'), *common,
                    '--width', '160', '--rounds', '2', '--rt-render', args.render,
                    '--out', str(tmp / 'final')]
        cls.first = run_script(*cls.argv, env=cls.env)
        cls.out = tmp / 'final'

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def rows(self):
        with open(self.out / 'final.csv') as f:
            return list(csv.DictReader(f))

    def plan(self):
        return json.loads((self.out / 'final_plan.json').read_text())

    def test_final_csv_has_roles_and_rounds(self):
        self.assertEqual(self.sweep.returncode, 0, self.sweep.stderr)
        self.assertEqual(self.first.returncode, 0, self.first.stderr)
        rows = self.rows()
        # Prepared and app configurations at B = R = 1 and 2. When the 1-thread run was the
        # faster one in the sweep (a loaded machine, a tiny image), CPU* is that point too and
        # only one prepared configuration is selected.
        modes = [p['mode'] for p in self.plan()['points']]
        self.assertIn(modes.count('prepared'), (1, 2))
        self.assertEqual(modes.count('app'), 2)
        self.assertEqual(sorted(r['quantity'] for r in rows),
                         sorted(['T_app'] * 2 + ['T_br', 'T_frame'] * modes.count('prepared')))
        for r in rows:
            self.assertEqual(r['rounds'], '2')
            self.assertEqual(r['runs'], '2' if r['quantity'] != 'T_app' else '1')
            self.assertLessEqual(float(r['min_ms']), float(r['median_ms']))
        one = next(r for r in rows if r['quantity'] == 'T_br' and r['build_threads'] == '1')
        self.assertIn('one_core', one['roles'].split())
        # CPU* is whichever thread count the sweep found faster; at 160x120 it can be either.
        star = [r for r in rows if r['quantity'] == 'T_br' and 'cpu_star' in r['roles'].split()]
        self.assertEqual(len(star), 1)
        star = star[0]
        for r in range(2):
            self.assertTrue((self.out / 'points' / star['point'] / f'round_{r}' /
                             'prepared.json').is_file())

    def test_rerun_skips_finished_rounds(self):
        again = run_script(*self.argv, env=self.env)
        self.assertEqual(again.returncode, 0, again.stderr)
        expected = 2 * len(self.plan()['points'])  # every configuration, 2 rounds
        self.assertEqual(again.stderr.count('done earlier, skipped'), expected, again.stderr)

    def test_refuses_other_configurations_in_the_same_directory(self):
        argv = [a if a != '2' else '3' for a in self.argv]  # --runs 3, --rounds 3
        r = run_script(*argv, env=self.env)
        self.assertEqual(r.returncode, 1)
        self.assertIn('other configurations', r.stderr)

    def test_merge_recognizes_final_runs(self):
        r = run_script('merge', '--out', str(self.out))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('final.csv', r.stderr)

    def test_dry_run_measures_nothing(self):
        argv = self.argv[:self.argv.index('--rt-render')] + ['--dry-run']
        r = run_script(*argv, env=self.env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('cpu_star', r.stdout)
        self.assertIn('rounds', r.stderr)

    def test_ratios_reads_roles(self):
        r = subprocess.run([sys.executable, str(Path(args.scripts) / 'ratios.py'),
                            str(self.out / 'final.csv')], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('Final target ratios', r.stdout)
        self.assertIn('2 interleaved rounds', r.stdout)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
