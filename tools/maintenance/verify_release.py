#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Reproduce native + shipped-browser regression checks for a built executable."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
INTEGRATIONS = [
    'script_cache_integration.py', 'wasm_app_integration.py', 'mcp_current_integration.py', 'frozen_lock_integration.py',
    'features_v330_integration.py', 'preview_assets_integration.py', 'zip_preview_integration.py', 'transfer_cache_integration.py', 'bundle_integration.py', 'durable_integration.py',
    'archive_integration.py', 'state_integration.py', 'traffic_integration.py',
    'secure_features_integration.py', 'integration.py', 'config_integration.py',
    'access_integration.py', 'tls_integration.py', 'mounts_integration.py',
    'p2p_integration.py', 'p2p_relay_integration.py', 'p2p_mount_integration.py',
    'branding_integration.py', 'collaboration_integration.py', 'defaults.py',
]
BROWSER = ['preview_assets_test.cjs', 'zip_codec_test.cjs', 'jobs_test.cjs', 'mounts_test.cjs', 'batch_test.cjs', 'p2p_test.cjs',
           'clipboard_test.cjs', 'drag_thumbnail_test.cjs', 'player_state_test.cjs',
           'collaboration_test.cjs', 'ie_compat_test.cjs']


def report_path_label(path):
    """Keep personal home/build directories out of publishable reports."""
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return '<external>/' + path.name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=pathlib.Path)
    parser.add_argument('--report', required=True, type=pathlib.Path)
    parser.add_argument('--log-dir', type=pathlib.Path)
    parser.add_argument('--ctest-dir', type=pathlib.Path)
    parser.add_argument('--skip-defaults', action='store_true', help='Port 8000 belongs to another service')
    parser.add_argument('--no-tls', action='store_true', help='Executable was built without optional TLS')
    parser.add_argument('--load-report', type=pathlib.Path, help='Also run the 96-client mixed-load check')
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    report_path = args.report.resolve()
    logs = (args.log_dir or report_path.with_suffix('')).resolve()
    logs.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = {'binary': report_path_label(executable), 'pathsRelativeTo': 'repository root',
              'sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'tests': [], 'skipped': []}
    commands = []
    if args.ctest_dir:
        commands.append(('ctest', ['ctest', '--test-dir', str(args.ctest_dir.resolve()), '--output-on-failure']))
    for name in INTEGRATIONS:
        if args.skip_defaults and name == 'defaults.py':
            report['skipped'].append({'name': name, 'reason': 'Port 8000 in use'})
            continue
        if args.no_tls and name in ('secure_features_integration.py', 'tls_integration.py', 'p2p_mount_integration.py', 'p2p_relay_integration.py'):
            report['skipped'].append({'name': name, 'reason': 'TLS deliberately disabled'})
            continue
        commands.append((name, [sys.executable, '-X', 'utf8', str(ROOT/'tests'/name), str(executable)]))
    commands.append(('resume_test.cjs', ['node', str(ROOT/'tests/resume_test.cjs'), str(executable)]))
    for name in BROWSER:
        commands.append((name, ['node', *(['--expose-internals'] if name in ('ie_compat_test.cjs','preview_assets_test.cjs') else []), str(ROOT/'tests'/name)]))
    if args.load_report:
        commands.append(('mixed-load', [sys.executable, '-X', 'utf8', str(ROOT/'tests/load.py'),
                                       str(executable), '--output', str(args.load_report.resolve())]))
    for name, command in commands:
        started = time.monotonic()
        log = logs/(name+'.log')
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=300)
        report['tests'].append({'name': name, 'exitCode': result.returncode,
                                'seconds': round(time.monotonic()-started, 2), 'log': report_path_label(log)})
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        print(name, 'passed' if result.returncode == 0 else 'FAILED', flush=True)
        if result.returncode:
            print(log.read_text(encoding='utf-8', errors='replace')[-6000:])
            return result.returncode
    return 0


if __name__ == '__main__':
    sys.exit(main())
