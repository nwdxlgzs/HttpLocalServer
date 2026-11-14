"""Mix approved paid narration, preserving local voice and honest captions."""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import wave

import numpy as np
from scipy.signal import resample_poly

import audio_build as local

ROOT = pathlib.Path(__file__).resolve().parents[1]
AUDIO = ROOT / 'assets/audio'
CLOUD = AUDIO / 'cloud'
RATE = 48000


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def read_audio(path):
    with wave.open(str(path), 'rb') as media:
        raw = np.frombuffer(media.readframes(media.getnframes()), '<i2').astype(np.float32) / 32768
        assert media.getnchannels() == 1 and media.getsampwidth() == 2
        source_rate = media.getframerate()
    if source_rate != RATE:
        from math import gcd
        divisor = gcd(RATE, source_rate)
        raw = resample_poly(raw, RATE // divisor, source_rate // divisor).astype(np.float32)
    # Preserve 80 ms before and 120 ms after audible content. This removes
    # service transport padding, not phonemes, pauses within speech or words.
    active = np.flatnonzero(np.abs(raw) > max(.0015, float(np.abs(raw).max()) * .006))
    if len(active):
        raw = raw[max(0, active[0] - round(.08 * RATE)):min(len(raw), active[-1] + round(.12 * RATE))]
    return raw


def groups_for(text):
    phrases = [part for part in re.split(r'(?<=[，。！？；：])', text) if part.strip()]
    groups = []
    current = ''
    for phrase in phrases:
        if current and len(current) + len(phrase) > 23:
            groups.append(current)
            current = phrase
        else:
            current += phrase
    if current:
        groups.append(current)
    return groups


def captions_for(cue, samples, absolute_offset):
    """Approximate phrase boundaries using real silent gaps, never claim ASR."""
    groups = groups_for(cue['text'])
    length = len(samples) / RATE
    block_size = 480
    blocks = len(samples) // block_size
    rms = np.sqrt(np.mean(samples[:blocks * block_size].reshape(blocks, block_size) ** 2, axis=1))
    threshold = max(.002, float(np.quantile(rms, .70)) * .16)
    silent = rms < threshold
    gaps = []
    beginning = None
    for index, is_silent in enumerate(np.append(silent, False)):
        if is_silent and beginning is None:
            beginning = index
        if not is_silent and beginning is not None:
            if index - beginning >= 6:
                gaps.append((beginning + index) * .005)
            beginning = None
    weights = [max(1, len(re.sub(r'[^\w\u4e00-\u9fff]', '', group))) for group in groups]
    starts = [0.0]
    completed = 0
    for group_index in range(1, len(groups)):
        completed += weights[group_index - 1]
        ideal = length * completed / sum(weights)
        candidates = [gap for gap in gaps if abs(gap - ideal) <= min(.7, length * .20)
                      and gap > starts[-1] + .25 and gap < length - .25]
        boundary = min(candidates, key=lambda gap: abs(gap - ideal)) if candidates else ideal
        starts.append(max(starts[-1] + .10, boundary))
    output = []
    for index, group in enumerate(groups):
        end = starts[index + 1] - .035 if index + 1 < len(groups) else length
        output.append({'id': cue['id'], 'start': round(absolute_offset + starts[index], 4),
                       'end': round(absolute_offset + end, 4), 'text': group,
                       'timing': 'measured-audio silence guided phrase estimate; not ASR'})
    return output


def save_local_fallback():
    fallback = AUDIO / 'local-sapi'
    fallback.mkdir(exist_ok=True)
    saved_manifest = fallback / 'audio-manifest.json'
    if saved_manifest.exists():
        return read_json(saved_manifest)
    old = read_json(ROOT / 'audio-manifest.json')
    if old['engine'] != 'Windows System.Speech/SAPI':
        raise RuntimeError('Cannot create original local fallback from a non-local manifest.')
    shutil.copy2(ROOT / 'audio-manifest.json', saved_manifest)
    for name in ('SCRIPT.md', 'transcript.json'):
        shutil.copy2(ROOT / name, fallback / name)
    for film in old['films']:
        for field in ('voiceFile', 'musicFile', 'mixFile', 'subtitles'):
            source = ROOT / film[field]
            shutil.copy2(source, fallback / source.name)
        for ending in ('captions.json', 'speech-events.json'):
            source = AUDIO / (film['id'] + '-' + ending)
            shutil.copy2(source, fallback / source.name)
        for cue in film['cues']:
            source = ROOT / cue['audio']
            shutil.copy2(source, fallback / source.name)
    return old


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--voice', choices=('stella', 'tide'), default='stella')
    parser.add_argument('--preserve-captions', action='store_true')
    args = parser.parse_args()
    cloud_manifest = read_json(CLOUD / ('all-' + args.voice + '-manifest.json'))
    records = {record['id']: record for record in cloud_manifest['cues']}
    qr_manifest = CLOUD / ('qr-' + args.voice + '-manifest.json')
    if qr_manifest.exists():
        records.update({record['id']: record for record in read_json(qr_manifest)['cues']})
    overrides_path = CLOUD / 'pronunciation-overrides.json'
    pronunciation_overrides = read_json(overrides_path)['records'] if overrides_path.exists() else []
    records.update({record['id']: record for record in pronunciation_overrides})
    preserve_captions = bool(pronunciation_overrides) or args.preserve_captions
    # Validate every paid cue exists before touching the final delivery files.
    for record in records.values():
        assert (ROOT / record['file']).exists(), record['id']
    save_local_fallback()
    scene_data = read_json(ROOT / 'data/scenes.json')
    manifest = {'version': 2, 'sampleRate': RATE,
                'timingProfile': scene_data.get('timingProfile', 'long-form'),
                'transitionSeconds': scene_data.get('transitionSeconds', .35), 'films': []}
    for film_id, variant in scene_data['variants'].items():
        cues = []
        for scene in variant['scenes']:
            cue = {'id': scene['id'], 'title': scene['planTitle'], 'start': scene['start'],
                   'duration': scene['duration'], 'text': scene['narration'],
                   'audioSourceId': scene.get('audioSourceId', scene['id']),
                   'plannedVoiceOffset': scene.get('voiceLead', .233333),
                   'minimumVoiceTail': scene.get('minimumVoiceTail', .35),
                   'visualHold': scene.get('visualHold', 0)}
            cues.append(cue)
        manifest['films'].append({'id': film_id, 'duration': variant['duration'], 'cues': cues,
                                 'voiceFile': 'assets/audio/voice-' + film_id + '.wav',
                                 'musicFile': 'assets/audio/music-' + film_id + '.wav',
                                 'mixFile': 'assets/audio/master-' + film_id + '.wav',
                                 'subtitles': 'assets/audio/' + film_id + '.srt'})
    manifest.update(engine='DeepSeek TTS', selectedVoice=args.voice,
                    generatedAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    captionTiming='measured-audio silence guided phrase estimate; not ASR',
                    pricingVisible=False, billingTotalVisible=False,
                    localFallback='assets/audio/local-sapi/audio-manifest.json')
    if preserve_captions:
        manifest.update(captionTiming='Approved earlier estimated phrase timings retained unchanged; pronunciation patches fit the existing cue windows. Not ASR.',
                        pronunciationReport='assets/audio/brand/pronunciation-report.json')
    stage = CLOUD / 'final-staging'
    stage.mkdir(exist_ok=True)
    transcript = {'source': 'DeepSeek synthesized voice. Caption phrase timing estimated from measured audio and silent gaps; not ASR.', 'films': []}
    script = ['# 中文旁白\n', '配音：DeepSeek TTS / ' + args.voice + '。文案和镜头时间以 data/scenes.json 为准。\n',
              '字幕依据实测音轨、短语长度与静音位置估计；不是语音识别，也不是服务端提供的逐字时间戳。\n',
              '原始本地 SAPI 版本及其事件字幕完整保存在 assets/audio/local-sapi/，可回退。\n']
    stretch_count = 0
    for film in manifest['films']:
        total = round(film['duration'] * RATE)
        voice = np.zeros(total, np.float32)
        captions = []
        script.append('\n## ' + film['id'] + ' · ' + str(film['duration']) + ' 秒\n')
        for cue in film['cues']:
            source = records[cue['audioSourceId']]
            assert source['text'] == cue['text'], cue['id']
            if source.get('preprocessed48k'):
                with wave.open(str(ROOT / source['file']), 'rb') as prepared:
                    assert (prepared.getframerate(), prepared.getnchannels(), prepared.getsampwidth()) == (RATE, 1, 2)
                    samples = np.frombuffer(prepared.readframes(prepared.getnframes()), '<i2').astype(np.float32) / 32768
            else:
                samples = read_audio(ROOT / source['file'])
            native_seconds = len(samples) / RATE
            lead = cue['plannedVoiceOffset']
            tail = cue['minimumVoiceTail']
            available = cue['duration'] - lead - tail
            speed = max(1., native_seconds / available)
            if speed > 1.00001:
                if speed > 1.35:
                    raise RuntimeError(cue['id'] + ' requires excessive acceleration; no words truncated.')
                import librosa
                adjusted = speed * 1.002
                spectrum = librosa.stft(samples, n_fft=1024, hop_length=256)
                stretched = librosa.phase_vocoder(spectrum, rate=adjusted, hop_length=256)
                samples = librosa.istft(stretched, hop_length=256,
                                        length=round(len(samples) / adjusted)).astype(np.float32)
                stretch_count += 1
            assert len(samples) / RATE <= available + .00001, cue['id']
            if not source.get('preprocessed48k'):
                samples *= .70 / float(np.abs(samples).max())
            offset = cue['start'] + lead
            first = round(offset * RATE)
            voice[first:first + len(samples)] += samples
            if source.get('preprocessed48k'):
                shutil.copy2(ROOT / source['file'], stage / (cue['id'] + '.wav'))
            else:
                local.write_wave(stage / (cue['id'] + '.wav'), samples)
            cue.update(audio='assets/audio/' + cue['id'] + '.wav', voice=args.voice,
                       spoken=source.get('spokenText', cue['text']), rate=None, sourceAudio=source['file'],
                       sourceMetadata=source['file'].replace('.wav', '.json'),
                       nativeVoiceSeconds=round(native_seconds, 5), timeStretchRate=round(speed, 6),
                       voiceOffset=lead, measuredVoiceSeconds=round(len(samples) / RATE, 5),
                       voiceEnd=round(offset + len(samples) / RATE, 5),
                       sha256=hashlib.sha256((stage / (cue['id'] + '.wav')).read_bytes()).hexdigest())
            if source.get('preprocessed48k'):
                details = source.get('pronunciation', {})
                cue['timeStretchRate'] = round(details.get('pitchPreservingTempo', 1.) * speed, 6)
                cue['pronunciationMethod'] = details.get('method', '')
                if cue['timeStretchRate'] > 1.00001:
                    stretch_count += 1
            if not preserve_captions:
                captions.extend(captions_for(cue, samples, offset))
            script.extend(['\n### ' + cue['id'] + ' · ' + str(cue['start']) + '–' + str(cue['start'] + cue['duration']) + ' 秒\n',
                           cue['text'] + '\n', '实测旁白 ' + str(cue['measuredVoiceSeconds']) + ' 秒；镜头内起点 ' + str(lead) + ' 秒。\n'])
        music = local.music_bed(film['duration'])
        envelope = np.convolve(np.abs(voice[::480]), np.ones(25) / 25, mode='same')
        music *= np.repeat(np.where(envelope > .003, .50, 1).astype(np.float32), 480)[:total]
        mix = voice + music
        peak = float(np.abs(mix).max())
        if peak > .92:
            mix *= .92 / peak
        for kind, samples in [('voice', voice), ('music', music), ('master', mix)]:
            local.write_wave(stage / (kind + '-' + film['id'] + '.wav'), samples)
        if preserve_captions:
            captions = read_json(AUDIO / (film['id'] + '-captions.json'))
            shutil.copy2(AUDIO / (film['id'] + '-captions.json'), stage / (film['id'] + '-captions.json'))
        else:
            local.write_json(stage / (film['id'] + '-captions.json'), captions)
        # No fabricated word events: this service response carries audio only.
        local.write_json(stage / (film['id'] + '-speech-events.json'), [])
        srt = '\n\n'.join(str(i) + '\n' + local.timestamp(c['start']) + ' --> ' + local.timestamp(c['end']) + '\n' + c['text'] for i, c in enumerate(captions, 1)) + '\n'
        if preserve_captions:
            shutil.copy2(AUDIO / (film['id'] + '.srt'), stage / (film['id'] + '.srt'))
        else:
            (stage / (film['id'] + '.srt')).write_text(srt, encoding='utf-8')
        film.update(peak=round(float(np.abs(mix).max()), 5), rms=round(float(np.sqrt(np.mean(mix * mix))), 5))
        transcript['films'].append({'id': film['id'], 'duration': film['duration'], 'captions': captions, 'speechEvents': []})
        print('Staged', film['id'], film['duration'], 'seconds; peak', film['peak'], flush=True)
    manifest['timeStretchedCues'] = stretch_count
    # Commit only once all versions have been constructed successfully.
    for source in stage.iterdir():
        if source.is_file():
            shutil.copy2(source, AUDIO / source.name)
    local.write_json(ROOT / 'audio-manifest.json', manifest)
    local.write_json(ROOT / 'transcript.json', transcript)
    (ROOT / 'SCRIPT.md').write_text('\n'.join(script), encoding='utf-8')
    (AUDIO / 'narration.txt').write_text('\n'.join(cue['text'] for film in manifest['films'] for cue in film['cues']) + '\n', encoding='utf-8')
    print('Committed four paid-voice editions; accelerated cues:', stretch_count, flush=True)


if __name__ == '__main__':
    main()
