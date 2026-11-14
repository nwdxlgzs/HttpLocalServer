#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Reproduce pinned browser preview inputs. No Rust toolchain is involved."""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[2]


def package(name,lock,mirror):
    source=lock[name]
    destination=ROOT/'.deps/preview-sources'/name.replace('/','-').replace('@','')
    destination.mkdir(parents=True,exist_ok=True)
    url=source['resolved']
    if mirror:
        url=url.replace('https://registry.npmjs.org/','https://registry.npmmirror.com/')
    with urllib.request.urlopen(url,timeout=180) as response:
        data=response.read()
    algorithm,digest=source['integrity'].split('-',1)
    if algorithm!='sha512' or base64.b64encode(hashlib.sha512(data).digest()).decode()!=digest:
        raise RuntimeError('Package integrity mismatch: '+name)
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
        for item in archive.getmembers():
            parts=Path(item.name).parts
            if not item.isfile() or not parts or parts[0]!='package' or '..' in parts:
                continue
            target=destination.joinpath(*parts[1:])
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(archive.extractfile(item).read())
    return destination


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mirror',action='store_true',help='Fetch through npmmirror; official npm integrity remains required')
    parser.add_argument('--legacy',action='store_true',help='Install pinned development tooling and regenerate ES5 PDF assets')
    args=parser.parse_args()
    lock=json.loads((ROOT/'tools/dependencies/preview-lock.json').read_text(encoding='utf-8'))
    office=package('@silurus/ooxml',lock,args.mirror)
    pdf=package('pdfjs-dist',lock,args.mirror)
    monaco=package('monaco-editor',lock,args.mirror)
    vendor=ROOT/'web/preview/vendor'
    for source in (office/'dist').rglob('*'):
        if source.is_file() and source.suffix in ('.js','.mjs','.wasm'):
            target=vendor/'office'/source.relative_to(office/'dist');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for source,target in [('LICENSE','silurus-ooxml-MIT.txt'),('THIRD_PARTY_NOTICES.md','silurus-ooxml-components.txt')]:
        shutil.copyfile(office/source,ROOT/'licenses'/target)
    for source in (monaco/'min/vs').rglob('*'):
        if not source.is_file() or source.suffix not in ('.js','.css','.ttf'):continue
        relative=source.relative_to(monaco/'min/vs')
        if relative.parts[0]=='nls' and source.name!='zh-cn.js':continue
        target=vendor/'monaco'/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for source,target in [('LICENSE','monaco-MIT.txt'),('ThirdPartyNotices.txt','monaco-components.txt')]:shutil.copyfile(monaco/source,ROOT/'licenses'/target)
    (vendor/'pdf').mkdir(parents=True,exist_ok=True)
    for name in ('pdf','pdf.worker','pdf.renderer'):
        shutil.copyfile(pdf/'legacy/build'/(name+'.min.mjs'),vendor/'pdf'/(name+'.mjs'))
    for directory in ('cmaps','standard_fonts','wasm','iccs'):
        if not (pdf/directory).exists():continue
        for source in (pdf/directory).rglob('*'):
            if not source.is_file():continue
            if source.name.startswith('LICENSE'):
                shutil.copyfile(source,ROOT/'licenses'/('pdfjs-'+directory+'-'+source.name+'.txt'))
            else:
                target=vendor/'pdf'/source.relative_to(pdf);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    shutil.copyfile(pdf/'LICENSE',ROOT/'licenses/pdfjs-Apache-2.0.txt')
    if args.legacy:
        tools=ROOT/'.deps/preview-tooling';tools.mkdir(parents=True,exist_ok=True)
        for name in ('package.json','package-lock.json'):
            shutil.copyfile(ROOT/'tools/dependencies/preview-tooling'/name,tools/name)
        npm='npm.cmd' if __import__('os').name=='nt' else 'npm'
        subprocess.run([npm,'ci','--prefix',str(tools),'--ignore-scripts','--no-audit','--no-fund'],check=True)
        subprocess.run(['node',str(ROOT/'tools/assets/build_preview_legacy.cjs')],check=True,cwd=ROOT)
    subprocess.run([__import__('sys').executable,str(ROOT/'tools/assets/pack_previews.py')],check=True)
    print('Preview inputs reproduced and verified')


if __name__=='__main__':main()
