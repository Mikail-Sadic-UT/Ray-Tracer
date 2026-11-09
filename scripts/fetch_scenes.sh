#!/usr/bin/env bash
# Retrieve an immutable scene set and verify archive hashes and scene/content hashes.
set -euo pipefail
if [[ $# != 1 ]]; then
  echo "usage: scripts/fetch_scenes.sh <set>" >&2
  exit 1
fi
rt_repo_root="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$rt_repo_root" "$1" <<'PY'
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

root, scene_set = Path(sys.argv[1]), sys.argv[2]

def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def main():
    manifest = Path(os.environ.get('RT_SCENE_MANIFEST', root / 'scenes/manifest.json'))
    if not manifest.is_file():
        raise ValueError(f'{manifest}: no published scene manifest; generate/validate and publish the canonical set first')
    entries = json.loads(manifest.read_text())
    if isinstance(entries, dict):
        entries = entries['scenes']
    entries = [entry for entry in entries if entry['set'] == scene_set]
    if not entries:
        raise ValueError(f'no manifest entries for {scene_set}')
    candidates = [root / b / 'apps/scene_dump/scene_dump' for b in ('build', 'build-cpu', 'build-mac', 'build-vista')]
    dump = os.environ.get('RT_SCENE_DUMP') or shutil.which('scene_dump')
    dump = dump or next((str(p) for p in candidates if p.is_file()), None)
    if not dump:
        raise ValueError('build scene_dump first or set RT_SCENE_DUMP to its executable')
    destination = Path(os.environ.get('RT_SCENE_DEST', root / 'scenes/generated'))
    destination.mkdir(parents=True, exist_ok=True)
    # Verify the entire set in staging before installing any scene files.
    with tempfile.TemporaryDirectory(prefix='rt-scenes-', dir=destination) as staging_name:
        staging = Path(staging_name)
        seen = set()
        for entry in entries:
            archive = entry['archive']
            if archive['release'] != scene_set:
                raise ValueError('manifest set and archive release differ')
            for file in entry['files']:
                name = file['path']
                if Path(name).name != name or name in ('', '.', '..') or name in seen:
                    raise ValueError(f'invalid or duplicate archive filename: {name}')
                seen.add(name)
                urls = archive['urls']
                url = next((u for u in urls if Path(urllib.parse.unquote(urllib.parse.urlparse(u).path)).name == name), None)
                if url is None:
                    raise ValueError(f'no archive URL for {name}')
                target = staging / name
                cached = destination / name
                if cached.is_file() and sha256(cached) == file['sha256']:
                    shutil.copyfile(cached, target)
                else:
                    parsed = urllib.parse.urlparse(url)
                    parts = parsed.path.strip('/').split('/')
                    if shutil.which('gh') and parsed.netloc == 'github.com' and len(parts) >= 6 and parts[2:4] == ['releases', 'download']:
                        subprocess.run(['gh', 'release', 'download', scene_set, '--repo', '/'.join(parts[:2]),
                                        '--pattern', name, '--dir', str(staging)], check=True)
                    else:
                        with urllib.request.urlopen(url, timeout=60) as response, target.open('wb') as output:
                            shutil.copyfileobj(response, output)
                if target.stat().st_size != file['bytes'] or sha256(target) != file['sha256']:
                    raise ValueError(f'{name}: size or SHA-256 mismatch')
                if name.endswith('.bin.gz'):
                    unpacked = target.with_suffix('')
                    with gzip.open(target, 'rb') as source, unpacked.open('wb') as output:
                        shutil.copyfileobj(source, output)
                    if sha256(unpacked) != file['uncompressed_sha256']:
                        raise ValueError(f'{unpacked.name}: uncompressed SHA-256 mismatch')
            scene_json = next((staging / f['path'] for f in entry['files'] if f['path'].endswith('.json')), None)
            if scene_json is None:
                raise ValueError(f'{entry["name"]}: no scene JSON asset')
            data = json.loads(scene_json.read_text())
            if str(data.get('generator', {}).get('terrain_source', '')).startswith('stub-'):
                raise ValueError('refusing a development terrain stub as a published scene set')
            checked = subprocess.run([dump, '--verify', str(scene_json)], check=True, capture_output=True, text=True)
            for field, label in (('content_hash', 'content_hash'), ('scene_hash', 'scene_hash')):
                if f'{label}: {entry[field].lower()}' not in checked.stdout.lower():
                    raise ValueError(f'{entry["name"]}: {field} differs from manifest')
        for path in staging.iterdir():
            os.replace(path, destination / path.name)
    print(f'Verified and retrieved {len(entries)} scenes from {scene_set}', file=sys.stderr)

try:
    main()
except Exception as error:
    print(f'fetch_scenes: {error}', file=sys.stderr)
    sys.exit(1)
PY
