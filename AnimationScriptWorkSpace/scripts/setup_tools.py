"""Install local Windows production tools, never application dependencies.

Reuse imageio-ffmpeg and the pinned native ffprobe npm archive. No global PATH changes.
"""
from pathlib import Path
import hashlib, json, shutil, subprocess, tarfile
import imageio_ffmpeg

ROOT=Path(__file__).resolve().parents[1]
BIN=ROOT/'tools/bin';BIN.mkdir(parents=True,exist_ok=True)
if not (BIN/'ffmpeg.exe').exists():
    shutil.copyfile(imageio_ffmpeg.get_ffmpeg_exe(),BIN/'ffmpeg.exe')
archive=ROOT/'tools/ffprobe-npm.tgz'
if not (BIN/'ffprobe.exe').exists():
    url='https://registry.npmjs.org/@ffprobe-installer/win32-x64/-/win32-x64-5.1.0.tgz'
    for attempt in range(4):
        result=subprocess.run(['curl.exe','-L','--fail','--silent','--show-error','--connect-timeout','15','--max-time','120','-C','-','-o',str(archive),url])
        if result.returncode==0:break
    else:raise SystemExit('ffprobe download did not complete; rerun to resume.')
    with tarfile.open(archive) as bundle:
        for item in bundle.getmembers():
            if item.name.endswith('/ffprobe.exe'):
                with bundle.extractfile(item) as src,(BIN/'ffprobe.exe').open('wb') as dst:shutil.copyfileobj(src,dst)
            elif item.name.endswith('/LICENSE') or item.name.endswith('/README.md'):
                with bundle.extractfile(item) as src,(BIN/('ffprobe-'+Path(item.name).name)).open('wb') as dst:shutil.copyfileobj(src,dst)
assets=[]
for name,source in [('ffmpeg','local imageio_ffmpeg package executable'),('ffprobe','@ffprobe-installer/win32-x64@5.1.0')]:
    binary=BIN/(name+'.exe')
    result=subprocess.run([str(binary),'-version'],capture_output=True,text=True,encoding='utf-8',errors='replace',check=True)
    assets.append({'name':name,'source':source,'path':str(binary.relative_to(ROOT)),'version':result.stdout.splitlines()[0],
                   'sha256':hashlib.sha256(binary.read_bytes()).hexdigest()})
(ROOT/'tools/manifest.json').write_text(json.dumps({'platform':'Windows x64','productionOnly':True,'assets':assets},indent=2),encoding='utf-8')
print('Native production tools ready')
