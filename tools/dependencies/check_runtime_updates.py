#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify pinned archives and compare runtime/MCP versions with official sources.

Read-only: this never replaces a dependency or rewrites a lock file.
"""
import argparse,datetime,hashlib,json,pathlib,re,sys,urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[2]

def fetch(url):
    request=urllib.request.Request(url,headers={'User-Agent':'HttpLocalServer-dependency-check','Accept':'application/json'})
    with urllib.request.urlopen(request,timeout=30) as response:return response.geturl(),response.read(4*1024*1024)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--offline',action='store_true');args=parser.parse_args()
    rows=[];errors=[]
    for file,repo in [('quickjs-lock.json','quickjs-ng/quickjs'),('wamr-lock.json','wasm-micro-runtime/wasm-micro-runtime')]:
        lock=json.loads(pathlib.Path(__file__).with_name(file).read_text(encoding='utf-8'));archive=ROOT/'third_party'/lock['archive']
        row={'component':lock['name'],'pinned':lock['version'],'archiveVerified':archive.exists() and hashlib.sha256(archive.read_bytes()).hexdigest()==lock['sha256']}
        if not row['archiveVerified']:errors.append('Archive checksum mismatch: '+lock['name'])
        if not args.offline:
            row['source']='https://api.github.com/repos/'+repo+'/releases/latest'
            try:
                _,data=fetch(row['source']);release=json.loads(data);row['latest']=release['tag_name'].removeprefix('WAMR-').removeprefix('v') if sys.version_info>=(3,9) else release['tag_name'].replace('WAMR-','').lstrip('v');row['published']=release['published_at'];row['updateAvailable']=row['latest']!=row['pinned']
            except Exception as error:row['error']=str(error);errors.append(lock['name']+': upstream unavailable')
        rows.append(row)
    row={'component':'MCP','pinned':'2026-07-28','source':'https://modelcontextprotocol.io/specification/latest'}
    if not args.offline:
        try:
            url,data=fetch(row['source']);match=re.search(r'/specification/(\d{4}-\d{2}-\d{2})',url)
            if not match:match=re.search(r'/specification/(\d{4}-\d{2}-\d{2})',data.decode('utf-8'))
            if not match:raise RuntimeError('Official latest page did not identify a dated version')
            row['latest']=match.group(1);row['updateAvailable']=row['latest']!=row['pinned']
        except Exception as error:row['error']=str(error);errors.append('MCP: upstream unavailable')
    rows.append(row)
    print(json.dumps({'checkedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'offline':args.offline,'components':rows,'errors':errors},ensure_ascii=False,indent=2))
    return 2 if errors else 1 if any(row.get('updateAvailable') for row in rows) else 0

if __name__=='__main__':sys.exit(main())
