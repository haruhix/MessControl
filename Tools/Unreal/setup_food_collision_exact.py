"""Install pinned offline-planner wheels under Saved, without global changes."""
from __future__ import annotations
import argparse
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys


def main():
    here = Path(__file__).resolve().parent
    lock = json.loads((here/'food_collision_exact_dependencies.json').read_text())
    version = '.'.join(map(str, sys.version_info[:3]))
    if version != lock['python'] or platform.python_implementation() != lock['implementation']:
        raise RuntimeError(f"Use the pinned {lock['implementation']} {lock['python']}; got {version}")
    if sys.platform != 'win32' or platform.machine().lower() not in ('amd64', 'x86_64'):
        raise RuntimeError('The checked native Tetgen wheel requires Windows x64')
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', type=Path, default=Path('Saved/FoodCollisionProbe/ExactPythonDeps'))
    args = parser.parse_args()
    saved_root = here.parents[1]/'Saved'
    if not args.target.resolve().is_relative_to(saved_root.resolve()):
        raise RuntimeError('Exact planner dependencies must remain inside the project Saved directory')
    args.target.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, '-m', 'pip', 'install', '--only-binary=:all:', '--no-deps',
                    '--target', str(args.target), '--upgrade',
                    '-r', str(here/'requirements-food-collision-exact.txt')], check=True)
    native = args.target/'tetgen/_tetgen.pyd'
    digest = hashlib.sha256(native.read_bytes()).hexdigest()
    if digest != lock['tetgen_native_sha256']:
        raise RuntimeError('Installed Tetgen native binary differs from the checked pinned wheel')
    manifest = {'python': version, 'platform': lock['platform'], 'packages': lock['packages'],
                'tetgen_native_sha256': digest, 'source_lock': str(here/'food_collision_exact_dependencies.json')}
    (args.target/'food_collision_exact_environment.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('Pinned exact planner dependencies ready:', args.target.resolve())


if __name__ == '__main__':
    main()
