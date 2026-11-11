"""Tests for scripts/bench/run_tapp.py: aggregation of T_app results files.

Everything runs on fixture data with fixed numbers, so nothing here depends on wall-clock time.
"""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--scripts', required=True)
parser.add_argument('--data', required=True)
args, remaining = parser.parse_known_args()
SCRIPTS = Path(args.scripts)
DATA = Path(args.data)
sys.path.insert(0, str(SCRIPTS))

import results_schema as rs  # noqa: E402
import run_tapp  # noqa: E402

SCRIPT = SCRIPTS / 'run_tapp.py'
OTHER_COMPONENTS = 0.5 + 3.25 + 1.0 + 2.0 + 0.5  # args, load, bounds, build, layout in the fixture


def app_doc(total, render_gap=1.75):
    """A valid app file with the given total. `render_gap` of it stays unattributed."""
    doc = copy.deepcopy(rs.load_results(DATA / 'app_valid.json'))
    doc['app']['total_ms'] = total
    doc['app']['components']['render'] = total - OTHER_COMPONENTS - render_gap
    doc['app']['unattributed_ms'] = render_gap
    rs.validate(doc)
    return doc


def docs_for(totals):
    return [app_doc(t) for t in totals]


def labels_for(docs):
    return [f'tapp_{i}.json' for i in range(len(docs))]


class Aggregate(unittest.TestCase):
    def summary(self, totals):
        docs = docs_for(totals)
        return run_tapp.aggregate(docs, labels_for(docs))

    def test_median_of_five(self):
        s = self.summary([30.0, 32.0, 29.0, 31.0, 33.0])
        self.assertEqual(s['quantity'], 'T_app')
        self.assertEqual(s['processes'], 5)
        self.assertEqual(s['median_ms'], 31.0)
        self.assertEqual((s['min_ms'], s['max_ms']), (29.0, 33.0))
        self.assertAlmostEqual(s['spread'], 4.0 / 31.0, places=15)
        self.assertEqual(s['median_run_index'], 3)
        self.assertEqual(s['totals_ms'], [30.0, 32.0, 29.0, 31.0, 33.0])
        # 4 / 31 = 12.9% is above the 10% line, and that is the only warning.
        self.assertEqual(len(s['warnings']), 1)
        self.assertIn('spread 12.9%', s['warnings'][0])

    def test_median_run_components_are_that_runs_not_a_median_of_components(self):
        s = self.summary([30.0, 32.0, 29.0, 31.0, 33.0])
        run = s['median_run']
        self.assertEqual(run['file'], 'tapp_3.json')
        self.assertEqual(run['total_ms'], 31.0)
        self.assertEqual(run['components']['render'], 31.0 - OTHER_COMPONENTS - 1.75)
        self.assertEqual(run['unattributed_ms'], 1.75)

    def test_result_does_not_depend_on_file_order(self):
        totals = [30.0, 32.0, 29.0, 31.0, 33.0]
        a = self.summary(totals)
        b = self.summary(list(reversed(totals)))
        for key in ('median_ms', 'min_ms', 'max_ms', 'spread'):
            self.assertEqual(a[key], b[key])
        self.assertEqual(a['median_run']['total_ms'], b['median_run']['total_ms'])

    def test_even_count_follows_the_w10_rule(self):
        s = self.summary([40.0, 10.0, 30.0, 20.0])
        self.assertEqual(s['median_ms'], 25.0)
        self.assertEqual(s['median_run_index'], 3)  # total 20, the lower middle
        self.assertTrue(any('4 processes' in w for w in s['warnings']))

    def test_spread_warning_above_ten_percent_only(self):
        quiet = self.summary([100.0, 105.0, 100.0, 105.0, 100.0])  # (105-100)/100 = 5%
        self.assertEqual(quiet['warnings'], [])
        noisy = self.summary([100.0, 112.0, 100.0, 100.0, 100.0])  # 12%
        self.assertEqual(len(noisy['warnings']), 1)
        self.assertIn('spread', noisy['warnings'][0])
        edge = self.summary([100.0, 110.0, 100.0, 100.0, 100.0])  # exactly 10%: not above it
        self.assertEqual(edge['warnings'], [])

    def test_summary_carries_the_cache_note_and_identity(self):
        s = self.summary([30.0, 31.0, 32.0, 33.0, 34.0])
        self.assertIn('warm file cache', s['note'])
        self.assertEqual(s['scene_hash'], '0xcbed9151089614c9')
        self.assertEqual(s['machine']['host'], 'fixture-host')

    def test_no_files(self):
        with self.assertRaises(run_tapp.TappError):
            run_tapp.aggregate([], [])


class Rejects(unittest.TestCase):
    def aggregate(self, docs):
        return run_tapp.aggregate(docs, labels_for(docs))

    def assertRejected(self, docs, fragment):
        with self.assertRaises(run_tapp.TappError) as caught:
            self.aggregate(docs)
        self.assertIn(fragment, str(caught.exception))

    def five(self):
        return docs_for([30.0, 31.0, 32.0, 33.0, 34.0])

    def test_invalid_file_is_named_with_its_field(self):
        docs = self.five()
        docs[2]['app']['components'].pop('load')
        self.assertRejected(docs, 'tapp_2.json: results: app.components.load: missing')

    def test_prepared_mode_file(self):
        docs = self.five()
        docs[1] = rs.load_results(DATA / 'prepared_valid.json')
        self.assertRejected(docs, 'tapp_1.json: no app section')

    def test_file_with_both_app_and_prepared_sections(self):
        docs = self.five()
        docs[0]['frame'] = rs.load_results(DATA / 'prepared_valid.json')['frame']
        self.assertRejected(docs, 'tapp_0.json: has a frame section')

    def test_mismatches_name_the_field_and_the_file(self):
        def change(path, value):
            docs = self.five()
            target = docs[4]
            keys = path.split('.')
            for key in keys[:-1]:
                target = target[key]
            target[keys[-1]] = value
            return docs

        cases = [
            ('scene.hash', '0x0000000000000001', 'scene'),
            ('config.render_threads', 4, 'config'),
            ('config.shade', 'normal', 'config'),
            ('machine.host', 'other-host', 'machine'),
            ('build.commit', 'b' * 40, 'build'),
            ('build.dirty', True, 'build'),
            ('image.rgb_hash', '0x00000000000000ff', 'image.rgb_hash'),
        ]
        for path, value, field in cases:
            with self.subTest(path):
                self.assertRejected(change(path, value), f'tapp_4.json: {field} differs')

    def test_total_may_differ_between_runs(self):
        # The only thing that should differ between processes is the timing.
        self.aggregate(self.five())


class CommandLine(unittest.TestCase):
    def run_cli(self, *argv):
        return subprocess.run([sys.executable, str(SCRIPT), *argv], capture_output=True, text=True)

    def write_files(self, directory, docs):
        paths = []
        for i, doc in enumerate(docs):
            path = Path(directory) / f'tapp_{i}.json'
            path.write_text(json.dumps(doc))
            paths.append(str(path))
        return paths

    def test_report_and_summary_file(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.write_files(directory, docs_for([30.0, 32.0, 29.0, 31.0, 33.0]))
            summary_path = Path(directory) / 'summary.json'
            result = self.run_cli('--from', *paths, '--summary', str(summary_path))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('T_app over 5 processes', result.stdout)
            self.assertIn('median 31.000 ms', result.stdout)
            self.assertIn('spread 12.9%', result.stdout)
            self.assertIn('warm file cache', result.stdout)
            self.assertIn('unattributed', result.stdout)
            self.assertEqual(result.stderr, '')
            summary = json.loads(summary_path.read_text())
            self.assertEqual(summary['median_ms'], 31.0)
            self.assertEqual(summary['median_run']['file'], paths[3])

    def test_invalid_input_exits_1_with_the_field_on_stderr(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = docs_for([30.0, 31.0, 32.0, 33.0, 34.0])
            docs[0]['schema_version'] = 1
            paths = self.write_files(directory, docs)
            result = self.run_cli('--from', *paths)
            self.assertEqual(result.returncode, 1)
            self.assertIn('schema_version: expected 2', result.stderr)
            self.assertEqual(result.stdout, '')

    def test_missing_and_malformed_files_exit_1(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_cli('--from', str(Path(directory) / 'absent.json'))
            self.assertEqual(result.returncode, 1)
            self.assertIn('cannot read', result.stderr)
            broken = Path(directory) / 'broken.json'
            broken.write_text('{')
            result = self.run_cli('--from', str(broken))
            self.assertEqual(result.returncode, 1)
            self.assertIn('not valid JSON', result.stderr)

    def test_unwritable_summary_exits_1(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.write_files(directory, docs_for([30.0, 31.0, 32.0, 33.0, 34.0]))
            result = self.run_cli('--from', *paths, '--summary', str(Path(directory) / 'no' / 's.json'))
            self.assertEqual(result.returncode, 1)
            self.assertIn('cannot write', result.stderr)

    def test_usage_error_exits_2(self):
        self.assertEqual(self.run_cli().returncode, 2)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
