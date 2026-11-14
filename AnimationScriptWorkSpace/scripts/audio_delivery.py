"""UHD edition: louder corrected narration and an original stereo music score.

No TTS request is made. Cue PCM, pronunciation edits, captions and shot timing stay intact.
"""
from pathlib import Path
import hashlib, json, math, shutil, subprocess, tempfile
import numpy as np
import soundfile as sf
import pyloudnorm as pyln
from scipy.ndimage import gaussian_filter1d, maximum_filter1d
from scipy.signal import resample_poly

ROOT=Path(__file__).resolve().parents[1]
AUDIO=ROOT/'assets/audio'
RATE=48000
PROFILE=json.loads((ROOT/'data/delivery.json').read_text(encoding='utf-8'))
CONFIG=PROFILE['audio']
METER=pyln.Meter(RATE)

def db(value):return 10.0**(value/20.0)
def loudness(samples):return float(METER.integrated_loudness(samples))
def true_peak(samples):
    # Fourfold oversampling is checked per channel to bound working memory.
    channels=[samples] if samples.ndim==1 else [samples[:,i] for i in range(samples.shape[1])]
    return max(float(np.max(np.abs(resample_poly(ch,4,1)))) for ch in channels)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def score(duration):
    """Original 108 BPM Fmaj7/Dm9/Bbmaj7/Cadd9 score, oscillator instruments only."""
    total=round(duration*RATE);out=np.zeros((total,2),np.float32)
    beat=60/108;bar=beat*4;rng=np.random.default_rng(351402)
    chords=[(53,57,60,64),(50,53,57,60,64),(46,50,53,57),(48,52,55,62)]
    def add(at,samples,pan=0.,gain=1.):
        first=round(at*RATE)
        if first>=total:return
        samples=samples[:total-first]*gain
        if first<0:samples=samples[-first:];first=0
        left=math.cos((pan+1)*math.pi/4);right=math.sin((pan+1)*math.pi/4)
        out[first:first+len(samples),0]+=samples*left
        out[first:first+len(samples),1]+=samples*right
    def note(midi,length,kind='key',vel=1.):
        t=np.arange(round(length*RATE),dtype=np.float32)/RATE
        f=440*2**((midi-69)/12)
        if kind=='pad':
            env=(1-np.exp(-t*3.3))*np.minimum(np.maximum(length-t,0)/.8,1)
            wave=(np.sin(2*np.pi*f*t)+.18*np.sin(2*np.pi*f*2.003*t)+.07*np.sin(2*np.pi*f*3*t))*.26
        elif kind=='bass':
            env=(1-np.exp(-t*65))*np.exp(-t*2.6)*np.minimum(np.maximum(length-t,0)/.07,1)
            wave=(np.sin(2*np.pi*f*t)+.18*np.sin(2*np.pi*f*2*t))*.5
        else:
            env=(1-np.exp(-t*140))*np.exp(-t*(3.7 if kind=='key' else 7.0))*np.minimum(np.maximum(length-t,0)/.055,1)
            wave=(np.sin(2*np.pi*f*t)+.32*np.sin(2*np.pi*f*2*t)*np.exp(-t*5)+.10*np.sin(2*np.pi*f*4*t)*np.exp(-t*9))*.4
        return (wave*env*vel).astype(np.float32)
    bars=math.ceil(duration/bar)
    for b in range(bars):
        at=b*bar;chord=chords[(b//2)%4];section=(b//16)%4
        # Soft held harmony, spacious upper keys and a restrained original motif.
        if b%2==0:
            for j,n in enumerate(chord):add(at,note(n,min(bar*2+.5,duration-at),'pad'),(j/(len(chord)-1)-.5)*1.2,.34)
        for step in range(8):
            n=chord[[0,2,1,3,2,1,0,2][step]%len(chord)]+12
            velocity=.72+.18*math.cos((b*8+step)*.79)
            if section==2 and step%2:continue
            add(at+step*beat/2,note(n,1.15,'key',velocity),-.4 if step%2 else .4,.24)
        if b%4==3:
            for j,n in enumerate([chord[2]+24,chord[1]+24,chord[0]+24]):
                add(at+beat*(1.5+j*.75),note(n,.9,'pluck'),[-.5,.25,.5][j],.15)
        if b>=2:
            for k in [0,1.5,2.5]:add(at+k*beat,note(chord[0]-12,.9,'bass'),0,.34)
        if b>=4 and section!=2:
            for k in [0,2]:
                t=np.arange(round(.30*RATE),dtype=np.float32)/RATE
                kick=np.sin(2*np.pi*(46*t+38*(1-np.exp(-t*35))/35))*(1-np.exp(-t*180))*np.exp(-t*20)
                add(at+k*beat,kick,0,.24)
            for k in [1,3]:
                t=np.arange(round(.13*RATE),dtype=np.float32)/RATE
                noise=rng.normal(0,1,len(t)).astype(np.float32)
                clap=np.concatenate(([0],np.diff(noise))).astype(np.float32)*np.exp(-t*38)
                clap*=1+.20*np.sin(2*np.pi*1400*t)
                add(at+k*beat,clap,-.12,.014)
            for step in range(8):
                t=np.arange(round(.08*RATE),dtype=np.float32)/RATE
                noise=rng.normal(0,1,len(t)).astype(np.float32)
                hat=np.concatenate(([0],np.diff(noise))).astype(np.float32)*np.exp(-t*75)
                add(at+step*beat/2,hat,.48 if step%2 else -.48,.006 if step%2 else .0035)
    # Low-level stereo echoes/reverb; fixed taps, no external samples.
    for delay,gain in [(.139,.14),(.278,.09),(.417,.055)]:
        shift=round(delay*RATE)
        out[shift:]+=out[:-shift,::-1]*gain
    fadein=round(min(1.2,duration/12)*RATE);fadeout=round(min(2.2,duration/10)*RATE)
    out[:fadein]*=np.linspace(0,1,fadein,dtype=np.float32)[:,None]
    out[-fadeout:]*=np.linspace(1,0,fadeout,dtype=np.float32)[:,None]
    return out

def main():
    backup=AUDIO/'before-4k-mix';backup.mkdir(exist_ok=True)
    old_manifest=backup/'audio-manifest.json'
    if not old_manifest.exists():shutil.copy2(ROOT/'audio-manifest.json',old_manifest)
    manifest=json.loads(old_manifest.read_text(encoding='utf-8-sig'))
    timing_files=[ROOT/'data/scenes.json',*[AUDIO/(v+s) for v in PROFILE['variants'] for s in ['-captions.json','.srt']]]
    original_hash={str(p.relative_to(ROOT)):sha(p) for p in timing_files}
    reports=[]
    for film in manifest['films']:
        name=film['id'];total=round(film['duration']*RATE)
        for kind in ['voice','music','master']:
            current=AUDIO/f'{kind}-{name}.wav';saved=backup/current.name
            if not saved.exists():shutil.copy2(current,saved)
        original,sr=sf.read(backup/f'voice-{name}.wav',dtype='float32');assert sr==RATE and original.ndim==1
        prior_mix,sr=sf.read(backup/f'master-{name}.wav',dtype='float32')
        old_lufs=loudness(original);target=min(old_lufs+CONFIG['voiceLoudnessIncreaseLU'],CONFIG['voiceMaximumLUFS'])
        with tempfile.TemporaryDirectory(prefix='audio-delivery-',dir=ROOT/'tools') as tmp:
            cleaned=Path(tmp)/'compressed.wav'
            subprocess.run([str(ROOT/'tools/bin/ffmpeg.exe'),'-v','error','-y','-i',str(backup/f'voice-{name}.wav'),'-af',
                'highpass=f=65,acompressor=threshold=0.18:ratio=2.0:attack=4:release=90:makeup=1:knee=2.4',
                '-c:a','pcm_f32le',str(cleaned)],check=True)
            voice,sr=sf.read(cleaned,dtype='float32');assert len(voice)==total
        voice*=db(target-loudness(voice))
        music=score(film['duration']);music*=db(CONFIG['musicIdleLUFS']-loudness(music))
        pad=(-len(voice))%480
        blocks=np.pad(voice,(0,pad)).reshape(-1,480)
        active=(np.sqrt(np.mean(blocks*blocks,axis=1))>.006).astype(np.float32)
        activity=gaussian_filter1d(maximum_filter1d(active,size=41,mode='nearest'),8,mode='nearest')
        gain=db(CONFIG['musicSpeechDuckDB'])+(1-db(CONFIG['musicSpeechDuckDB']))*(1-activity)
        music*=np.repeat(gain,480)[:total,None]
        master=voice[:,None]+music
        tp=true_peak(master);attenuation=min(1.,db(CONFIG['truePeakCeilingDBTP'])/tp)
        voice*=attenuation;music*=attenuation;master*=attenuation
        for kind,samples in [('voice',voice),('music',music),('master',master)]:
            sf.write(AUDIO/f'{kind}-{name}.wav',samples,RATE,subtype='PCM_16')
        # Re-measure actual quantized delivery audio.
        delivered,sr=sf.read(AUDIO/f'master-{name}.wav',dtype='float32')
        true_peak_db=20*np.log10(true_peak(delivered));assert true_peak_db<=-1.17,(name,true_peak_db)
        rec={'id':name,'seconds':total/RATE,'oldMixLUFS':round(loudness(prior_mix),3),
             'newMixLUFS':round(loudness(delivered),3),'oldVoiceLUFS':round(old_lufs,3),
             'newVoiceLUFS':round(loudness(voice),3),'voiceIncreaseLU':round(loudness(voice)-old_lufs,3),
             'musicLUFS':round(loudness(music),3),'musicIdleTargetLUFS':CONFIG['musicIdleLUFS'],
             'musicSpeechDuckDB':CONFIG['musicSpeechDuckDB'],'truePeakDBTP':round(float(true_peak_db),3),
             'channels':2,'sampleRate':RATE,'mixSha256':sha(AUDIO/f'master-{name}.wav')}
        film.update(peak=round(float(np.abs(delivered).max()),6),rms=round(float(np.sqrt(np.mean(delivered*delivered))),6),deliveryMix=rec)
        reports.append(rec);print(json.dumps(rec,ensure_ascii=False),flush=True)
        del original,prior_mix,voice,music,master,delivered
    manifest['deliveryProfile']=PROFILE['profile'];manifest['deliveryAudioChannels']=2
    (ROOT/'audio-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    unchanged={k:sha(ROOT/k)==v for k,v in original_hash.items()};assert all(unchanged.values())
    report={'passed':True,'profile':PROFILE['profile'],'originalMusic':True,
            'score':'Original 108 BPM Fmaj7/Dm9/Bbmaj7/Cadd9; soft keys, stereo pad, bass, kick and seeded percussion.',
            'externalSamples':False,'newTTSRequests':0,'timingFilesUnchanged':unchanged,'films':reports}
    (AUDIO/'delivery-mix-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    excerpt,_=sf.read(AUDIO/'master-overview.wav',dtype='float32',frames=round(18*RATE))
    sf.write(AUDIO/'delivery-mix-audition.wav',excerpt,RATE,subtype='PCM_16')
    print('PASS: louder narration + original stereo score; corrected cue PCM and captions preserved.',flush=True)

if __name__=='__main__':
    if 'musicGainRelativeV2DB' in CONFIG:raise SystemExit('Current delivery uses the stronger music mix. Run scripts/audio_rebalance.py; audio_delivery.py is the historical v2 stem builder.')
    main()
