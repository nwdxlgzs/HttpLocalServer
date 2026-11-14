"""Deterministic, local SAPI Chinese voice + original ambient audio production.

Requires Python 3.10+ with numpy and Windows System.Speech via PowerShell.
The word/syllable events are actual SAPI SpeakProgress AudioPosition events,
not an automatic speech-recognition transcript. Subtitle groups derive from
those native positions (with fallback phrase estimates explicitly identified).
"""
from __future__ import annotations
import argparse
import audioop
import datetime
import hashlib
import json
import math
import pathlib
import re
import shutil
import subprocess
import sys
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
AUDIO = ROOT / 'assets' / 'audio'
PLAN = ROOT.parent / 'AnimationScriptPlan.md'
RATE = 48000
POWERSHELL = shutil.which('pwsh') or shutil.which('pwsh.exe') or 'powershell.exe'


def spoken(text):
    substitutions = {
        'HttpLocalServer':'HTTP Local Server',
        'FileTrans':'文件传输模式',
        'QuickJS':'Quick J S',
        'WAMR':'W A M R',
        'Wasm':'Web Assembly',
        'PCDN':'P C D N',
        'MCP':'M C P',
        'HTTPS':'H T T P S',
        'HTTP':'H T T P',
        'UTF-8':'U T F 八',
        'IP':'I P',
        'CLI':'C L I',
        'COW':'C O W',
        'ZIP':'Zip',
        'JSON':'J son',
        'PPT':'P P T',
        'PDF':'P D F',
        'AI':'A I',
        '2MiB':'两兆字节',
    }
    # Match all substitutions once so newly substituted HTTP isn't expanded twice.
    return re.sub('|'.join(re.escape(x) for x in sorted(substitutions,key=len,reverse=True)),
                  lambda m: substitutions[m.group()], text)


def create_manifest():
    source = PLAN.read_text(encoding='utf-8')
    timing = re.findall(r'^### (S\d{2})｜(.+?)｜(\d{2}):(\d{2})–(\d{2}):(\d{2})（(\d+)秒）$', source, re.M)
    vo = dict(re.findall(r'^\| (S\d{2}) \| ([^|]+) \|$', source, re.M))
    main = []
    for cue_id,title,sm,ss,em,es,duration in timing:
        main.append({'id':cue_id,'title':title,'start':int(sm)*60+int(ss),
                     'duration':int(duration),'text':vo[cue_id].strip(),'rate':0})
    films = [{'id':'main','duration':480,'cues':main}]
    overview = [
        (0,8,'文件与网页，交给同一个局域网入口。'),
        (8,10,'启动HttpLocalServer，用浏览器访问。一个程序，先把服务提供出来。'),
        (18,13,'熟悉的管理器，多标签、检索与按需列表，找到需要的内容。'),
        (31,12,'大文件分块传输，目录按能力保存，写操作先确认冲突。'),
        (43,13,'先预览，再下载。文档、表格、媒体和代码，各有打开方式。'),
        (56,12,'谁能写、开放什么、资源怎么用，由主机设定。'),
        (68,12,'它也能托管网站，以可信脚本和受限组件扩展轻量应用。'),
        (80,10,'让本机，成为局域网的服务入口。HttpLocalServer。')]
    teaser = [
        (0,4,'本机内容，局域网可达。'),
        (4,5,'一个程序，启动HTTP服务。'),
        (9,6,'文件浏览、预览与传输。'),
        (15,7,'开放文件，也开放网页与轻量应用。'),
        (22,4,'能力可配，资源有界。'),
        (26,4,'HttpLocalServer。')]
    vertical = [
        (0,5,'电脑上的内容，交给同一网络的人。'),
        (5,7,'启动一个HTTP入口，用浏览器访问。'),
        (12,8,'浏览、预览、下载，找到需要的资料。'),
        (20,8,'可写范围和传输规则，由主机设定。'),
        (28,8,'它也能托管网页，运行轻量应用。'),
        (36,5,'按需求增强，资源有边界。'),
        (41,4,'HttpLocalServer。')]
    for film_id,prefix,duration,cues in [('overview','O',90,overview),('teaser','T',30,teaser),('vertical','V',45,vertical)]:
        films.append({'id':film_id,'duration':duration,'cues':[{'id':f'{prefix}{i:02d}',
            'title':f'{film_id} {i}','start':start,'duration':length,'text':text,
            'rate':0} for i,(start,length,text) in enumerate(cues,1)]})
    for film in films:
        for cue in film['cues']:
            cue['spoken']=spoken(cue['text'])
    scenes_path=ROOT/'data/scenes.json'
    if scenes_path.exists():
        scene_data=json.loads(scenes_path.read_text(encoding='utf-8'))
        for film in films:
            for cue in film['cues']:
                actual=next(item for item in scene_data['variants'][film['id']]['scenes'] if item['id']==cue['id'])
                cue.update(text=actual['narration'],start=actual['start'],duration=actual['duration'])
                cue['spoken']=spoken(cue['text'])
    return {'version':1,'generatedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'engine':'Windows System.Speech/SAPI','selectedVoice':'Microsoft Yaoyao',
            'sampleRate':RATE,'films':films}


def write_json(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def read_wave(path):
    with wave.open(str(path),'rb') as source:
        assert source.getframerate() in (16000,RATE) and source.getnchannels()==1 and source.getsampwidth()==2
        raw=source.readframes(source.getnframes())
        if source.getframerate()!=RATE:
            raw=audioop.ratecv(raw,2,1,source.getframerate(),RATE,None)[0]
        return np.frombuffer(raw,'<i2').astype(np.float32)/32768


def write_wave(path,data):
    assert np.max(np.abs(data))<1,'Clipping: '+str(path)
    values=np.round(np.clip(data,-.9999,.9999)*32767).astype('<i2')
    with wave.open(str(path),'wb') as output:
        output.setnchannels(1 if values.ndim==1 else values.shape[1])
        output.setsampwidth(2);output.setframerate(RATE);output.writeframes(values.tobytes())


def timestamp(value):
    ms=round(value*1000);return f'{ms//3600000:02d}:{(ms//60000)%60:02d}:{(ms//1000)%60:02d},{ms%1000:03d}'


def music_bed(duration, seed=351):
    """Original, deterministic sparse Fmaj7/Dm7/Bbmaj7/Cadd9 ambient bed.

    No third-party melody, sample or commercial recording. Oscillators,
    envelopes, seeded filtered noise and a restrained low pulse only.
    """
    total=round(duration*RATE)
    output=np.zeros(total,np.float32)
    rng=np.random.default_rng(seed)
    bpm=100;beat=60/bpm;bar=beat*4
    chords=[(53,57,60,64),(50,53,57,60),(46,50,53,57),(48,52,55,62)]
    for n,at in enumerate(np.arange(0,duration,bar*2)):
        chord=chords[n%len(chords)];length=min(bar*2+.8,duration-at)
        t=np.arange(round(length*RATE),dtype=np.float32)/RATE
        env=np.minimum(t/.65,1)*np.minimum(np.maximum(length-t,0)/.9,1)
        pad=np.zeros_like(t)
        for k,note in enumerate(chord):
            freq=440*2**((note-69)/12)
            pad+=(np.sin(2*np.pi*freq*t+.19*k)+.23*np.sin(2*np.pi*freq*2*t+.3*k))/len(chord)
        pad*=env*.022
        start=round(at*RATE);output[start:start+len(pad)]+=pad
    for n,at in enumerate(np.arange(0,duration,beat)):
        if at<1 or at>duration-2:continue
        length=min(.12,duration-at);t=np.arange(round(length*RATE),dtype=np.float32)/RATE
        # Alternating sparse tick and a very quiet round kick.
        if n%4==0:
            pulse=np.sin(2*np.pi*(66*t+17*(1-np.exp(-t*32))/32))*np.exp(-t*34)*.018
        elif n%2==0:
            noise=rng.normal(0,1,len(t)).astype(np.float32)
            pulse=np.concatenate(([0],np.diff(noise)))*np.exp(-t*65)*.0025
        else:continue
        start=round(at*RATE);output[start:start+len(pulse)]+=pulse
    fade=round(min(2,duration/8)*RATE)
    output[:fade]*=np.linspace(0,1,fade,dtype=np.float32)
    output[-fade:]*=np.linspace(1,0,fade,dtype=np.float32)
    return output


def cue_subtitles(cue,raw_length,events,offset):
    # Original display text can differ from spoken pronunciation replacements.
    # Captions are whole short phrases, timed by the relative spoken progress.
    phrases=[x for x in re.split(r'(?<=[，。！？；：])',cue['text']) if x.strip()]
    groups=[];current=''
    for phrase in phrases:
        if current and len(current)+len(phrase)>23:
            groups.append(current);current=phrase
        else:current+=phrase
    if current:groups.append(current)
    # At the voice-native 16 kHz, SAPI's progress events correspond to the
    # finished PCM. Pronunciation replacements preserve prefix boundaries.
    weights=[max(1,len(spoken(g).replace(' ',''))) for g in groups];total=sum(weights)
    subtitles=[];starts=[];modes=[];display_prefix='';passed=0
    for text,weight in zip(groups,weights):
        position=len(spoken(display_prefix))
        candidates=[event for event in events if event['position']>=position and event['start']<raw_length]
        if candidates:
            starts.append(offset+candidates[0]['start']);modes.append('native SAPI SpeakProgress phrase boundary; not ASR')
        else:
            starts.append(offset+raw_length*passed/total);modes.append('phrase-length estimate within measured synthesized cue')
        display_prefix+=text;passed+=weight
    for i,(text,start,mode) in enumerate(zip(groups,starts,modes)):
        end=starts[i+1]-.035 if i+1<len(starts) else offset+raw_length
        end=max(start+.08,end)
        subtitles.append({'id':cue['id'],'start':round(start,4),'end':round(end,4),'text':text,'timing':mode})
    return subtitles


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mix-only',action='store_true');args=parser.parse_args()
    AUDIO.mkdir(parents=True,exist_ok=True)
    manifest_path=ROOT/'audio-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8')) if args.mix_only else create_manifest()
    write_json(manifest_path,manifest)
    if not args.mix_only:
        ps=ROOT/'scripts/audio_sapi.ps1'
        subprocess.run([POWERSHELL,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(ps),'-Manifest',str(manifest_path)],check=True)
    # If any cue does not fit, rerender only that cue with a modest engine rate.
    for film in manifest['films']:
        for cue in film['cues']:
            path=AUDIO/'raw'/f"{cue['id']}.wav";samples=read_wave(path);duration=len(samples)/RATE
            padding=.65 if cue['duration']>=8 else .35
            available=cue['duration']-padding-(.8 if cue['duration']>=8 else .25)
            if duration>available:
                for rate in (1,2,3):
                    tiny=dict(manifest);tiny['films']=[{'id':'single','cues':[dict(cue,rate=rate)]}]
                    temp=ROOT/'assets/audio/rerender.json';write_json(temp,tiny)
                    subprocess.run([POWERSHELL,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'scripts/audio_sapi.ps1'),'-Manifest',str(temp)],check=True)
                    if read_wave(path).shape[0]/RATE<=available:break
                if read_wave(path).shape[0]/RATE>available:
                    raise RuntimeError(f'{cue["id"]} narration exceeds slot: {duration:.2f}s > {available:.2f}s')
    transcript={'source':'Native 16kHz SAPI SpeakProgress events, resampled to 48kHz PCM; not ASR. Fallback estimated captions explicitly marked.', 'films':[]}
    for film in manifest['films']:
        total=round(film['duration']*RATE);voice=np.zeros(total,np.float32);subtitles=[];word_events=[]
        for cue in film['cues']:
            raw=AUDIO/'raw'/f"{cue['id']}.wav";samples=read_wave(raw)
            meta=json.loads(pathlib.Path(str(raw)+'.json').read_text(encoding='utf-8-sig'))
            assert all(event['start']<len(samples)/RATE for event in meta['events']), 'Invalid SAPI position: '+cue['id']
            padding=.65 if cue['duration']>=8 else .35
            offset=cue['start']+padding;start=round(offset*RATE)
            # Normalize each voice cue gently, with a common sensible peak.
            peak=float(np.max(np.abs(samples)))
            if peak>0:samples*=min(1.3,.70/peak)
            voice[start:start+len(samples)]+=samples
            write_wave(AUDIO/f"{cue['id']}.wav",samples)
            cue.update({'audio':f"assets/audio/{cue['id']}.wav",'voiceOffset':padding,
                        'measuredVoiceSeconds':round(len(samples)/RATE,5),'voiceEnd':round(offset+len(samples)/RATE,5),
                        'voice':meta['voice'],'rate':meta['rate'],'sha256':hashlib.sha256((AUDIO/f"{cue['id']}.wav").read_bytes()).hexdigest()})
            subtitles.extend(cue_subtitles(cue,len(samples)/RATE,meta['events'],offset))
            for event in meta['events']:
                word_events.append(dict(event,cue=cue['id'],absoluteStart=round(offset+event['start'],5)))
        bed=music_bed(film['duration'])
        # Approximate speech ducking with a smoothed envelope. Base music is quiet.
        envelope=np.convolve(np.abs(voice[::480]),np.ones(25)/25,mode='same')
        duck=np.where(envelope>.003,.50,1).astype(np.float32)
        duck=np.repeat(duck,480)[:total];bed*=duck
        mix=voice+bed
        maximum=float(np.max(np.abs(mix)))
        if maximum>.92:mix*=.92/maximum
        write_wave(AUDIO/f'voice-{film["id"]}.wav',voice)
        write_wave(AUDIO/f'music-{film["id"]}.wav',bed)
        write_wave(AUDIO/f'master-{film["id"]}.wav',mix)
        srt='\n\n'.join(str(i)+'\n'+timestamp(c['start'])+' --> '+timestamp(c['end'])+'\n'+c['text'] for i,c in enumerate(subtitles,1))+'\n'
        (AUDIO/f'{film["id"]}.srt').write_text(srt,encoding='utf-8')
        write_json(AUDIO/f'{film["id"]}-captions.json',subtitles)
        write_json(AUDIO/f'{film["id"]}-speech-events.json',word_events)
        film.update({'voiceFile':f'assets/audio/voice-{film["id"]}.wav',
                     'musicFile':f'assets/audio/music-{film["id"]}.wav','mixFile':f'assets/audio/master-{film["id"]}.wav',
                     'subtitles':f'assets/audio/{film["id"]}.srt','peak':round(float(np.max(np.abs(mix))),5),
                     'rms':round(float(np.sqrt(np.mean(mix*mix))),5)})
        transcript['films'].append({'id':film['id'],'duration':film['duration'],'captions':subtitles,'speechEvents':word_events})
        print('Mixed',film['id'],film['duration'],'seconds; peak',film['peak'],flush=True)
    write_json(manifest_path,manifest);write_json(ROOT/'transcript.json',transcript)
    script=['# Narration script\n','Chinese voice: Microsoft Yaoyao; all synthesis stays local.\n',
            'Pronunciation substitutions are recorded as spoken text. Captions preserve the approved display copy.\n',
            'Phrase boundaries use native SAPI progress events; any fallback estimate is marked in captions JSON. Native 16kHz voice is resampled to 48kHz after synthesis to keep timestamps correct. This is synthesized voice alignment, not ASR.\n']
    for film in manifest['films']:
        script+=['\n## '+film['id']+' · '+str(film['duration'])+' seconds\n']
        for cue in film['cues']:
            script += [f"\n### {cue['id']} · {cue['start']}–{cue['start']+cue['duration']}s\n",cue['text']+'\n',
                       'Spoken: '+cue['spoken']+'\n',f"Measured voice: {cue['measuredVoiceSeconds']}s, offset {cue['voiceOffset']}s.\n"]
    (ROOT/'SCRIPT.md').write_text('\n'.join(script),encoding='utf-8')
    (AUDIO/'narration.txt').write_text('\n'.join(c['spoken'] for f in manifest['films'] for c in f['cues'])+'\n',encoding='utf-8')


if __name__=='__main__':main()
