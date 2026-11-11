#!/usr/bin/env python3
"""Report T_app (cold application time) from several rt_render results files.

T_app is one sample per fresh process, and the report uses the median of 5 processes
(docs/BENCHMARKING.md "Timing quantities"). This script reads the results file of each process,
checks it against schema v2 (INTERFACES section 8), refuses to combine files that do not describe
the same run, and prints the median, min, max and spread of `app.total_ms`.

    run_tapp.py --from tapp_0.json tapp_1.json tapp_2.json tapp_3.json tapp_4.json

or run the processes itself. Everything after `--` goes to rt_render; the script adds
`--mode app --results <file>` for each process:

    run_tapp.py --rt-render build/apps/rt_render/rt_render --out-dir tapp-results \
        -- --scene scenes/known/known_two_spheres.json --backend cpu --bvh median

Exit status: 0 on success, 1 for an invalid, mismatched or missing input, 2 for a usage error.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import results_schema as rs  # noqa: E402

DEFAULT_PROCESSES = 5
SPREAD_WARNING = 0.10

# BENCHMARKING.md: "T_app on Vista is cold process, warm file cache (we can't drop OS caches);
# the report says so."
CACHE_NOTE = ('T_app is a cold process with a warm file cache: the OS file cache is not dropped '
              'between processes.')

# Fields that must be identical in every file, or the runs do not measure the same thing.
# Each entry is a dotted path into the results object.
SAME_IN_ALL_RUNS = ('machine', 'build', 'scene', 'config', 'image.rgb_hash')


class TappError(Exception):
    """An input that cannot be reported: invalid, mismatched, or the wrong kind of file."""


def _get(doc, dotted):
    value = doc
    for key in dotted.split('.'):
        value = value[key]
    return value


def check_app_file(doc, label):
    """Validates one results file and checks that it holds an app-mode measurement only."""
    try:
        rs.validate(doc)
    except rs.ResultsError as error:
        raise TappError(f'{label}: {error}') from error
    if 'app' not in doc:
        raise TappError(f'{label}: no app section; T_app needs a file written with --mode app')
    for section in ('build_and_render', 'frame'):
        if section in doc:
            raise TappError(f'{label}: has a {section} section; T_app needs a file written with '
                            '--mode app, not a prepared-mode file')


def check_same_run(docs, labels):
    """Raises TappError naming the first field that differs between the files."""
    for dotted in SAME_IN_ALL_RUNS:
        first = _get(docs[0], dotted)
        for doc, label in zip(docs[1:], labels[1:]):
            value = _get(doc, dotted)
            if value != first:
                raise TappError(
                    f'{label}: {dotted} differs from {labels[0]}; refusing to combine runs of '
                    f'different setups ({_brief(first)} vs {_brief(value)})')


def _brief(value):
    text = json.dumps(value, sort_keys=True)
    return text if len(text) <= 60 else text[:57] + '...'


def aggregate(docs, labels):
    """Summary of T_app over the given results files.

    `docs` are parsed results objects and `labels` name them (file paths) in messages. Raises
    TappError if any file is invalid, is not an app-mode file, or differs from the others in
    machine, build, scene, configuration or image hash.
    """
    if not docs:
        raise TappError('no results files given')
    for doc, label in zip(docs, labels):
        check_app_file(doc, label)
    check_same_run(docs, labels)

    totals = [doc['app']['total_ms'] for doc in docs]
    stats = rs.summarize(totals)
    median_index = stats['median_run_index']
    median_run = docs[median_index]['app']

    warnings = []
    if len(docs) != DEFAULT_PROCESSES:
        warnings.append(f'{len(docs)} processes given; BENCHMARKING.md reports the median of '
                        f'{DEFAULT_PROCESSES}')
    if stats['spread'] > SPREAD_WARNING:
        warnings.append(f"spread {stats['spread']:.1%} is above {SPREAD_WARNING:.0%}; rerun and "
                        'look into it (BENCHMARKING.md)')

    return {
        'quantity': 'T_app',
        'processes': len(docs),
        'median_ms': stats['median_ms'],
        'min_ms': stats['min_ms'],
        'max_ms': stats['max_ms'],
        'spread': stats['spread'],
        'median_run_index': median_index,
        'median_run': {
            'file': labels[median_index],
            'total_ms': median_run['total_ms'],
            'components': median_run['components'],
            'unattributed_ms': median_run['unattributed_ms'],
        },
        'totals_ms': totals,
        'files': list(labels),
        'scene_hash': docs[0]['scene']['hash'],
        'machine': docs[0]['machine'],
        'build': docs[0]['build'],
        'config': docs[0]['config'],
        'note': CACHE_NOTE,
        'warnings': warnings,
    }


def format_report(summary):
    """Human-readable report for `summary` (from aggregate)."""
    run = summary['median_run']
    machine = summary['machine']
    lines = [
        f"T_app over {summary['processes']} processes",
        f"  scene {summary['scene_hash']}, host {machine['host'] or '?'}, "
        f"commit {summary['build']['commit'][:12]}{' (dirty)' if summary['build']['dirty'] else ''}",
        f"  median {summary['median_ms']:.3f} ms  min {summary['min_ms']:.3f} ms  "
        f"max {summary['max_ms']:.3f} ms  spread {summary['spread']:.1%}",
        '  totals (ms): ' + ', '.join(f'{t:.3f}' for t in summary['totals_ms']),
        f"  median run is #{summary['median_run_index']} ({run['file']}); its components:",
    ]
    for name, value in run['components'].items():
        if value != 0.0:
            lines.append(f'    {name:<10} {value:10.3f} ms')
    lines.append(f"    {'unattributed':<10} {run['unattributed_ms']:10.3f} ms")
    lines.append(f"  note: {summary['note']}")
    for warning in summary['warnings']:
        lines.append(f'  warning: {warning}')
    return '\n'.join(lines)


# Options the script sets itself for every process. Passing them through would make the files
# something other than five app-mode measurements.
OWNED_OPTIONS = ('--mode', '--results', '--runs')


def check_passthrough(extra):
    """Raises TappError if `extra` (the rt_render arguments after `--`) sets an option we own."""
    for argument in extra:
        for option in OWNED_OPTIONS:
            if argument == option or argument.startswith(option + '='):
                raise TappError(f'{option} is set by run_tapp.py; remove it from the rt_render '
                                'arguments')


def run_processes(rt_render, out_dir, processes, extra, log=sys.stderr):
    """Runs `rt_render --mode app` in `processes` fresh processes, one after another.

    Each process writes its own results file, `tapp_<i>.json` in `out_dir`. A stale file of that
    name is removed first, so a process that fails to write one cannot be mistaken for it.
    Returns the list of result paths. Raises TappError if a process exits non-zero, prints to
    stdout (INTERFACES section 0: progress goes to stderr, results to files), or leaves no file.
    """
    check_passthrough(extra)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for i in range(processes):
        path = out_dir / f'tapp_{i}.json'
        path.unlink(missing_ok=True)
        command = [str(rt_render), '--mode', 'app', '--results', str(path), *extra]
        print(f'process {i + 1}/{processes}: {" ".join(command)}', file=log)
        try:
            result = subprocess.run(command, capture_output=True, text=True)
        except OSError as error:
            raise TappError(f'cannot run {rt_render}: {error.strerror}') from error
        if result.returncode != 0:
            tail = result.stderr.strip().splitlines()[-3:]
            raise TappError(f'process {i + 1} exited with status {result.returncode}'
                            + (': ' + ' | '.join(tail) if tail else ''))
        if result.stdout:
            raise TappError(f'process {i + 1} wrote to stdout; rt_render must report on stderr '
                            f'and in files: {result.stdout.strip()[:80]!r}')
        if not path.is_file():
            raise TappError(f'process {i + 1} exited 0 but wrote no results file at {path}')
        paths.append(str(path))
    return paths


def load_all(paths):
    docs = []
    for path in paths:
        try:
            docs.append(rs.load_results(path))
        except rs.ResultsError as error:
            raise TappError(str(error)) from error
    return docs


def build_parser():
    parser = argparse.ArgumentParser(
        description='Report the median T_app, with spread, from rt_render results files.')
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--from', dest='sources', nargs='+', metavar='RESULTS.json',
                        help='results files written by rt_render --mode app, one per process')
    source.add_argument('--rt-render', metavar='BIN',
                        help='run this rt_render in fresh processes; its arguments follow `--`')
    parser.add_argument('--processes', type=int, default=DEFAULT_PROCESSES, metavar='N',
                        help=f'with --rt-render: number of processes (default {DEFAULT_PROCESSES})')
    parser.add_argument('--out-dir', metavar='DIR',
                        help='with --rt-render: where the results files go (required)')
    parser.add_argument('--summary', metavar='FILE', help='also write the summary as JSON')
    return parser


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    extra = []
    if '--' in argv:
        split = argv.index('--')
        argv, extra = argv[:split], argv[split + 1:]
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.sources:
        if extra or args.out_dir or args.processes != DEFAULT_PROCESSES:
            parser.error('--from cannot be combined with --out-dir, --processes or rt_render '
                         'arguments')
    else:
        if not args.out_dir:
            parser.error('--rt-render needs --out-dir')
        if args.processes < 1:
            parser.error('--processes must be at least 1')
    try:
        paths = args.sources or run_processes(args.rt_render, args.out_dir, args.processes, extra)
        docs = load_all(paths)
        summary = aggregate(docs, [str(p) for p in paths])
    except TappError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    print(format_report(summary))
    if args.summary:
        try:
            with open(args.summary, 'w', encoding='utf-8') as handle:
                json.dump(summary, handle, indent=2)
                handle.write('\n')
        except OSError as error:
            print(f'error: cannot write {args.summary}: {error.strerror}', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
