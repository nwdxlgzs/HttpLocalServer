"""Replace only the encoded audio after a voice correction; video packets are copied."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT=Path(__file__).resolve().parents[1]
BIN=ROOT/'tools/bin'
parser=argparse.ArgumentParser()
parser.add_argument('variants',nargs='*',metavar='EDITION')
parser.add_argument('--legacy-1080p',action='store_true')
options=parser.parse_args()
if set(options.variants)-{'main','overview','teaser','vertical'}:parser.error('Unknown edition')
delivery=json.loads((ROOT/'data/delivery.json').read_text(encoding='utf-8'))
names={k:v['file'] for k,v in delivery['variants'].items()}
if options.legacy_1080p:names={'main':'HttpLocalServer-main-1080p.mp4','overview':'HttpLocalServer-overview.mp4','teaser':'HttpLocalServer-teaser.mp4','vertical':'HttpLocalServer-vertical.mp4'}
if options.variants:names={k:names[k] for k in options.variants}
data=json.loads((ROOT/'data/scenes.json').read_text(encoding='utf-8-sig'))['variants']
report_path=ROOT/('review/audio-refresh-1080p.json' if options.legacy_1080p else 'review/audio-refresh-4k.json')
report={'videoReencoded':False,'profile':delivery['profile'],'films':[]}
if report_path.exists():
    prior=json.loads(report_path.read_text(encoding='utf-8'))
    if prior.get('profile')==delivery['profile']:report['films']=[f for f in prior['films'] if f['id'] not in names]
for variant,name in names.items():
    source=ROOT/'exports'/name
    voice=ROOT/'assets/audio'/('master-'+variant+'.wav')
    temporary=source.with_name(source.stem+'.audio-refresh.mp4')
    def video_hash(file):
        return subprocess.check_output([str(BIN/'ffmpeg.exe'),'-v','error','-i',str(file),'-map','0:v:0','-c','copy','-f','hash','-hash','sha256','-'],text=True).strip()
    before_video_hash=video_hash(source)
    command=[str(BIN/'ffmpeg.exe'),'-y','-v','error','-i',str(source),'-i',str(voice),
             '-map','0:v:0','-map','1:a:0','-map_metadata','0','-c:v','copy','-c:a','aac','-b:a',delivery['audio']['aacBitrate'],'-ar','48000','-ac','2',
             '-t',str(data[variant]['duration']),'-movflags','+faststart',str(temporary)]
    subprocess.run(command,check=True)
    meta=json.loads(subprocess.check_output([str(BIN/'ffprobe.exe'),'-v','error','-show_format','-show_streams','-of','json',str(temporary)]))
    duration=float(meta['format']['duration'])
    assert abs(duration-data[variant]['duration'])<.08,(variant,duration)
    after_video_hash=video_hash(temporary)
    assert before_video_hash==after_video_hash,(variant,'video packets changed')
    os.replace(temporary,source)
    report['films'].append({'id':variant,'file':str(source.relative_to(ROOT)),'duration':duration,
                           'masterAudioSha256':hashlib.sha256(voice.read_bytes()).hexdigest(),'videoReencoded':False,'videoPacketHash':after_video_hash,'videoPacketHashUnchanged':True})
    print(variant,'audio refreshed; video copied',flush=True)
report_path.write_text(json.dumps(report,indent=2),encoding='utf-8')
