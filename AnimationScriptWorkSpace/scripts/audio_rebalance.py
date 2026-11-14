"""Raise the approved original score, preserving corrected narration and timing."""
from pathlib import Path
import hashlib,json,shutil
import numpy as np
import soundfile as sf
import pyloudnorm as pyln
from scipy.ndimage import maximum_filter1d, gaussian_filter1d
from scipy.signal import resample_poly

ROOT=Path(__file__).resolve().parents[1];AUDIO=ROOT/'assets/audio';RATE=48000
PROFILE=json.loads((ROOT/'data/delivery.json').read_text(encoding='utf-8'))
BACKUP=AUDIO/'before-music-rebalance'
current_manifest=json.loads((ROOT/'audio-manifest.json').read_text(encoding='utf-8'))
baseline_files=[BACKUP/'audio-manifest.json',*[BACKUP/f'{kind}-{key}.wav' for key in PROFILE['variants'] for kind in ['voice','music','master']]]
if current_manifest.get('deliveryProfile')==PROFILE['profile'] and not all(p.exists() for p in baseline_files):
    raise SystemExit('The fixed pre-rebalance baseline was removed. Current audio already uses this profile; restore that baseline before re-running to avoid applying the gain twice.')
BACKUP.mkdir(exist_ok=True)
METER=pyln.Meter(RATE)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def lufs(x):return float(METER.integrated_loudness(x))
def true_peak(x):return max(float(np.abs(resample_poly(x[:,i],4,1)).max()) for i in range(x.shape[1]))

timings=[ROOT/'data/scenes.json',*[AUDIO/(k+s) for k in PROFILE['variants'] for s in ['.srt','-captions.json']]]
hashes={str(p.relative_to(ROOT)):sha(p) for p in timings}
for filename in ['audio-manifest.json']:
    if not (BACKUP/filename).exists():shutil.copy2(ROOT/filename,BACKUP/filename)
if not (BACKUP/'delivery-mix-report.json').exists():shutil.copy2(AUDIO/'delivery-mix-report.json',BACKUP/'delivery-mix-report.json')
manifest=json.loads((BACKUP/'audio-manifest.json').read_text(encoding='utf-8'))
rows=[];boost=PROFILE['audio']['musicGainRelativeV2DB'];ceiling=10**(PROFILE['audio']['truePeakCeilingDBTP']/20)
for film in manifest['films']:
    key=film['id']
    for kind in ['voice','music','master']:
        path=AUDIO/f'{kind}-{key}.wav'
        if not (BACKUP/path.name).exists():shutil.copy2(path,BACKUP/path.name)
    voice,sr=sf.read(BACKUP/f'voice-{key}.wav',dtype='float32');assert sr==RATE
    music,sr=sf.read(BACKUP/f'music-{key}.wav',dtype='float32');assert sr==RATE and music.ndim==2
    old_music=lufs(music);old_voice=lufs(voice)
    music*=10**(boost/20);master=voice[:,None]+music
    # Offline look-ahead gain control preserves the duration. Measure every
    # delivered channel with fourfold oversampling after limiting.
    peaks=maximum_filter1d(np.abs(master).max(axis=1),size=961,mode='nearest')
    gain=np.minimum(1,ceiling/np.maximum(peaks,1e-8))
    gain=np.minimum(gain,gaussian_filter1d(gain,480,mode='nearest'))
    voice*=gain;music*=gain[:,None];master=voice[:,None]+music
    trim=min(1.,ceiling/true_peak(master));voice*=trim;music*=trim;master*=trim
    for kind,samples in [('voice',voice),('music',music),('master',master)]:sf.write(AUDIO/f'{kind}-{key}.wav',samples,RATE,subtype='PCM_16')
    delivered,_=sf.read(AUDIO/f'master-{key}.wav',dtype='float32')
    tp=20*np.log10(true_peak(delivered));assert tp<=-1.17
    rec={'id':key,'seconds':len(voice)/RATE,'musicGainRequestedDB':boost,
         'oldMusicLUFS':round(old_music,3),'newMusicLUFS':round(lufs(music),3),
         'musicIncreaseLU':round(lufs(music)-old_music,3),
         'voiceChangeFromV2LU':round(lufs(voice)-old_voice,3),
         'newVoiceLUFS':round(lufs(voice),3),'newMixLUFS':round(lufs(delivered),3),
         'truePeakDBTP':round(float(tp),3),'channels':2,'sampleRate':RATE,
         'mixSha256':sha(AUDIO/f'master-{key}.wav')}
    assert rec['voiceChangeFromV2LU']>-.5,(key,'too much narration attenuation',rec)
    film['deliveryMix']=rec;film['peak']=round(float(np.abs(delivered).max()),6)
    film['rms']=round(float(np.sqrt(np.mean(delivered*delivered))),6)
    rows.append(rec);print(json.dumps(rec,ensure_ascii=False),flush=True)
    del voice,music,master,delivered,peaks,gain
manifest['deliveryProfile']=PROFILE['profile'];manifest['deliveryAudioChannels']=2
(ROOT/'audio-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
unchanged={name:sha(ROOT/name)==value for name,value in hashes.items()};assert all(unchanged.values())
report={'passed':True,'profile':PROFILE['profile'],'originalMusic':True,'newTTSRequests':0,
        'musicGainFromPreviousMixDB':boost,'timingFilesUnchanged':unchanged,'films':rows}
(AUDIO/'delivery-mix-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
samples,_=sf.read(AUDIO/'master-overview.wav',dtype='float32',frames=18*RATE)
sf.write(AUDIO/'delivery-mix-audition.wav',samples,RATE,subtype='PCM_16')
print('PASS: background music raised; narration, timing and pronunciation preserved.',flush=True)
