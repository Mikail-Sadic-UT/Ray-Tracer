"""Tests for scripts/bench/results_schema.py against the shared fixtures in tests/data/bench.

The same fixtures go through rt::validate_results in tests/test_results_fixtures.cpp, so the
Python mirror and the C++ validator must agree on every case in manifest.json.
"""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--scripts', required=True)
parser.add_argument('--data', required=True)
args, remaining = parser.parse_known_args()
sys.path.insert(0, args.scripts)
DATA = Path(args.data)

import results_schema as rs  # noqa: E402


def fixture(name):
    return rs.load_results(DATA / name)


class Fixtures(unittest.TestCase):
    def test_valid_files_pass(self):
        manifest = json.loads((DATA / 'manifest.json').read_text())
        self.assertTrue(manifest['valid'])
        for name in manifest['valid']:
            with self.subTest(name):
                rs.validate(fixture(name))

    def test_invalid_files_fail_naming_the_field(self):
        manifest = json.loads((DATA / 'manifest.json').read_text())
        self.assertTrue(manifest['invalid'])
        for entry in manifest['invalid']:
            with self.subTest(entry['file']):
                with self.assertRaises(rs.ResultsError) as caught:
                    rs.validate(fixture(entry['file']))
                self.assertIn(entry['error'], str(caught.exception))

    def test_every_fixture_file_is_in_the_manifest(self):
        manifest = json.loads((DATA / 'manifest.json').read_text())
        listed = set(manifest['valid']) | {e['file'] for e in manifest['invalid']}
        on_disk = {p.name for p in DATA.glob('*.json')} - {'manifest.json'}
        self.assertEqual(listed, on_disk)


class Mutations(unittest.TestCase):
    """Cases beyond the committed files, built from the valid app fixture."""

    def mutated(self, change):
        doc = copy.deepcopy(fixture('app_valid.json'))
        change(doc)
        return doc

    def assertRejects(self, change, fragment):
        with self.assertRaises(rs.ResultsError) as caught:
            rs.validate(self.mutated(change))
        self.assertIn(fragment, str(caught.exception))

    def test_not_an_object(self):
        with self.assertRaises(rs.ResultsError):
            rs.validate([])

    def test_integer_fields_reject_floats_bools_and_negatives(self):
        self.assertRejects(lambda d: d['machine'].__setitem__('cpu_cores', 8.0), 'machine.cpu_cores')
        self.assertRejects(lambda d: d['machine'].__setitem__('cpu_cores', True), 'machine.cpu_cores')
        self.assertRejects(lambda d: d['config'].__setitem__('width', -1), 'config.width')

    def test_missing_section_and_field_paths(self):
        self.assertRejects(lambda d: d.pop('image'), 'image: missing')
        self.assertRejects(lambda d: d['config']['camera'].pop('up'), 'config.camera.up: missing')
        self.assertRejects(lambda d: d['config']['mem'].pop('fresh_pages'),
                           'config.mem.fresh_pages: missing')

    def test_fresh_pages_accepts_null_and_booleans_only(self):
        for value in (None, True, False):
            doc = self.mutated(lambda d, v=value: d['config']['mem'].__setitem__('fresh_pages', v))
            rs.validate(doc)
        self.assertRejects(lambda d: d['config']['mem'].__setitem__('fresh_pages', 'no'),
                           'config.mem.fresh_pages')

    def test_empty_strings_where_not_allowed(self):
        self.assertRejects(lambda d: d['config'].__setitem__('backend', ''), 'config.backend')
        rs.validate(self.mutated(lambda d: d['config'].__setitem__('pipeline', '')))

    def test_vec3_needs_three_finite_numbers(self):
        self.assertRejects(lambda d: d['config']['camera'].__setitem__('position', [0, 1]),
                           'config.camera.position')
        self.assertRejects(lambda d: d['config']['camera'].__setitem__('up', [0, 1, 'x']),
                           'config.camera.up')

    def test_image_hash_needs_the_exact_form(self):
        for bad in ('0x1234', '1234567890abcdef12', '0x0123456789abcdeg', '0X0123456789abcdef'):
            with self.subTest(bad):
                self.assertRejects(lambda d, b=bad: d['image'].__setitem__('rgb_hash', b),
                                   'image.rgb_hash')

    def test_unattributed_uses_a_relative_tolerance(self):
        doc = self.mutated(lambda d: None)
        expected = doc['app']['unattributed_ms']
        doc['app']['unattributed_ms'] = expected + 1e-12  # inside 1e-9 relative
        rs.validate(doc)
        doc['app']['unattributed_ms'] = expected + 1e-3
        with self.assertRaises(rs.ResultsError):
            rs.validate(doc)

    def test_nan_and_infinity_are_not_json(self):
        with tempfile.TemporaryDirectory() as directory:
            for text in ('NaN', 'Infinity', '-Infinity'):
                path = Path(directory) / 'bad.json'
                path.write_text('{"x": ' + text + '}')
                with self.subTest(text):
                    with self.assertRaises(rs.ResultsError):
                        rs.load_results(path)

    def test_load_reports_missing_and_malformed_files(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(rs.ResultsError):
                rs.load_results(Path(directory) / 'absent.json')
            path = Path(directory) / 'broken.json'
            path.write_text('{"a": ')
            with self.assertRaises(rs.ResultsError):
                rs.load_results(path)

    def test_prepared_sections_validate_run_by_run(self):
        doc = fixture('prepared_valid.json')
        doc['build_and_render']['runs'][2]['components'].pop('render')
        with self.assertRaises(rs.ResultsError) as caught:
            rs.validate(doc)
        self.assertIn('build_and_render.runs[2].components.render: missing', str(caught.exception))


class Summarize(unittest.TestCase):
    """Hand-computed values; the even-count and tie rules are CONTRACT_NOTES W10."""

    def test_odd_count(self):
        s = rs.summarize([10.0, 12.0, 9.0, 11.0, 14.0])
        self.assertEqual(s['median_ms'], 11.0)
        self.assertEqual((s['min_ms'], s['max_ms']), (9.0, 14.0))
        self.assertEqual(s['median_run_index'], 3)
        self.assertAlmostEqual(s['spread'], 5.0 / 11.0, places=15)

    def test_even_count_uses_mean_of_middle_and_lower_run(self):
        s = rs.summarize([4.0, 1.0, 3.0, 2.0])
        self.assertEqual(s['median_ms'], 2.5)
        self.assertEqual(s['median_run_index'], 3)  # total 2.0 is the lower of 2.0 and 3.0
        self.assertAlmostEqual(s['spread'], 3.0 / 2.5, places=15)

    def test_equal_totals_keep_run_order(self):
        self.assertEqual(rs.summarize([5.0, 5.0, 5.0, 5.0])['median_run_index'], 1)
        self.assertEqual(rs.summarize([5.0, 5.0, 5.0])['median_run_index'], 1)

    def test_zero_median_gives_zero_spread(self):
        self.assertEqual(rs.summarize([0.0, 0.0, 0.0])['spread'], 0.0)

    def test_single_run(self):
        s = rs.summarize([7.0])
        self.assertEqual((s['median_ms'], s['spread'], s['median_run_index']), (7.0, 0.0, 0))

    def test_rejects_empty_negative_and_nonfinite(self):
        for bad in ([], [1.0, -1.0], [1.0, float('nan')], [float('inf')]):
            with self.subTest(bad):
                with self.assertRaises(rs.ResultsError):
                    rs.summarize(bad)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
