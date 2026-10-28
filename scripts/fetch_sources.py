"""Fetch locked source revisions without resetting existing checkouts."""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = Path(os.environ.get('PORT_SOURCES_DIR', str(ROOT / 'sources'))).resolve()


def git(path, *args, capture=False):
    command = ['git', '-C', str(path), *args]
    if capture:
        return subprocess.check_output(command, text=True).strip()
    subprocess.run(command, check=True)


def main():
    for item in json.loads((ROOT / 'dependencies.json').read_text()):
        destination = (SOURCES / item['path']).resolve()
        if not destination.is_relative_to(SOURCES):
            raise SystemExit('Dependency path escapes source directory')
        if destination.exists() and any(destination.iterdir()):
            if not (destination / '.git').exists():
                raise SystemExit(f'Existing non-checkout directory: {destination}')
            if git(destination, 'rev-parse', 'HEAD', capture=True) != item['revision']:
                raise SystemExit(f'Wrong revision in {destination}; preserve or relocate it first')
            if git(destination, 'remote', 'get-url', 'origin', capture=True) != item['url']:
                raise SystemExit(f'Unexpected origin URL in {destination}')
            print(f'Using {item["path"]} at {item["revision"]}')
            continue
        destination.mkdir(parents=True, exist_ok=True)
        git(destination, 'init')
        git(destination, 'config', 'core.autocrlf', 'false')
        git(destination, 'remote', 'add', 'origin', item['url'])
        options = ['--depth', '1']
        if item.get('sparse'):
            options += ['--filter=blob:none']
            git(destination, 'config', 'remote.origin.promisor', 'true')
            git(destination, 'config', 'remote.origin.partialclonefilter', 'blob:none')
            git(destination, 'sparse-checkout', 'init', '--cone')
            git(destination, 'sparse-checkout', 'set', *item['sparse'])
        git(destination, 'fetch', *options, 'origin', item['revision'])
        git(destination, 'checkout', '--detach', 'FETCH_HEAD')


if __name__ == '__main__':
    main()
