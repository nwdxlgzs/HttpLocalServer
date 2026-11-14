"""Tight continuous cuts from measured speech, without regenerating paid lines."""
from __future__ import annotations
import copy
import json
import math
import pathlib
import shutil

from audio_cloud_mix import read_audio

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEAD = 7 / 30
TAIL = .50
TRANSITION = .35


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    history = ROOT / 'data/scenes-long-form.json'
    if not history.exists():
        shutil.copy2(ROOT / 'data/scenes.json', history)
    scenes = read_json(history)
    records = read_json(ROOT / 'assets/audio/cloud/all-stella-manifest.json')['cues']
    records += read_json(ROOT / 'assets/audio/cloud/qr-stella-manifest.json')['cues']
    audio = {record['id']: record for record in records}
    lengths = {cue_id: len(read_audio(ROOT / record['file'])) / 48000 for cue_id, record in audio.items()}
    address = next(scene for scene in scenes['variants']['main']['scenes'] if scene['id'] == 'S04')
    for film_id, prefix in [('overview', 'O'), ('teaser', 'T'), ('vertical', 'V')]:
        variant = scenes['variants'][film_id]
        qr = copy.deepcopy(address)
        qr.update(id=prefix + 'QR', sourceId='S04', audioSourceId='QR01',
                  planTitle='扫码，直接进入服务', title='扫一扫，直接打开',
                  subtitle='网页或命令行显示二维码，手机快速访问。',
                  narration='扫一扫，直接打开。', qrDemonstration=True)
        after = next(index for index, scene in enumerate(variant['scenes']) if scene.get('sourceId') == 'S03')
        variant['scenes'].insert(after + 1, qr)
    scenes.update(timingProfile='continuous-overlap', transitionSeconds=TRANSITION,
                  historicalPlan='data/scenes-long-form.json',
                  timingSource='Measured cached DeepSeek stella PCM; existing speech is reused without new requests.')
    report = {'timingProfile': 'continuous-overlap', 'transitionSeconds': TRANSITION, 'films': []}
    vertical_holds = {'V01': .4, 'V02': .25, 'V03': .65, 'V04': .5, 'V05': .6, 'V06': .35, 'V07': .5}
    for film_id, variant in scenes['variants'].items():
        cursor = 0.0
        for index, scene in enumerate(variant['scenes']):
            if scene['kind'] == 'address':
                scene.update(title='扫一扫，直接打开',
                             subtitle='网页与 CLI 都能扫码，手机无需手动输入地址。')
            source = scene.get('audioSourceId', scene['id'])
            seconds = lengths[source]
            hold = 0.0
            minimum = 0.0
            if film_id == 'main':
                minimum = 4.2
                if scene['kind'] == 'technical':
                    hold = .6
                elif scene['kind'] in ('download', 'tasks', 'folders', 'identify'):
                    hold = .2
                if scene['id'] == 'S04':
                    minimum = 8.0
                    scene['qrDemonstration'] = True
            elif film_id == 'overview':
                hold = .75
            elif film_id == 'teaser':
                minimum = 3.0
                hold = .3 if scene['id'] == 'T04' else .4 if scene['id'] == 'T06' else 0
            elif film_id == 'vertical':
                hold = vertical_holds.get(scene['id'], 0)
            if scene.get('qrDemonstration') and film_id != 'main':
                minimum = 5.8
                hold = 0
            duration = math.ceil(max(minimum, seconds + LEAD + TAIL + hold) * 30 - 1e-7) / 30
            scene.update(start=round(cursor, 6), duration=round(duration, 6), voiceLead=round(LEAD, 6),
                         minimumVoiceTail=.35, measuredNativeVoice=round(seconds, 5),
                         visualHold=hold, transitionSeconds=TRANSITION)
            cursor += duration
            if index + 1 < len(variant['scenes']):
                cursor -= TRANSITION
        # A frame-aligned film ending can add at most 1/60 s to the final shot.
        film_duration = math.ceil(cursor * 30 - 1e-7) / 30
        variant['scenes'][-1]['duration'] = round(variant['scenes'][-1]['duration'] + film_duration - cursor, 6)
        variant.update(duration=round(film_duration, 6), transitionSeconds=TRANSITION)
        report['films'].append({'id': film_id, 'shots': len(variant['scenes']),
                                'duration': variant['duration'],
                                'nativeSpeechSeconds': round(sum(lengths[s.get('audioSourceId', s['id'])] for s in variant['scenes']), 5)})
    (ROOT / 'data/scenes.json').write_text(json.dumps(scenes, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (ROOT / 'assets/audio/retiming.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
