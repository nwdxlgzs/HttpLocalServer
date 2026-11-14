#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Bundle ES5 sources and deterministically gzip the embedded page; no npm needed."""
import gzip
import hashlib
import json
import os
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
STYLES=['base.css','polish.css','dark.css','player.css','dragdrop.css','collaboration.css','mounts.css','access.css','compat.css','p2p.css','tasks.css','preview_tabs.css','mobile.css']
SCRIPTS=['core.js','frozen.js','tabs.js','thumbnails.js','listing.js','reveal.js','tasks.js','transfer_cache.js','jobs.js','batch.js','resume.js','operations.js','transfers.js','p2p_cache.js','p2p_hash.js','p2p.js','p2p_download.js','p2p_relay.js','dragdrop.js','previews.js','content_tabs.js','player.js','documents.js','archive.js','mounts.js','directory_save.js','collaboration.js','settings.js','access.js','clipboard.js','boot.js','mobile.js']

def main():
    files=['web/index.html','web/scripts/compat.js']+['web/styles/'+name for name in STYLES]+['web/vendor/'+name+'.js' for name in ['marked','fflate']]+['web/scripts/'+name for name in SCRIPTS]
    texts={name:(ROOT/name).read_text(encoding='utf-8') for name in files}
    css='\n'.join(texts['web/styles/'+name] for name in STYLES)
    vendor='\n'.join(texts['web/vendor/'+name+'.js'] for name in ['marked','fflate'])
    app="(function(){\n'use strict';\n"+'\n'.join(texts['web/scripts/'+name] for name in SCRIPTS)+'\n}());'
    # Prevent literal closing script tags in sources or example strings.
    script=(texts['web/scripts/compat.js']+'\n'+vendor+'\n'+app).replace('</script','<\\/script')
    html=texts['web/index.html'].replace('<!-- HLS_STYLES -->','<style>\n'+css+'\n</style>').replace('<!-- HLS_SCRIPTS -->','<script>\n'+script+'\n</script>')
    raw=html.encode('utf-8')
    destination=ROOT/'resources';destination.mkdir(exist_ok=True)
    sources={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files}
    # Preserve a valid committed bundle across Python/zlib versions, which may
    # emit different but equivalent DEFLATE streams for identical inputs.
    packed=None
    try:
        previous=json.loads((destination/'web-manifest.json').read_text(encoding='utf-8'))
        candidate=(destination/'web.gz').read_bytes()
        if previous.get('sources')==sources and previous.get('sha256')==hashlib.sha256(candidate).hexdigest() and gzip.decompress(candidate)==raw:
            packed=candidate
    except (OSError,ValueError,EOFError,gzip.BadGzipFile):
        pass
    if packed is None:
        packed=bytearray(gzip.compress(raw,compresslevel=9,mtime=0));packed[9]=255;packed=bytes(packed)
    manifest={'format':'gzip','uncompressedBytes':len(raw),'gzipBytes':len(packed),'sha256':hashlib.sha256(packed).hexdigest(),'sources':sources}
    for name,data in [('web.gz',packed),('web-manifest.json',(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode())]:
        path=destination/name
        if not path.exists() or path.read_bytes()!=data:
            with tempfile.NamedTemporaryFile(dir=destination,prefix='.web-pack-',delete=False) as temp:
                temp.write(data);temporary=temp.name
            os.replace(temporary,path)
    # Development outputs are not build inputs and are never fetched by the app.
    (ROOT/'.deps').mkdir(exist_ok=True)
    (ROOT/'.deps/web-v22.html').write_bytes(raw)
    (ROOT/'.deps/web-script-v22.js').write_text(app,encoding='utf-8')
    print('Embedded page:',len(raw),'->',len(packed),'bytes (gzip)')

if __name__=='__main__':main()
