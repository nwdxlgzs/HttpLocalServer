"""Check produced workspace text for exact private credentials, without disclosure."""
from __future__ import annotations
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SUFFIXES = {'.html', '.css', '.js', '.mjs', '.cjs', '.ts', '.json', '.md', '.txt', '.srt',
            '.vtt', '.yaml', '.yml', '.toml', '.ps1', '.py', '.svg', '.csv', '.log', '.map', '.lock'}
SKIP = {'node_modules', '.git', '__pycache__'}


def main():
    private = ROOT.parent / '.PrivateSpace'
    values = []
    for path in private.iterdir():
        if path.is_file() and 'key' in path.name.lower():
            value = path.read_text(encoding='utf-8-sig').strip()
            if value:
                values.append(value)
    if not values:
        raise RuntimeError('No credential material was available for exact-match verification.')
    findings = []
    scanned = 0
    occurrences = 0
    for path in ROOT.rglob('*'):
        if not path.is_file() or any(part in SKIP for part in path.relative_to(ROOT).parts):
            continue
        if path.suffix.lower() not in SUFFIXES:
            continue
        data = path.read_bytes()
        scanned += 1
        count = sum(data.count(value.encode(encoding)) for value in values for encoding in ('utf-8', 'utf-16-le', 'utf-16-be'))
        if count:
            findings.append(str(path.relative_to(ROOT)).replace('\\', '/'))
            occurrences += count
    report = {'scannedWorkspaceTextFiles': scanned, 'matchingFiles': len(findings),
              'exactMatches': occurrences, 'paths': findings}
    (ROOT / 'assets/audio/private-data-scan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
    if occurrences:
        sys.exit(1)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        print('Private-data verification failed; exception details withheld.', file=sys.stderr)
        sys.exit(1)
