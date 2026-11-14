"""Fetch a pinned upstream source archive; normal builds are offline."""
import hashlib
import json
import pathlib
import urllib.request

root=pathlib.Path(__file__).resolve().parents[2]
lock=json.loads((pathlib.Path(__file__).with_name('quickjs-lock.json')).read_text(encoding='utf-8'))
destination=root/'third_party'/lock['archive']
if destination.exists() and hashlib.sha256(destination.read_bytes()).hexdigest()==lock['sha256']:
    print('QuickJS-NG',lock['version'],'verified')
else:
    with urllib.request.urlopen(lock['url'],timeout=120) as response:
        data=response.read(16*1024*1024+1)
    if len(data)>16*1024*1024 or hashlib.sha256(data).hexdigest()!=lock['sha256']:
        raise RuntimeError('QuickJS-NG archive checksum mismatch')
    destination.write_bytes(data)
    print('QuickJS-NG',lock['version'],'downloaded and verified')
