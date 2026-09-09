#!/usr/bin/env python3
"""Verify default XDG selection in isolated, receipt-bound browser processes."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import tomllib

import environment_smoke as native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-receipt', type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid() == 0 or not os.environ.get('DISPLAY'):
        parser.error('non-root X11 desktop required')
    _, binary, branding = native.validate_receipt(args.build_receipt.resolve())
    root = Path(tempfile.mkdtemp(prefix='default-config-', dir=native.ROOT / '.build/test-evidence'))
    for name in ('home', 'xdg-config', 'xdg-data', 'xdg-state', 'xdg-cache'):
        (root / name).mkdir(mode=0o700)
    env = native.child_env(root)
    identity = branding['profile_directory_name']
    config = root / 'xdg-config' / identity / 'config.toml'
    state = root / 'xdg-state' / identity / 'state.toml'
    personal = root / 'xdg-data' / identity / 'environments/personal'
    work = root / 'xdg-data' / identity / 'environments/work'
    owned = []
    result = {'status': 'failed', 'build_receipt': str(args.build_receipt),
              'evidence': str(root), 'checks': [], 'cleanup': []}

    def start(label, switches=(), activation=False):
        command = [str(binary), '--ozone-platform=x11', '--no-first-run',
                   '--no-default-browser-check', *switches, 'about:blank']
        with (root / (label + '.log')).open('wb') as log:
            process = subprocess.Popen(command, env=env, cwd=root, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        owned.append(process)
        if activation:
            native.finish_naturally(process, 60)
        else:
            native.wait_until(lambda: native.x11_windows(process.pid), label, 60)
            if Path(f'/proc/{process.pid}/exe').resolve() != binary.resolve():
                raise RuntimeError('owned executable mismatch')
        return process

    def stop(process):
        if native.peek(process) is not None:
            raise RuntimeError('browser exited before native session shutdown')
        os.kill(process.pid, signal.SIGTERM)
        native.finish_naturally(process, 60)

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        result['checks'].append(name)

    try:
        first = start('bootstrap')
        check(config.is_file() and config.stat().st_mode & 0o777 == 0o600,
              'private default config created')
        parsed = tomllib.loads(config.read_text())
        check(parsed['environments']['personal']['data_directory'] == str(personal),
              'default root honors XDG_DATA_HOME')
        check(native.lock_owner(personal / 'SingletonLock')[0] == first.pid,
              'default personal root owns native singleton')
        check(tomllib.loads(state.read_text())['last_environment'] == 'personal',
              'normal selection remembered')
        stop(first)
        with config.open('a') as stream:
            stream.write('\n[environments.work]\ndata_directory = ' +
                         json.dumps(str(work)) + '\nstartup_urls = []\n')
        worker = start('work', ['--environment=work'])
        check(native.lock_owner(work / 'SingletonLock')[0] == worker.pid,
              'explicit environment overrides default')
        remembered = state.read_bytes()
        check(tomllib.loads(remembered.decode())['last_environment'] == 'work',
              'work selection remembered')
        start('restore-work', activation=True)
        check(native.peek(worker) is None and
              native.lock_owner(work / 'SingletonLock')[0] == worker.pid,
              'default launch activates remembered work owner')
        private = start('private-personal', ['--environment=personal', '--incognito'])
        check(state.read_bytes() == remembered, 'incognito leaves remembered selection unchanged')
        stop(private)
        check(state.read_bytes() == remembered, 'private shutdown leaves selection unchanged')
        stop(worker)
        result['status'] = 'passed'
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        result['error'] = str(error)
    finally:
        result['cleanup'] = [native.kill_group(process) for process in owned]
        if any(item.get('owned') for item in result['cleanup']):
            result['status'] = 'failed'
            result.setdefault('error', 'forced cleanup required')
        (root / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
