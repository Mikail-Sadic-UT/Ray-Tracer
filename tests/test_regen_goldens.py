"""Golden regeneration must reject renderers from another or dirty source tree."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(sys.argv.pop(1)).resolve()


class ProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.git('init', '-q')
        self.git('-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                 'commit', '--allow-empty', '-qm', 'fixture')
        self.commit = self.git('rev-parse', 'HEAD').stdout.strip()
        self.renderer = self.root / 'render'
        self.renderer.write_text('''#!/usr/bin/env python3
import json
from pathlib import Path
import sys
args = sys.argv[1:]
out = Path(args[args.index('--pfm') + 1])
for suffix in ('.pfm', '.primid.u32img', '.lightmask.u32img'):
    out.with_suffix(suffix).write_bytes(b'deterministic fixture')
Path(args[args.index('--results') + 1]).write_text(json.dumps({
    'build': json.loads(Path(__file__).with_name('identity.json').read_text())}))
''')
        self.renderer.chmod(0o755)

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, check=True,
                              capture_output=True, text=True)

    def regenerate(self, commit, dirty):
        (self.root / 'identity.json').write_text(json.dumps({'commit': commit, 'dirty': dirty}))
        return subprocess.run(['bash', str(SCRIPT), '--render', str(self.renderer),
                               '--reason', 'test', '--out', str(self.root / 'goldens')],
                              cwd=self.root, capture_output=True, text=True)

    def test_rejects_stale_dirty_and_unknown_builds(self):
        for commit, dirty in [('0' * 40, False), (self.commit, True), ('unknown', False)]:
            with self.subTest(commit=commit, dirty=dirty):
                result = self.regenerate(commit, dirty)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('rebuild rt_render', result.stderr)
                self.assertFalse((self.root / 'goldens').exists())

    def test_accepts_matching_clean_build(self):
        result = self.regenerate(self.commit, False)
        self.assertEqual(result.returncode, 0, result.stderr)
        provenance = (self.root / 'goldens/PROVENANCE.txt').read_text()
        self.assertIn('commit: ' + self.commit, provenance)
        self.assertEqual(len(list((self.root / 'goldens').glob('known_*'))), 9)


if __name__ == '__main__':
    unittest.main()
