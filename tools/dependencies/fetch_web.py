#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Fetch the pinned ES5 browser dependencies, checking npm SHA-512 integrity.

Uses only Python's standard library. Normal builds use the vendored copies.
"""
import base64
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request
from web_compat import patch_web_dependency

ROOT=Path(__file__).resolve().parents[2]
PACKAGES={
    'fflate':('0.8.2','package/umd/index.js','package/LICENSE'),
    'marked':('4.3.0','package/lib/marked.umd.js','package/LICENSE.md'),
}

def main():
    manifest_path=ROOT/'tools/dependencies/web-lock.json'
    locked=json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    output={}
    for name,(version,script,license_path) in PACKAGES.items():
        if name in locked:
            source=locked[name]
        else:
            metadata=json.load(urllib.request.urlopen('https://registry.npmjs.org/'+name+'/'+version))
            source={'version':version,'url':metadata['dist']['tarball'],'integrity':metadata['dist']['integrity']}
        assert source['version']==version
        data=urllib.request.urlopen(source['url']).read()
        algorithm,digest=source['integrity'].split('-',1)
        assert algorithm=='sha512' and base64.b64encode(hashlib.sha512(data).digest()).decode()==digest
        with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
            code=archive.extractfile(script).read()
            license_data=archive.extractfile(license_path).read()
        source['upstreamJavaScriptSha256']=hashlib.sha256(code).hexdigest()
        code,patches=patch_web_dependency(name,code)
        if patches:source['compatibilityPatches']=patches
        (ROOT/'web/vendor').mkdir(exist_ok=True)
        (ROOT/'web/vendor'/(name+'.js')).write_bytes(code)
        (ROOT/'licenses'/(name+'-MIT.txt')).write_bytes(license_data)
        source['javascriptSha256']=hashlib.sha256(code).hexdigest()
        source['licenseSha256']=hashlib.sha256(license_data).hexdigest()
        output[name]=source
        print(name,version,len(code),source['javascriptSha256'])
    manifest_path.write_text(json.dumps(output,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
