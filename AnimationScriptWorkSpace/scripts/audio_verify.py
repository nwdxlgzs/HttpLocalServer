"""Check generated voice, music, captions and approved shot timing."""
from __future__ import annotations
import hashlib
import json
import pathlib
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATE = 48000


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def inspect_wave(path, expected_frames=None):
    with wave.open(str(path), 'rb') as media:
        assert media.getnchannels() in (1,2) and (media.getsampwidth(), media.getframerate()) == (2, RATE), path
        count = media.getnframes()
        if expected_frames is not None:
            assert count == expected_frames, (path, count, expected_frames)
        samples = np.frombuffer(media.readframes(count), '<i2').astype(np.float32) / 32768
    assert np.isfinite(samples).all() and np.any(samples != 0), path
    peak = float(np.abs(samples).max())
    assert peak < .99, (path, peak)
    return samples, count / RATE, peak


def main():
    manifest = read_json(ROOT / 'audio-manifest.json')
    scene_data = read_json(ROOT / 'data/scenes.json')['variants']
    transcript = read_json(ROOT / 'transcript.json')
    report = {'passed': True, 'sampleRate': RATE, 'engine': manifest['engine'],
              'voice': manifest['selectedVoice'], 'films': []}
    all_cues = set()
    for film in manifest['films']:
        total_frames = round(film['duration'] * RATE)
        track_report = []
        for key in ('voiceFile', 'musicFile', 'mixFile'):
            samples, duration, peak = inspect_wave(ROOT / film[key], total_frames)
            track_report.append({'file': film[key], 'seconds': duration, 'peak': round(peak, 6),
                                 'rms': round(float(np.sqrt(np.mean(samples * samples))), 6)})
        shots = {s['id']: s for s in scene_data[film['id']]['scenes']}
        assert len(shots) == len(film['cues'])
        minimum_tail = 99
        maximum_tail = 0
        ordinary_tails = []
        qr_tails = {}
        minimum_gap = 99
        previous_voice_end = None
        native_events = 0
        for cue in film['cues']:
            assert cue['id'] not in all_cues
            all_cues.add(cue['id'])
            shot = shots[cue['id']]
            assert (cue['text'], cue['start'], cue['duration']) == (shot['narration'], shot['start'], shot['duration']), cue['id']
            samples, seconds, peak = inspect_wave(ROOT / cue['audio'])
            assert abs(seconds - cue['measuredVoiceSeconds']) < .0001, cue['id']
            assert hashlib.sha256((ROOT / cue['audio']).read_bytes()).hexdigest() == cue['sha256'], cue['id']
            if manifest.get('timingProfile') == 'continuous-overlap':
                lead = cue['voiceOffset']
                tail = .35
                assert lead >= .2 and abs(lead - shot['voiceLead']) < .0001, cue['id']
                if previous_voice_end is not None:
                    gap = cue['start'] + lead - previous_voice_end
                    assert gap >= 0, ('Overlapping speech', cue['id'], gap)
                    minimum_gap = min(minimum_gap, gap)
                previous_voice_end = cue['start'] + lead + seconds
            else:
                lead = .65 if cue['duration'] >= 8 else .35
                tail = .8 if cue['duration'] >= 8 else .25
                assert cue['voiceOffset'] == lead, cue['id']
            actual_tail = cue['duration'] - lead - seconds
            assert actual_tail + .0001 >= tail, (cue['id'], actual_tail, tail)
            minimum_tail = min(minimum_tail, actual_tail)
            maximum_tail = max(maximum_tail, actual_tail)
            if shot.get('qrDemonstration'):
                qr_tails[cue['id']] = round(actual_tail, 5)
            else:
                ordinary_tails.append(actual_tail)
            if manifest['engine'] == 'Windows System.Speech/SAPI':
                meta = read_json(ROOT / 'assets/audio/raw' / (cue['id'] + '.wav.json'))
                previous = -1
                for event in meta['events']:
                    assert 0 <= event['start'] <= seconds and event['start'] >= previous, cue['id']
                    previous = event['start']
                native_events += len(meta['events'])
            else:
                source_meta = read_json(ROOT / cue['sourceMetadata'])
                assert source_meta['text'] == cue['text'] and (ROOT / cue['sourceAudio']).exists(), cue['id']
        captions = read_json(ROOT / 'assets/audio' / (film['id'] + '-captions.json'))
        previous = 0
        fallback_count = 0
        for caption in captions:
            assert 0 <= caption['start'] < caption['end'] <= film['duration'], caption
            assert caption['start'] >= previous, caption
            assert caption['text'].strip(), caption
            previous = caption['end']
            fallback_count += 'estimate' in caption['timing']
        for cue in film['cues']:
            cue_captions = [caption for caption in captions if caption['id'] == cue['id']]
            assert ''.join(caption['text'] for caption in cue_captions) == cue['text'], cue['id']
            assert all(caption['start'] >= cue['start'] and caption['end'] <= cue['start'] + cue['duration']
                       for caption in cue_captions), cue['id']
        assert captions == next(f for f in transcript['films'] if f['id'] == film['id'])['captions']
        srt_path = ROOT / film['subtitles']
        assert srt_path.read_text(encoding='utf-8').count(' --> ') == len(captions)
        film_report = {'id': film['id'], 'seconds': film['duration'], 'shots': len(shots),
                       'captions': len(captions), 'fallbackEstimatedCaptions': fallback_count,
                       'nativeSpeechEvents': native_events, 'minimumTailSeconds': round(minimum_tail, 5),
                       'maximumTailSeconds': round(maximum_tail, 5),
                       'ordinaryTailMinSeconds': round(min(ordinary_tails), 5),
                       'ordinaryTailMaxSeconds': round(max(ordinary_tails), 5),
                       'qrDemonstrationTailsSeconds': qr_tails,
                       'minimumSpeechGapSeconds': round(minimum_gap, 5) if minimum_gap < 99 else None,
                       'truncatedCues': 0,
                       'tracks': track_report}
        report['films'].append(film_report)
        print(json.dumps(film_report, ensure_ascii=False))
    report['totalCues'] = len(all_cues)
    (ROOT / 'assets/audio/verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('PASS:', len(all_cues), 'narration cues; every WAV, caption and shot checked.')


if __name__ == '__main__':
    main()
