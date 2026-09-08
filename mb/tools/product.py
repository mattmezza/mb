#!/usr/bin/env python3
"""Stage the reviewed product overlay and run reproducible Chromium build steps."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import tomllib

import branding
import branding_assets
import branding_strings
import upstream

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.build'
SOURCE = BUILD / 'chromium/src'
DEPOT = BUILD / 'depot_tools'
RECEIPT = BUILD / 'product-integration.json'
OUTPUT = SOURCE / 'out/mb-debug'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_relative(name):
    if not isinstance(name, str) or not name or '\\' in name:
        raise RuntimeError(f'Unsafe integration path: {name!r}')
    path = Path(name)
    if path.is_absolute() or any(p in ('', '.', '..') for p in name.split('/')):
        raise RuntimeError(f'Unsafe integration path: {name!r}')
    return path


def regular_destination(root, name):
    path = root / safe_relative(name)
    for item in (path, *path.parents):
        if item == root:
            break
        if item.is_symlink():
            raise RuntimeError(f'Refusing symlink in integration destination: {item}')
    if path.exists() and not path.is_file():
        raise RuntimeError(f'Integration destination is not a regular file: {path}')
    return path


def check_identity():
    pins = tomllib.loads((ROOT / 'mb/upstream-version.toml').read_text())
    for key, repo in (('chromium', SOURCE), ('depot_tools', DEPOT)):
        if upstream.git(repo, 'rev-parse', 'HEAD') != pins[key]['commit']:
            raise RuntimeError(f'{key}: checkout differs from pin; follow upstream update procedure')
    if upstream.git(DEPOT, 'status', '--porcelain', '--untracked-files=normal'):
        raise RuntimeError('depot_tools has local changes')
    return pins


def verify_gate(path, pins):
    review = json.loads(path.read_text())
    if review.get('schema_version') != 1 or review.get('result') != 'passed':
        raise RuntimeError('A passed, visually reviewed upstream baseline is required')
    evidence = Path(review['evidence_directory'])
    if file_sha(evidence / 'results.md') != review['review_sha256']:
        raise RuntimeError('Baseline review text changed')
    for name, expected in review['screenshots'].items():
        if file_sha(regular_destination(evidence, name)) != expected:
            raise RuntimeError(f'Baseline screenshot changed: {name}')
    result = json.loads((evidence / 'results.json').read_text())
    receipt = json.loads(Path(review['build_receipt']).read_text())
    if result.get('automated_result') != 'passed' or result.get('browser_exit_code') != 0 or result.get('forced_cleanup'):
        raise RuntimeError('Baseline automated launch/shutdown did not pass')
    if receipt.get('stage') != 'build' or receipt.get('exit_code') != 0:
        raise RuntimeError('Baseline build did not pass')
    for key in ('chromium', 'depot_tools'):
        if receipt.get(key + '_commit') != pins[key]['commit']:
            raise RuntimeError('Baseline build differs from pin')
    expected = review['binary_sha256']
    if receipt.get('binary_sha256') != expected or result.get('binary_sha256') != expected or file_sha(SOURCE / 'out/mb-debug/chrome') != expected:
        raise RuntimeError('Baseline binary does not match reviewed build')
    return {'review': str(path.resolve()), 'review_sha256': file_sha(path),
            'binary_sha256': expected, 'chromium_commit': pins['chromium']['commit']}


def patch_payload():
    """Materialize exact reviewed patches over pinned blobs in a private scratch tree."""
    patches = sorted((ROOT / 'mb/patches').glob('*.patch'))
    if not patches:
        raise RuntimeError('No product integration patches exist yet')
    paths = set()
    patch_hashes = {}
    for patch in patches:
        data = patch.read_bytes()
        patch_hashes[str(patch.relative_to(ROOT))] = sha(data)
        text = data.decode('utf-8')
        if any(line.startswith(('new file mode ', 'deleted file mode ', 'rename ', 'copy ', 'GIT binary patch')) for line in text.splitlines()):
            raise RuntimeError(f'{patch}: upstream patch must only edit existing text files')
        for line in text.splitlines():
            if not line.startswith('diff --git '):
                continue
            match = re.fullmatch(r'diff --git a/([a-zA-Z0-9_./-]+) b/([a-zA-Z0-9_./-]+)', line)
            if not match or match[1] != match[2]:
                raise RuntimeError(f'{patch}: unsupported patch path')
            name = match[1]
            safe_relative(name)
            if not name.startswith(('chrome/', 'components/', 'build/')):
                raise RuntimeError(f'{patch}: integration outside reviewed browser/build scope: {name}')
            paths.add(name)
    if not paths:
        raise RuntimeError('Integration patches contain no file changes')
    scratch = BUILD / 'tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    original = {}
    with tempfile.TemporaryDirectory(prefix='product-patches-', dir=scratch) as directory:
        staging = Path(directory)
        subprocess.run(['git', 'init', '-q', str(staging)], check=True)
        for name in sorted(paths):
            data = subprocess.check_output(['git', '--no-replace-objects', '-C', str(SOURCE), 'show', 'HEAD:' + name])
            original[name] = data
            path = staging / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for patch in patches:
            subprocess.run(['git', '-C', str(staging), 'apply', '--check', str(patch)], check=True)
            subprocess.run(['git', '-C', str(staging), 'apply', str(patch)], check=True)
        expected = {name: (staging / name).read_bytes() for name in sorted(paths)}
    return expected, original, patch_hashes


def tree_payload(path, prefix):
    result = {}
    for item in sorted(path.rglob('*')):
        if '__pycache__' in item.parts or item.suffix == '.pyc':
            continue
        if item.is_symlink():
            raise RuntimeError(f'Refusing source overlay symlink: {item}')
        if item.is_file():
            result[prefix + '/' + item.relative_to(path).as_posix()] = item.read_bytes()
    return result


def generated_payload():
    manifest = ROOT / 'mb/branding.toml'
    branding.generate(manifest, BUILD / 'generated/branding')
    branding_assets.generate(manifest_path=manifest, output_dir=BUILD / 'generated/branding-assets')
    branding_strings.generate(manifest, BUILD / 'generated/branding-strings', SOURCE)
    payload = tree_payload(ROOT / 'mb', 'mb')
    payload.update(tree_payload(BUILD / 'generated/branding', 'mb/generated/branding'))
    payload.update(tree_payload(BUILD / 'generated/branding-strings', 'mb/generated/branding-strings'))
    assets = BUILD / 'generated/branding-assets'
    payload.update(tree_payload(assets / 'unscaled', 'chrome/app/theme/mb'))
    for scale in ('default_100_percent', 'default_200_percent'):
        payload.update(tree_payload(assets / scale, 'chrome/app/theme/' + scale + '/mb'))
    vector_icon = (ROOT / 'mb/resources/product.icon').read_bytes()
    for name in ('product.icon', 'product_refresh.icon'):
        payload['components/vector_icons/mb/' + name] = vector_icon
    renderer, _ = branding_assets.find_renderer()
    with tempfile.TemporaryDirectory(prefix='product-icons-', dir=BUILD / 'tmp') as directory:
        for scale, size in (('default_100_percent', 16), ('default_200_percent', 32)):
            target = Path(directory) / (scale + '.png')
            branding_assets.rasterize(renderer, ROOT / 'mb/resources/password-manager.svg', size, size, target)
            payload['chrome/app/theme/' + scale + '/mb/favicon_password_manager.png'] = target.read_bytes()
        # Version UI uses component resources separately from browser theme
        # resources. All scale factors derive from the same manifest SVG.
        for multiplier in (1, 2, 3):
            scale = f'default_{100 * multiplier}_percent'
            for name, size in (('product_logo.png', 32),
                               ('product_logo_white.png', 32),
                               ('favicon_product.png', 16)):
                target = Path(directory) / (scale + '-' + name)
                branding_assets.rasterize(renderer, BUILD / 'generated/branding/icon.svg',
                                         size * multiplier, size * multiplier, target)
                payload['components/resources/' + scale + '/mb/' + name] = target.read_bytes()
    payload['chrome/app/theme/mb/BRANDING'] = (BUILD / 'generated/branding/BRANDING').read_bytes()
    payload['chrome/app/theme/default_100_percent/mb/linux/product_logo_32.png'] = (assets / 'default_100_percent/product_logo_32.png').read_bytes()
    return payload


def check_changes(payload, original, previous):
    allowed = set(payload) | set(previous)
    changed = upstream.git(SOURCE, 'diff', '--name-only', 'HEAD').splitlines()
    untracked = upstream.git(SOURCE, 'ls-files', '--others', '--exclude-standard').splitlines()
    unexpected = (set(changed) | set(untracked)) - allowed
    if unexpected:
        raise RuntimeError('Unrelated Chromium changes preserved; refusing integration: ' + ', '.join(sorted(unexpected)))
    for name in sorted(allowed):
        path = regular_destination(SOURCE, name)
        if not path.exists():
            if name in original:
                raise RuntimeError(f'Missing upstream file: {name}')
            continue
        current = file_sha(path)
        acceptable = {previous.get(name)}
        if name in payload:
            acceptable.add(sha(payload[name]))
        if name in original:
            acceptable.add(sha(original[name]))
        if current not in acceptable:
            raise RuntimeError(f'Local edit preserved; refusing overwrite: {name}')


def write_payload(payload):
    for name, data in sorted(payload.items()):
        path = regular_destination(SOURCE, name)
        if path.exists() and path.read_bytes() == data:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.product-', delete=False) as temporary:
            temporary.write(data)
            temporary.flush()
            os.fsync(temporary.fileno())
            staging = Path(temporary.name)
        staging.chmod(path.stat().st_mode & 0o777 if path.exists() else 0o644)
        staging.replace(path)


def retired_payload(previous_files, payload):
    retired = set(previous_files) - set(payload)
    if not retired:
        return {}, set()
    for name in retired:
        safe_relative(name)
    tracked = set(upstream.git(SOURCE, 'ls-tree', '-r', '--name-only', 'HEAD',
                               '--', *sorted(retired)).splitlines())
    restores = {name: subprocess.check_output([
        'git', '--no-replace-objects', '-C', str(SOURCE), 'show', 'HEAD:' + name])
        for name in sorted(tracked)}
    return restores, retired - tracked


def verify_product_sources(receipt):
    current = tree_payload(ROOT / 'mb', 'mb')
    # Compatibility for the initial staging receipt, before this explicit list.
    expected = set(receipt.get('source_files', [name for name in receipt['files']
                       if name.startswith('mb/') and not name.startswith('mb/generated/')]))
    if set(current) != expected:
        raise RuntimeError('Product source file set changed; run prepare after reviewing additions/deletions')
    for name, data in current.items():
        if receipt['files'].get(name) != sha(data):
            raise RuntimeError(f'Product source changed: {name}; run prepare')


def prepare(args):
    pins = check_identity()
    previous = json.loads(RECEIPT.read_text()) if RECEIPT.exists() else None
    if previous and previous.get('chromium_commit') != pins['chromium']['commit']:
        raise RuntimeError('Existing integration receipt belongs to a different upstream pin')
    if previous:
        gate = previous['baseline_gate']
    elif args.baseline_review:
        gate = verify_gate(args.baseline_review, pins)
    else:
        raise RuntimeError('First prepare requires --baseline-review PATH to the reviewed upstream review.json')
    dependencies = upstream.verify_dependencies(BUILD / 'chromium')
    patches, original, patch_hashes = patch_payload()
    payload = generated_payload()
    payload.update(patches)
    old_files = previous['files'] if previous else {}
    restores, retired = retired_payload(old_files, payload)
    check_changes(payload | restores, original | restores, old_files)
    # Preserve retired generated outputs in a dated directory, never delete or
    # overwrite an unrecognized user edit. Validation above precedes all moves.
    if retired:
        archive = BUILD / 'integration-retired' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        for name in sorted(retired):
            path = regular_destination(SOURCE, name)
            if path.exists():
                destination = archive / safe_relative(name)
                destination.parent.mkdir(parents=True, exist_ok=True)
                path.rename(destination)
    # A retired upstream patch restores its pinned file in place. Only owned
    # generated outputs leave the tree; tracked Chromium files are never moved.
    write_payload(payload | restores)
    receipt = {'schema_version': 1, 'chromium_commit': pins['chromium']['commit'],
               'depot_tools_commit': pins['depot_tools']['commit'],
               'git_dependencies': dependencies, 'baseline_gate': gate,
               'prepared': datetime.now(timezone.utc).isoformat(),
               'files': {name: sha(data) for name, data in sorted(payload.items())},
               'source_files': sorted(tree_payload(ROOT / 'mb', 'mb')),
               'patches': patch_hashes,
               'product': branding.load_manifest(ROOT / 'mb/branding.toml')['product']}
    branding.atomic_write(RECEIPT, json.dumps(receipt, indent=2) + '\n')
    print(f'Product integration staged: {len(payload)} files; {RECEIPT}', flush=True)


def verify():
    pins = check_identity()
    if not RECEIPT.is_file():
        raise RuntimeError('Run product.py prepare first')
    receipt = json.loads(RECEIPT.read_text())
    if receipt['chromium_commit'] != pins['chromium']['commit']:
        raise RuntimeError('Integration receipt differs from pin')
    for name, expected in receipt['files'].items():
        if file_sha(regular_destination(SOURCE, name)) != expected:
            raise RuntimeError(f'Staged integration changed: {name}; run prepare after reviewing edits')
    verify_product_sources(receipt)
    if receipt['product'] != branding.load_manifest(ROOT / 'mb/branding.toml')['product']:
        raise RuntimeError('Integration product identity differs from the authoritative manifest')
    check_changes({}, {}, receipt['files'])
    dependencies = upstream.verify_dependencies(BUILD / 'chromium')
    if dependencies != receipt['git_dependencies']:
        raise RuntimeError('Dependency identities changed after preparation')
    return receipt


def run_build_stage(args):
    receipt = verify()
    scratch = BUILD / 'tmp'
    scratch.mkdir(exist_ok=True)
    if shutil.disk_usage(BUILD).free < 25 * 2**30:
        raise RuntimeError('Less than 25 GiB free working reserve')
    expected = (ROOT / 'mb/tools/gn/product-debug.gn').read_text()
    args_path = OUTPUT / 'args.gn'
    if args.stage == 'gen':
        current = args_path.read_text()
        if current not in (expected, (ROOT / 'mb/tools/gn/upstream-debug.gn').read_text()):
            raise RuntimeError('Unrecognized existing GN arguments; preserving them')
        if current != expected:
            # The successful baseline receipt already preserves exact old args.
            branding.atomic_write(args_path, expected)
        command = [str(DEPOT / 'gn'), 'gen', 'out/mb-debug', '--fail-on-unused-args']
    else:
        if args_path.read_text() != expected:
            raise RuntimeError('Run product.py gen with the product arguments first')
        targets = args.targets or (['mb:mb_unit_tests', 'mb:mb_control'] if args.stage == 'test-build' else ['chrome', 'chrome_sandbox'])
        command = [str(DEPOT / 'autoninja'), '-C', 'out/mb-debug', '-j', str(args.jobs), *targets]
    env = dict(os.environ, PATH=f"{DEPOT}:{DEPOT / 'python-bin'}:{os.environ.get('PATH', '')}",
               DEPOT_TOOLS_UPDATE='0', DEPOT_TOOLS_METRICS='0',
               PYTHONUNBUFFERED='1', TMPDIR=str(scratch))
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    log = BUILD / 'logs' / f'product-{args.stage}-{stamp}.log'
    print(f'[{SOURCE}] {shlex.join(command)}\nLog: {log}', flush=True)
    with log.open('x') as stream:
        stream.write(f'cwd: {SOURCE}\ncommand: {shlex.join(command)}\n')
        stream.flush()
        outcome = subprocess.run(command, cwd=SOURCE, env=env, stdout=stream, stderr=subprocess.STDOUT)
    result = {'stage': args.stage, 'started': stamp, 'finished': datetime.now(timezone.utc).isoformat(),
              'exit_code': outcome.returncode, 'command': command, 'args_gn': expected,
              'integration_sha256': file_sha(RECEIPT), 'integration': receipt, 'log': str(log)}
    binary = OUTPUT / receipt['product']['executable_name']
    if args.stage == 'build' and outcome.returncode == 0:
        result.update(binary=str(binary), binary_sha256=file_sha(binary))
    if args.stage == 'test-build' and outcome.returncode == 0:
        test_outputs = {
            'mb_unit_tests': OUTPUT / 'mb_unit_tests',
            'mb_browser_tests': OUTPUT / 'mb_browser_tests',
            'mb_control': OUTPUT / (receipt['product']['executable_name'] + 'ctl'),
        }
        result['test_binaries'] = {
            name: {'path': str(path), 'sha256': file_sha(path)}
            for name, path in test_outputs.items()
            if 'mb:' + name in targets or '//mb:' + name in targets
        }
    log.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    if outcome.returncode:
        raise RuntimeError(f'{args.stage} failed ({outcome.returncode}); inspect {log}')
    print(f'{args.stage} succeeded; receipt: {log.with_suffix(".json")}', flush=True)



def run_tests():
    receipt = verify()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    evidence = BUILD / 'test-evidence' / ('product-unit-' + stamp)
    scratch = BUILD / 'test-data' / ('product-unit-' + stamp)
    evidence.mkdir(mode=0o700, parents=True)
    scratch.mkdir(mode=0o700, parents=True)
    (BUILD / 'control').mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(scratch),
               MB_ENVIRONMENT_TEST_TMPDIR=str(scratch),
               MB_RUNTIME_CONFIG_TEST_TMPDIR=str(scratch))
    binary = OUTPUT / 'mb_unit_tests'
    companion = OUTPUT / (receipt['product']['executable_name'] + 'ctl')
    commands = [
        [str(binary), '--test-launcher-jobs=4',
         '--test-launcher-summary-output=' + str(evidence / 'summary.json')],
        [sys.executable, '-c',
         'import pathlib, sys, unittest; '
         'sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / "mb/test")); '
         'import test_control; test_control.BINARY = pathlib.Path(sys.argv[2]); '
         'result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(test_control)); '
         'sys.exit(not result.wasSuccessful())', str(ROOT), str(companion)],
    ]
    result = {'integration_sha256': file_sha(RECEIPT),
              'binary_sha256': file_sha(binary),
              'companion_sha256': file_sha(companion), 'commands': []}
    for index, command in enumerate(commands):
        log = evidence / f'{index + 1}.log'
        with log.open('x') as stream:
            run = subprocess.run(command, cwd=ROOT, env=env,
                                 stdout=stream, stderr=subprocess.STDOUT)
        result['commands'].append({'command': command, 'exit_code': run.returncode,
                                   'log': str(log)})
        result['result'] = 'failed' if run.returncode else 'passed'
        (evidence / 'run.json').write_text(json.dumps(result, indent=2) + '\n')
        if run.returncode:
            raise RuntimeError(f'Product tests failed; see {log}')
    print(f'Product C++ and companion tests passed: {evidence}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('prepare', 'verify', 'gen', 'build', 'test-build', 'test'))
    parser.add_argument('--baseline-review', type=Path)
    parser.add_argument('--jobs', type=int, default=12)
    parser.add_argument('--targets', nargs='+')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        if args.stage == 'prepare':
            prepare(args)
        elif args.stage == 'verify':
            verify()
            print('Product integration verified')
        elif args.stage == 'test':
            args.stage = 'test-build'
            args.targets = ['mb:mb_unit_tests', 'mb:mb_control']
            run_build_stage(args)
            run_tests()
        else:
            run_build_stage(args)
        return 0
    except (RuntimeError, ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
