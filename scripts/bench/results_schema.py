"""Validator for results schema v2 (INTERFACES section 8, CONTRACT_NOTES W10).

A stdlib-only mirror of ``rt::validate_results`` in ``src/bench/results.cpp`` and of
``rt::summarize`` in ``src/bench/stats.cpp``. The C++ validator is the authority; the shared
fixtures in ``tests/data/bench`` are checked by both (``tests/test_results_fixtures.cpp`` and
``tests/test_run_tapp.py``), so the two cannot drift apart on those cases.
"""
import json
import math

SCHEMA_VERSION = 2

FRAME_COMPONENTS = ('render', 'download')
BUILD_RENDER_COMPONENTS = ('bounds', 'build', 'layout', 'convert', 'alloc', 'upload', 'render',
                           'download')
APP_COMPONENTS = ('args', 'load', 'cuda_init', 'bounds', 'build', 'layout', 'convert', 'alloc',
                  'upload', 'render', 'download')


class ResultsError(ValueError):
    """The results file does not follow schema v2. The message names the first bad field."""


def _reject_constant(name):
    # Python accepts NaN and Infinity, but they are not JSON and the C++ parser rejects them.
    raise ResultsError(f'results: invalid JSON constant {name}')


def load_results(path):
    """Reads a results file and returns the parsed object (not yet validated).

    Raises ResultsError for unreadable or malformed JSON, including NaN and Infinity.
    """
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle, parse_constant=_reject_constant)
    except OSError as error:
        raise ResultsError(f'results: cannot read {path}: {error.strerror}') from error
    except json.JSONDecodeError as error:
        raise ResultsError(f'results: {path} is not valid JSON: {error}') from error


def _fail(path, problem):
    raise ResultsError(f'results: {path}: {problem}')


def _join(path, key):
    return key if not path else f'{path}.{key}'


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _member(obj, path, key):
    if not isinstance(obj, dict):
        _fail(path, 'expected an object')
    if key not in obj:
        _fail(_join(path, key), 'missing')
    return obj[key]


def _object(parent, path, key):
    value = _member(parent, path, key)
    if not isinstance(value, dict):
        _fail(_join(path, key), 'expected an object')
    return value


def _array(parent, path, key):
    value = _member(parent, path, key)
    if not isinstance(value, list):
        _fail(_join(path, key), 'expected an array')
    return value


def _string(parent, path, key, non_empty=False):
    value = _member(parent, path, key)
    if not isinstance(value, str):
        _fail(_join(path, key), 'expected a string')
    if non_empty and not value:
        _fail(_join(path, key), 'must not be empty')
    return value


def _bool(parent, path, key):
    value = _member(parent, path, key)
    if not isinstance(value, bool):
        _fail(_join(path, key), 'expected true or false')
    return value


def _uint(parent, path, key):
    value = _member(parent, path, key)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        _fail(_join(path, key), 'expected a non-negative integer')
    return value


def _number(parent, path, key):
    value = _member(parent, path, key)
    if not _is_number(value):
        _fail(_join(path, key), 'expected a number')
    if not math.isfinite(value):
        _fail(_join(path, key), 'not finite')
    return float(value)


def _time(parent, path, key):
    ms = _number(parent, path, key)
    if ms < 0.0:
        _fail(_join(path, key), 'negative time')
    return ms


def _check_hex(parent, path, key):
    text = _string(parent, path, key)
    ok = len(text) == 18 and text.startswith('0x') and all(c in '0123456789abcdef' for c in text[2:])
    if not ok:
        _fail(_join(path, key), f'expected 0x and 16 lower-case hex digits, got "{text}"')


def _near(a, b):
    """Same tolerance as ``near`` in results.cpp."""
    return abs(a - b) <= 1e-9 * max(1.0, abs(a), abs(b))


def _check_vec3(parent, path, key):
    vec = _array(parent, path, key)
    if len(vec) != 3:
        _fail(_join(path, key), 'expected 3 numbers')
    for x in vec:
        if not _is_number(x) or not math.isfinite(x):
            _fail(_join(path, key), 'expected 3 finite numbers')


def summarize(totals_ms):
    """Median, min, max, spread and median run index, as ``rt::summarize`` computes them.

    For an even count the median is the mean of the two middle totals and the median run is the
    one with the lower of them. Equal totals keep run order.
    """
    if not totals_ms:
        raise ResultsError('bench: cannot summarize zero runs')
    for i, total in enumerate(totals_ms):
        if not math.isfinite(total) or total < 0.0:
            raise ResultsError(f'bench: run {i} has total {total} ms, expected a finite, '
                               'non-negative time')
    order = sorted(range(len(totals_ms)), key=lambda i: totals_ms[i])  # sorted() is stable
    n = len(order)
    lower = (n - 1) // 2
    lo_mid = totals_ms[order[lower]]
    hi_mid = totals_ms[order[n // 2]]
    median = lo_mid if n % 2 == 1 else lo_mid + (hi_mid - lo_mid) / 2.0
    low, high = totals_ms[order[0]], totals_ms[order[-1]]
    return {
        'median_ms': median,
        'min_ms': low,
        'max_ms': high,
        'spread': (high - low) / median if median > 0.0 else 0.0,
        'median_run_index': order[lower],
    }


def _check_run(run, path, components_wanted, diagnostics):
    total = _time(run, path, 'total_ms')
    components = _object(run, path, 'components')
    components_path = _join(path, 'components')
    total_of_components = 0.0
    for name in components_wanted:
        total_of_components += _time(components, components_path, name)
    for name in components:
        if name == 'render_kernel':
            _fail(_join(components_path, name),
                  'a diagnostic overlaps render; it belongs in diagnostics')
        if name not in components_wanted:
            _fail(_join(components_path, name), 'unknown component')
    if diagnostics:
        diag = _object(run, path, 'diagnostics')
        diag_path = _join(path, 'diagnostics')
        for name in diag:
            if name != 'render_kernel':
                _fail(_join(diag_path, name), 'unknown diagnostic')
            _time(diag, diag_path, 'render_kernel')
    elif 'diagnostics' in run:
        _fail(_join(path, 'diagnostics'), 'the app section has no diagnostics')
    unattributed = _number(run, path, 'unattributed_ms')
    if not _near(unattributed, total - total_of_components):
        _fail(_join(path, 'unattributed_ms'),
              f'expected total_ms - sum of components = {total - total_of_components}, '
              f'got {unattributed}')
    return total


def _check_run_set(section, path, components_wanted):
    runs = _array(section, path, 'runs')
    if not runs:
        _fail(_join(path, 'runs'), 'expected at least one run')
    totals = [_check_run(run, f'{path}.runs[{i}]', components_wanted, True)
              for i, run in enumerate(runs)]
    stats = summarize(totals)
    for key in ('median_ms', 'min_ms', 'max_ms', 'spread'):
        actual = _time(section, path, key)
        if not _near(actual, stats[key]):
            _fail(_join(path, key), f'the runs give {stats[key]}, got {actual}')
    index = _uint(section, path, 'median_run_index')
    if index >= len(runs):
        _fail(_join(path, 'median_run_index'), 'no such run')
    if index != stats['median_run_index']:
        _fail(_join(path, 'median_run_index'),
              f"the runs give {stats['median_run_index']}, got {index}")


def validate(results):
    """Checks that ``results`` (a parsed JSON value) follows schema v2.

    Raises ResultsError naming the first field that fails, in the form the C++ validator uses,
    for example ``results: app.components.render: missing``.
    """
    if not isinstance(results, dict):
        _fail('', 'expected a JSON object')
    if _uint(results, '', 'schema_version') != SCHEMA_VERSION:
        _fail('schema_version', f'expected {SCHEMA_VERSION}')

    machine = _object(results, '', 'machine')
    for key in ('host', 'cpu_model', 'gpu_name', 'gpu_uuid', 'driver', 'cuda_runtime', 'nvcc',
                'gcc', 'os'):
        _string(machine, 'machine', key)
    _uint(machine, 'machine', 'cpu_cores')

    build = _object(results, '', 'build')
    _string(build, 'build', 'commit')
    _bool(build, 'build', 'dirty')
    _string(build, 'build', 'cmake_flags')

    scene = _object(results, '', 'scene')
    _string(scene, 'scene', 'json')
    _check_hex(scene, 'scene', 'hash')
    _string(scene, 'scene', 'generator_command')
    spheres = _uint(scene, 'scene', 'num_spheres')
    triangles = _uint(scene, 'scene', 'num_triangles')
    if _uint(scene, 'scene', 'num_primitives') != spheres + triangles:
        _fail('scene.num_primitives',
              f'expected num_spheres + num_triangles = {spheres + triangles}')
    _uint(scene, 'scene', 'num_lights')

    config = _object(results, '', 'config')
    for key in ('backend', 'bvh', 'query', 'shade'):
        _string(config, 'config', key, non_empty=True)
    _string(config, 'config', 'pipeline')
    for key in ('max_leaf_size', 'build_threads', 'render_threads', 'width', 'height'):
        _uint(config, 'config', key)
    for aov in _array(config, 'config', 'aovs'):
        if not isinstance(aov, str):
            _fail('config.aovs', 'expected strings')
    _bool(config, 'config', 'counters')
    camera = _object(config, 'config', 'camera')
    for key in ('position', 'look_at', 'up'):
        _check_vec3(camera, 'config.camera', key)
    _number(camera, 'config.camera', 'vfov_deg')
    for name in _array(camera, 'config.camera', 'overridden'):
        if not isinstance(name, str):
            _fail('config.camera.overridden', 'expected strings')
    mem = _object(config, 'config', 'mem')
    _string(mem, 'config.mem', 'mode', non_empty=True)
    _string(mem, 'config.mem', 'advice', non_empty=True)
    for key in ('prefetch', 'realloc_per_prepare', 'geometry_reused'):
        _bool(mem, 'config.mem', key)
    fresh = _member(mem, 'config.mem', 'fresh_pages')
    if fresh is not None and not isinstance(fresh, bool):
        _fail('config.mem.fresh_pages', 'expected true, false or null')

    measured = False
    if 'app' in results:
        measured = True
        app = _object(results, '', 'app')
        _check_run(app, 'app', APP_COMPONENTS, False)
    if 'build_and_render' in results:
        measured = True
        _check_run_set(_object(results, '', 'build_and_render'), 'build_and_render',
                       BUILD_RENDER_COMPONENTS)
    if 'frame' in results:
        measured = True
        _check_run_set(_object(results, '', 'frame'), 'frame', FRAME_COMPONENTS)
    if not measured:
        _fail('', 'expected at least one of app, build_and_render or frame')

    if 'bvh_stats' in results:
        bvh = _object(results, '', 'bvh_stats')
        for key in ('nodes', 'leaves', 'depth', 'fallbacks'):
            _uint(bvh, 'bvh_stats', key)
        _time(bvh, 'bvh_stats', 'sah_cost')

    image = _object(results, '', 'image')
    _string(image, 'image', 'path')
    _check_hex(image, 'image', 'rgb_hash')
