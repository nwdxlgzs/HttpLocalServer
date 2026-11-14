#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Acquire the official pinned TLS source archive; ordinary builds are offline."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[2]


def main():
    locked=json.loads((ROOT/'tools/dependencies/mbedtls-lock.json').read_text(encoding='utf-8'))
    destination=ROOT/locked['archive']
    data=destination.read_bytes() if destination.exists() else urllib.request.urlopen(locked['url'],timeout=60).read()
    if hashlib.sha256(data).hexdigest()!=locked['sha256']:
        raise ValueError('Pinned mbedTLS source SHA-256 mismatch')
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:bz2') as source:
        license_data=source.extractfile('mbedtls-'+locked['version']+'/LICENSE').read()
    if not destination.exists():destination.write_bytes(data)
    (ROOT/'licenses/mbedtls-Apache-2.0.txt').write_bytes(license_data)
    print(locked['name'],locked['version'],len(data),locked['sha256'])


if __name__=='__main__':main()
