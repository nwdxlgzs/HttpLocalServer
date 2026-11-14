#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Pack separately fetched preview assets into the single native executable."""
import gzip
import hashlib
import json
import os
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
TYPES={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.wasm':'application/wasm','.ttf':'font/ttf','.pfb':'application/octet-stream','.bcmap':'application/octet-stream','.icc':'application/octet-stream'}

def main():
    directory=ROOT/'web/preview'
    files=sorted((p for p in directory.rglob('*') if p.is_file() and p.suffix in TYPES and p.name!='legacy-canvas.js'),key=lambda p:p.relative_to(directory).as_posix())
    files.extend([ROOT/'web/vendor/fflate.js',ROOT/'web/vendor/marked.js',ROOT/'web/scripts/compat.js'])
    previous={};old=b''
    try:
        manifest=json.loads((ROOT/'resources/previews-manifest.json').read_text(encoding='utf-8'))
        old=(ROOT/'resources/previews.bin').read_bytes()
        if hashlib.sha256(old).hexdigest()==manifest['sha256']:
            previous={a['path']:a for a in manifest['assets']}
    except (OSError,ValueError,KeyError):pass
    result=[];packed=bytearray();sources={}
    for file in files:
        relative=file.name if file.parent in (ROOT/'web/vendor',ROOT/'web/scripts') else file.relative_to(directory).as_posix()
        raw=file.read_bytes();data=None;prior=previous.get(relative)
        if prior and prior['sha256']==hashlib.sha256(raw).hexdigest():
            candidate=old[prior['offset']:prior['offset']+prior['bytes']]
            try:
                if gzip.decompress(candidate)==raw:data=candidate
            except (OSError,ValueError,EOFError):pass
        if data is None:
            data=bytearray(gzip.compress(raw,compresslevel=9,mtime=0));data[9]=255
        sources[file.relative_to(ROOT).as_posix()]=hashlib.sha256(raw).hexdigest()
        result.append({'path':relative,'offset':len(packed),'bytes':len(data),'rawBytes':len(raw),'mime':TYPES[file.suffix],'sha256':hashlib.sha256(raw).hexdigest()});packed.extend(data)
    destination=ROOT/'resources';manifest={'packedBytes':len(packed),'assets':result,'sources':sources,'sha256':hashlib.sha256(packed).hexdigest()}
    for name,data in [('previews.bin',packed),('previews-manifest.json',(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n').encode())]:
        p=destination/name
        if not p.exists() or p.read_bytes()!=data:
            with tempfile.NamedTemporaryFile(dir=destination,prefix='.preview-pack-',delete=False) as temp:
                temp.write(data);temporary=temp.name
            os.replace(temporary,p)
    print('Preview assets:',len(result),'files,',len(packed),'compressed bytes')

if __name__=='__main__':main()
