#!/usr/bin/env python3
"""Summarize a sweep.csv: CPU* and GPU* selection, the three target ratios, and the T_br
breakdown, as Markdown tables (docs/BENCHMARKING.md "Targets (from proposal)").

    ratios.py sweep.csv [--bvh lbvh] [--width 1920]

CPU* is the CPU configuration (B = R) with the lowest median of the quantity being reported,
chosen per scene and per quantity among the thread counts the sweep measured. GPU* is chosen the
same way among the measured build-thread counts. A size measured at only two thread counts gets
a CPU* from those two, and the table says how many were tested. Only one-light rows count toward the
ratios; the `lights` preset's rows get their own table. These are preliminary ratios
straight from a sweep; the final report reruns the selected configurations interleaved.

Exit status: 0 on success, 1 for an unreadable or empty file.
"""
import argparse
import csv
import sys

QUANTITIES = ('T_br', 'T_frame', 'T_app')


def load(path):
    with open(path, newline='') as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError(f'{path}: no rows')
    for r in rows:
        for key in ('median_ms', 'min_ms', 'max_ms', 'spread'):
            r[key] = float(r[key])
        for key in ('build_threads', 'render_threads', 'width', 'height', 'num_primitives'):
            r[key] = int(r[key])
        r['num_lights'] = int(r.get('num_lights') or 1)  # sweeps before the column: 1 light
    return rows


def pick(rows, scene, quantity, backend, bvh, width, lights=1):
    return [r for r in rows if r['scene'] == scene and r['quantity'] == quantity
            and r['backend'] == backend and r['bvh'] == bvh and r['width'] == width
            and r['num_lights'] == lights]


def best(points):
    return min(points, key=lambda r: r['median_ms']) if points else None


def at(points, threads):
    return next((r for r in points if r['build_threads'] == threads), None)


def scenes(rows, bvh, width):
    found = {r['scene']: r['num_primitives'] for r in rows
             if r['bvh'] == bvh and r['width'] == width and r['backend'] == 'cuda'}
    return sorted(found, key=found.get)


def ratio(a, b):
    return a['median_ms'] / b['median_ms'] if a and b else float('nan')


def ratio_table(rows, bvh, width):
    out = ['| Scene | Quantity | CPU 1 core (ms) | CPU\\* (ms, threads, tested) | GPU\\* (ms, B) '
           '| 1 core / CPU\\* | CPU\\* / GPU\\* | 1 core / GPU\\* |',
           '|---|---|---:|---:|---:|---:|---:|---:|']
    for s in scenes(rows, bvh, width):
        for q in QUANTITIES:
            cpu = pick(rows, s, q, 'cpu', bvh, width)
            gpu = pick(rows, s, q, 'cuda', bvh, width)
            one, cs, gs = at(cpu, 1), best(cpu), best(gpu)
            if not (one and cs and gs):
                continue
            out.append(
                f"| {s} | {q} | {one['median_ms']:.1f} | {cs['median_ms']:.1f} "
                f"({cs['build_threads']}, {len(cpu)}) | {gs['median_ms']:.1f} "
                f"({gs['build_threads']}) | {ratio(one, cs):.1f}x | {ratio(cs, gs):.2f}x "
                f"| {ratio(one, gs):.1f}x |")
    return out


def breakdown_table(rows, scene, bvh, width):
    """T_br components of the median run, for every measured thread count at one scene."""
    out = ['| Backend | B (R) | Total | bounds | build | layout | alloc+upload | render '
           '| download |', '|---|---|---:|---:|---:|---:|---:|---:|---:|']
    points = pick(rows, scene, 'T_br', 'cpu', bvh, width) + \
        pick(rows, scene, 'T_br', 'cuda', bvh, width)
    for r in sorted(points, key=lambda r: (r['backend'], r['build_threads'])):
        def c(name):
            return float(r[f'{name}_ms'] or 0.0)
        threads = (f"{r['build_threads']} ({r['render_threads']})" if r['backend'] == 'cpu'
                   else str(r['build_threads']))
        out.append(f"| {r['backend']} | {threads} | {r['median_ms']:.1f} | {c('bounds'):.1f} "
                   f"| {c('build'):.1f} | {c('layout'):.1f} | {c('alloc') + c('upload'):.1f} "
                   f"| {c('render'):.1f} | {c('download'):.1f} |")
    return out


def lights_table(rows, scene, bvh, width):
    """T_frame and T_br against light count, at the thread count the `lights` preset used."""
    counts = sorted({r['num_lights'] for r in rows if r['scene'] == scene})
    if len(counts) < 2:
        return []
    out = ['| Backend | Quantity | ' + ' | '.join(f'L = {n} (ms)' for n in counts) + ' |',
           '|---|---|' + '---:|' * len(counts)]
    for backend in ('cpu', 'cuda'):
        for q in ('T_frame', 'T_br'):
            # The thread count measured at every light count (the preset's top count).
            by_count = [{r['build_threads']: r for r in pick(rows, scene, q, backend, bvh, width, n)}
                        for n in counts]
            common = set.intersection(*(set(d) for d in by_count))
            if not common:
                continue
            b = max(common)
            out.append(f'| {backend} B{b} | {q} | ' +
                       ' | '.join(f"{d[b]['median_ms']:.1f}" for d in by_count) + ' |')
    return out


def spread_warnings(rows):
    return [f"{r['scene']} {r['quantity']} {r['backend']} B{r['build_threads']} "
            f"{r['bvh']} {r['width']}{' L' + str(r['num_lights']) if r['num_lights'] != 1 else ''}"
            f": spread {r['spread']:.0%}"
            for r in rows if r['spread'] > 0.10]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('csv')
    parser.add_argument('--bvh', default='lbvh')
    parser.add_argument('--width', type=int, default=1920)
    args = parser.parse_args(argv)
    try:
        rows = load(args.csv)
    except (OSError, ValueError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    print(f'### Target ratios ({args.bvh}, {args.width} px wide)\n')
    print('\n'.join(ratio_table(rows, args.bvh, args.width)))
    for s in scenes(rows, args.bvh, args.width):
        if len(pick(rows, s, 'T_br', 'cpu', args.bvh, args.width)) > 2:
            print(f'\n### T_br breakdown, {s} (ms, median run)\n')
            print('\n'.join(breakdown_table(rows, s, args.bvh, args.width)))
    for s in scenes(rows, args.bvh, args.width):
        table = lights_table(rows, s, args.bvh, args.width)
        if table:
            print(f'\n### Light count, {s} (ms, median)\n')
            print('\n'.join(table))
    warnings = spread_warnings(rows)
    if warnings:
        print('\n### Spread above 10%\n')
        print('\n'.join(f'- {w}' for w in warnings))
    return 0


if __name__ == '__main__':
    sys.exit(main())
