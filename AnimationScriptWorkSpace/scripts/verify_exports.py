"""Verify deliverable media streams and retain representative frames."""
from pathlib import Path
from fractions import Fraction
import argparse, hashlib, json, re, shutil, subprocess
from PIL import Image, ImageDraw
import numpy as np
import wave

ROOT=Path(__file__).resolve().parents[1]
ffmpeg=ROOT/'tools/bin/ffmpeg.exe';probe=ROOT/'tools/bin/ffprobe.exe'
parser=argparse.ArgumentParser()
parser.add_argument('variants',nargs='*',metavar='EDITION')
options=parser.parse_args()
if set(options.variants)-{'main','overview','teaser','vertical'}:parser.error('Unknown edition')
delivery=json.loads((ROOT/'data/delivery.json').read_text(encoding='utf-8'))
variants={k:v['file'] for k,v in delivery['variants'].items() if not options.variants or k in options.variants}
scene_data=json.loads((ROOT/'data/scenes.json').read_text(encoding='utf-8-sig'))['variants']
report={'passed':True,'profile':delivery['profile'],'films':[]}
report_path=ROOT/'review/export-verification-4k.json'
if report_path.exists():
 prior=json.loads(report_path.read_text(encoding='utf-8'))
 if prior.get('profile')==delivery['profile']:report['films']=[f for f in prior['films'] if f['id'] not in variants]
for key,name in variants.items():
 config=scene_data[key];duration=config['duration'];width=delivery['variants'][key]['width'];height=delivery['variants'][key]['height']
 times=[round(s['start']+min(2.5,s['duration']*.6),3) for s in config['scenes']]
 for s in config['scenes']:
  if s['kind']=='address':times.extend([round(s['start']+.9,3),round(s['start']+s['duration']*.74,3)])
 times=sorted(set(times))
 file=ROOT/'exports'/name
 if not file.exists():
  report['passed']=False;report['films'].append({'id':key,'passed':False,'reason':'missing export'});print(key,'MISSING');continue
 meta=json.loads(subprocess.check_output([str(probe),'-v','error','-show_streams','-show_format','-of','json',str(file)]))
 video=next(x for x in meta['streams'] if x['codec_type']=='video');audio=next(x for x in meta['streams'] if x['codec_type']=='audio')
 assert video['codec_name']=='h264' and audio['codec_name']=='aac',(key,'codecs')
 assert (video['width'],video['height'])==(width,height),(key,'dimensions')
 assert Fraction(video['r_frame_rate'])==30,(key,'fps')
 assert abs(float(meta['format']['duration'])-duration)<.08,(key,'duration')
 assert abs(float(video['duration'])-float(audio['duration']))<.08,(key,'A/V duration')
 assert int(audio['sample_rate'])==48000 and audio['channels']==2,(key,'stereo sample rate')
 subprocess.run([str(ffmpeg),'-v','error','-i',str(file),'-f','null','-'],check=True)
 decoded=np.frombuffer(subprocess.check_output([str(ffmpeg),'-v','error','-i',str(file),'-vn','-ac','1','-ar','48000','-f','s16le','-']),'<i2').astype(np.float32)
 with wave.open(str(ROOT/'assets/audio'/('master-'+key+'.wav')),'rb') as wav:
  original=np.frombuffer(wav.readframes(wav.getnframes()),'<i2').astype(np.float32).reshape(-1,wav.getnchannels()).mean(axis=1)
 count=min(len(decoded),len(original));correlation=float(np.corrcoef(decoded[:count:8],original[:count:8])[0,1])
 assert correlation>.98,(key,'encoded audio differs from current master',correlation)
 # Measure encoded audio itself: AAC can shift inter-sample peaks slightly.
 analysis=subprocess.run([str(ffmpeg),'-hide_banner','-nostats','-i',str(file),'-vn','-af',
  'loudnorm=I=-14:TP=-1:LRA=11:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
 audio_stats=json.loads(re.findall(r'\{\s*"input_i"[\s\S]*?\}',analysis.stderr)[-1])
 assert float(audio_stats['input_tp'])<0,(key,'encoded true peak clips',audio_stats)
 out=ROOT/'review'/('rendered-'+key+'-4k');out.mkdir(parents=True,exist_ok=True)
 frames=[]
 for t in times:
  frame=out/f'{t:06.2f}s.png'
  subprocess.run([str(ffmpeg),'-y','-v','error','-ss',str(t),'-i',str(file),'-frames:v','1',str(frame)],check=True)
  im=Image.open(frame).convert('RGB');assert im.size==(width,height)
  frames.append(frame)
 tilew,tileh=(640,390) if width>height else (360,680)
 cols=3 if width>height else 4
 sheet=Image.new('RGB',(tilew*cols,tileh*((len(frames)+cols-1)//cols)),'#F4F7FC');draw=ImageDraw.Draw(sheet)
 for i,frame in enumerate(frames):
  im=Image.open(frame);im.thumbnail((tilew-12,tileh-29))
  x=(i%cols)*tilew;y=(i//cols)*tileh
  sheet.paste(im,(x,y+25));draw.text((x+8,y+6),key+' / '+frame.stem,fill='#18324D')
 sheet.save(ROOT/'review'/('rendered-'+key+'-4k-contact.jpg'),quality=90)
 shutil.copyfile(ROOT/'assets/audio'/(key+'.srt'),ROOT/'exports'/(file.stem+'.srt'))
 poster=frames[0];shutil.copyfile(poster,ROOT/'exports'/(file.stem+'-poster.png'))
 record={'id':key,'passed':True,'file':str(file.relative_to(ROOT)),'bytes':file.stat().st_size,'duration':float(meta['format']['duration']),
  'width':width,'height':height,'fps':30,'videoCodec':video['codec_name'],'audioCodec':audio['codec_name'],'sampleRate':48000,'audioChannels':audio['channels'],
  'videoSeconds':float(video['duration']),'audioSeconds':float(audio['duration']),'reviewFrames':len(frames),'audioMasterCorrelation':round(correlation,6),
  'encodedLUFS':float(audio_stats['input_i']),'encodedTruePeakDBTP':float(audio_stats['input_tp']),
  'fullDecodePassed':True,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
 report['films'].append(record);print(key,'PASS',f'{file.stat().st_size/1024/1024:.1f} MiB',flush=True)
report['passed']=report['passed'] and all(f.get('passed',False) for f in report['films'])
report_path.write_text(json.dumps(report,indent=2),encoding='utf-8')
if not report['passed']:raise SystemExit(1)
