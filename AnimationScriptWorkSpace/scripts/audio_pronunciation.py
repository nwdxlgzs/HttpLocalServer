"""Patch technical pronunciation inside locked picture/subtitle time windows."""
from __future__ import annotations
import hashlib
import json
import pathlib
import re
import shutil
import wave

import numpy as np

from audio_cloud_mix import read_audio

ROOT = pathlib.Path(__file__).resolve().parents[1]
AUDIO = ROOT / 'assets/audio'
CLOUD = AUDIO / 'cloud'
BRAND = AUDIO / 'brand'
BACKUP = AUDIO / 'before-pronunciation'
RATE = 48000


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pcm(path):
    with wave.open(str(path), 'rb') as media:
        assert (media.getframerate(), media.getnchannels(), media.getsampwidth()) == (RATE, 1, 2)
        return np.frombuffer(media.readframes(media.getnframes()), '<i2').astype(np.float32) / 32768


def save(path, samples):
    assert float(np.abs(samples).max()) < .99
    with wave.open(str(path), 'wb') as media:
        media.setnchannels(1); media.setsampwidth(2); media.setframerate(RATE)
        media.writeframes(np.round(samples * 32768).astype('<i2').tobytes())


def quiet_gaps(samples):
    hop = 240
    count = len(samples) // hop
    rms = np.sqrt(np.mean(samples[:count * hop].reshape(count, hop) ** 2, axis=1))
    quiet = rms < .0007
    start = None
    gaps = []
    for index, flag in enumerate(np.append(quiet, False)):
        if flag and start is None:
            start = index
        if not flag and start is not None:
            if index - start >= 12:
                gaps.append((start * hop, min(index * hop, len(samples))))
            start = None
    return gaps


def fit(samples, target_frames, tail_padding=False, extra_existing_frames=0):
    """Remove only quiet-gap centers, then <=1.10 pitch-preserving tempo."""
    edits = []
    speed = 1.0
    original_target = target_frames
    delta = len(samples) - target_frames
    if delta > 0:
        candidates = []
        for start, end in quiet_gaps(samples):
            at_edge = start == 0 or end >= len(samples) - 240
            keep = round((.04 if start == 0 else .08 if at_edge else .12) * RATE)
            capacity = max(0, end - start - keep)
            if capacity:
                candidates.append((start, end, capacity))
        cuts = []
        for start, end, capacity in sorted(candidates, key=lambda item: (item[0] != 0 and item[1] < len(samples) - 240, -item[2])):
            amount = min(delta, capacity)
            if amount <= 0:
                continue
            middle = (start + end) // 2
            first = middle - amount // 2
            last = first + amount
            cuts.append((first, last))
            edits.append({'type': 'quiet-gap-center-removed', 'sourceStart': first / RATE,
                          'sourceEnd': last / RATE,
                          'removedRms': float(np.sqrt(np.mean(samples[first:last] ** 2)))})
            delta -= amount
        for first, last in sorted(cuts, reverse=True):
            samples = np.concatenate((samples[:first], samples[last:]))
        if len(samples) > target_frames:
            speed = len(samples) / target_frames
            if speed > 1.10:
                needed = int(np.ceil(len(samples) / 1.10)) + 1
                if needed <= target_frames + extra_existing_frames:
                    target_frames = needed
                    speed = len(samples) / target_frames
                else:
                    raise RuntimeError('Pronunciation exceeds 1.10 tempo ceiling: ' + str(round(speed, 5)))
            import librosa
            spectrum = librosa.stft(samples, n_fft=1024, hop_length=256)
            stretched = librosa.phase_vocoder(spectrum, rate=speed, hop_length=256)
            samples = librosa.istft(stretched, hop_length=256, length=target_frames).astype(np.float32)
    if len(samples) < target_frames:
        amount = target_frames - len(samples)
        internal = [(start, end) for start, end in quiet_gaps(samples)
                    if start > 0 and end < len(samples) - 240 and end - start >= round(.12 * RATE)]
        if internal and not tail_padding:
            positions = [(start + end) // 2 for start, end in sorted(internal, key=lambda item: item[1] - item[0], reverse=True)[:3]]
            chunks = [amount // len(positions)] * len(positions)
            chunks[0] += amount - sum(chunks)
            for position, chunk in sorted(zip(positions, chunks), reverse=True):
                samples = np.concatenate((samples[:position], np.zeros(chunk, np.float32), samples[position:]))
                edits.append({'type': 'punctuation-pause-extended', 'sourcePosition': position / RATE, 'seconds': chunk / RATE})
        else:
            samples = np.pad(samples, (0, amount))
            edits.append({'type': 'trailing-silence-added', 'seconds': amount / RATE})
    assert len(samples) == target_frames
    return samples, {'pitchPreservingTempo': speed, 'edits': edits,
                     'usedExtraExistingCueSeconds': max(0, target_frames - original_target) / RATE}


def record_for(cue, path, spoken, details):
    metadata = {'id': cue['id'], 'voice': 'stella', 'text': cue['text'], 'spokenText': spoken,
                'file': str(path.relative_to(ROOT)).replace('\\', '/'), 'preprocessed48k': True,
                'seconds': len(pcm(path)) / RATE, 'sampleRate': RATE, 'channels': 1,
                'sampleWidth': 2, 'sha256': sha(path), 'pronunciation': details}
    path.with_suffix('.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return metadata


def snapshot():
    BACKUP.mkdir(exist_ok=True)
    manifest_path = BACKUP / 'audio-manifest.json'
    if manifest_path.exists():
        return load(manifest_path)
    manifest = load(ROOT / 'audio-manifest.json')
    for name in ('audio-manifest.json', 'transcript.json', 'SCRIPT.md'):
        shutil.copy2(ROOT / name, BACKUP / name)
    for film in manifest['films']:
        for suffix in ('-captions.json', '-speech-events.json', '.srt'):
            shutil.copy2(AUDIO / (film['id'] + suffix), BACKUP / (film['id'] + suffix))
        for kind in ('voice', 'music', 'master'):
            shutil.copy2(AUDIO / (kind + '-' + film['id'] + '.wav'), BACKUP / (kind + '-' + film['id'] + '.wav'))
        for cue in film['cues']:
            shutil.copy2(AUDIO / (cue['id'] + '.wav'), BACKUP / (cue['id'] + '.wav'))
    evidence = {'sceneSha256': sha(ROOT / 'data/scenes.json'), 'captions': {
        str(path.relative_to(AUDIO)).replace('\\', '/'): sha(path)
        for film in manifest['films'] for path in (AUDIO / (film['id'] + '-captions.json'), AUDIO / (film['id'] + '.srt'))}}
    (BACKUP / 'locked-picture-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    return manifest


def main():
    BRAND.mkdir(exist_ok=True)
    old = snapshot()
    cues = {cue['id']: cue for film in old['films'] for cue in film['cues']}
    normal = read_audio(CLOUD / 'BRAND-spaced-stella.wav')
    save(BRAND / 'brand-spaced-normalized48k.wav', normal)
    # Both cuts lie inside measured silence: the initial 0.00–0.20 s gap and
    # final 2.07–2.23 s gap. Retain 80 ms before and after spoken phonemes.
    first, last = round(.12 * RATE), len(normal) - round(.08 * RATE)
    standard = normal[first:last].copy()
    standard *= .70 / float(np.abs(standard).max())
    save(BRAND / 'brand-pronunciation.wav', standard)
    references = {'brand': {'file': 'assets/audio/brand/brand-pronunciation.wav',
                            'spoken': 'H T T P Local Server', 'seconds': len(standard) / RATE,
                            'normalizedSource': 'assets/audio/brand/brand-spaced-normalized48k.wav',
                            'cropStart': first / RATE, 'cropEnd': last / RATE}}
    for key, source, spoken in [('zip', 'ZIP-term', 'zip'), ('wasm', 'WEB-ASSEMBLY-term', 'Web Assembly')]:
        sample = read_audio(CLOUD / (source + '-stella.wav'))
        sample *= .70 / float(np.abs(sample).max())
        target = BRAND / (key + '-pronunciation.wav')
        save(target, sample)
        references[key] = {'file': str(target.relative_to(ROOT)).replace('\\', '/'),
                           'spoken': spoken, 'seconds': len(sample) / RATE,
                           'source': 'assets/audio/cloud/' + source + '-stella.wav'}
    audition = np.concatenate((pcm(BRAND / 'brand-pronunciation.wav'), np.zeros(round(.35 * RATE),np.float32),
                               pcm(BRAND / 'zip-pronunciation.wav'), np.zeros(round(.35 * RATE),np.float32),
                               pcm(BRAND / 'wasm-pronunciation.wav')))
    save(BRAND / 'pronunciation-audition.wav', audition)
    records = []
    changes = []
    for cue_id, start_seconds, end_seconds in [('S02', 0, 2.23), ('S44', 0, 1.96), ('O08', 3.50, None), ('T06', 0, None), ('V07', 0, None)]:
        cue = cues[cue_id]
        original = pcm(BACKUP / (cue_id + '.wav'))
        first = round(start_seconds * RATE)
        last = round(end_seconds * RATE) if end_seconds is not None else len(original)
        replacement, details = fit(standard.copy(), last - first, tail_padding=True)
        result = original.copy()
        result[first:last] = replacement
        fade = min(384, len(replacement) // 4)
        envelope = (.5 - .5 * np.cos(np.linspace(0, np.pi, fade))).astype(np.float32)
        result[first:first + fade] = original[first:first + fade] * (1 - envelope) + replacement[:fade] * envelope
        result[last - fade:last] = replacement[-fade:] * (1 - envelope) + original[last - fade:last] * envelope
        path = BRAND / (cue_id + '-pronunciation-fixed.wav')
        save(path, result)
        assert np.array_equal(np.round(pcm(path)[:first] * 32768), np.round(original[:first] * 32768))
        assert np.array_equal(np.round(pcm(path)[last:] * 32768), np.round(original[last:] * 32768))
        jumps = [abs(float(result[index] - result[index - 1])) for index in (first, last) if 0 < index < len(result)]
        assert max(jumps, default=0) < .01
        details.update(method='reused standard brand in original sample window; outside samples unchanged',
                       originalClip='assets/audio/before-pronunciation/' + cue_id + '.wav',
                       replacedStart=first / RATE, replacedEnd=last / RATE,
                       normalizedTerm=references['brand']['file'], crossfadeSeconds=fade / RATE,
                       maximumJoinJump=max(jumps, default=0), outsideWindowPcmUnchanged=True)
        record = record_for(cue, path, cue['text'].replace('HttpLocalServer', 'H T T P Local Server'), details)
        records.append(record); changes.append({'id': cue_id, **details})
    for source_json in sorted(CLOUD.glob('corrected-*-stella.json')):
        source = load(source_json)
        cue_id = source['id']
        cue = cues[cue_id]
        original = pcm(BACKUP / (cue_id + '.wav'))
        sample = read_audio(ROOT / source['file'])
        native_seconds = len(sample) / RATE
        remaining = max(0, round((cue['duration'] - cue['voiceOffset'] - .3501) * RATE) - len(original))
        try:
            result, details = fit(sample, len(original), extra_existing_frames=remaining)
        except RuntimeError as error:
            raise RuntimeError(cue_id + ': ' + str(error)) from None
        result *= .70 / float(np.abs(result).max())
        # Smooth only 5 ms at the file ends; these lie in retained quiet pads.
        fade = 240
        result[:fade] *= np.linspace(0, 1, fade)
        result[-fade:] *= np.linspace(1, 0, fade)
        path = BRAND / (cue_id + '-pronunciation-fixed.wav')
        save(path, result)
        details.update(method='only affected cue resynthesized with explicit English spelling; fitted to original sample count',
                       source=source['file'], sourceSpokenText=source['spokenText'],
                       nativeSeconds=native_seconds, finalSeconds=len(result) / RATE,
                       originalSeconds=len(original) / RATE)
        records.append(record_for(cue, path, source['spokenText'], details))
        changes.append({'id': cue_id, **details})
    expected = {'S02','S44','O02','O08','T06','V07','S16','S22','S24','S30','S32','S34','S36','S37','S39','S40','T02','V02'}
    assert {record['id'] for record in records} == expected
    overrides = {'version': 1, 'records': records}
    (CLOUD / 'pronunciation-overrides.json').write_text(json.dumps(overrides, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    standards = {'HttpLocalServer':'H T T P Local Server','ZIP':'zip /zɪp/','Wasm':'Web Assembly',
                 'QuickJS':'Quick J S','WAMR':'W A M R','MCP':'M C P','PCDN':'P C D N',
                 'HTTP':'H T T P','HTTPS':'H T T P S','IP':'I P','CLI':'C L I','PDF':'P D F',
                 'Word':'Word','Server':'Server','Markdown':'Markdown','AI':'A I'}
    audit = []
    for term, spoken in standards.items():
        hits = [cue['id'] for cue in cues.values() if re.search(r'(?<![A-Za-z])'+re.escape(term)+r'(?![A-Za-z])', cue['text'])]
        audit.append({'display':term,'spokenStandard':spoken,'cues':hits,
                      'changedCues':[cue_id for cue_id in hits if cue_id in expected],
                      'verification':'Explicit request spelling; independent-English ASR is supporting evidence only. Chinese whole-sentence English-model recognition is invalid and not used.'})
    report = {'version':1,'voice':'stella','captionAndSceneTimingLocked':True,
              'oldMasterBackup':'assets/audio/before-pronunciation/',
              'references':references,'changes':changes,'standards':audit,
              'maximumPitchPreservingTempo':max(change['pitchPreservingTempo'] for change in changes),
              'durationExtensionRequired':False,'truncatedPhonemes':0,
              'verificationLimit':'No human listening or phonetic certification is claimed. tiny.en supports independent English clips only; its mixed Chinese whole-sentence hallucinations are excluded.',
              'discardedWasmPronunciation':'waz-um references were produced before the user selected Web Assembly and are not used in delivery.'}
    (BRAND / 'pronunciation-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'patchedCues':len(records),'maximumTempo':report['maximumPitchPreservingTempo'],
                      'references':references,'durationExtensionRequired':False},ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
