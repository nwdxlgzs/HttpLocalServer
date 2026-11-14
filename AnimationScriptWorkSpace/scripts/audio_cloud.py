"""Optional paid TTS production, with credentials confined to request memory.

The credentials file is never printed, copied, hashed into cache metadata, or
placed on a command line. Error response bodies and request headers are never
logged. Files containing speech are the only downloaded service output saved.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import pathlib
import shutil
import sys
import urllib.error
import urllib.request
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLOUD = ROOT / 'assets/audio/cloud'
PRIVATE = ROOT.parent / '.PrivateSpace'
ENDPOINT = 'https://api.deepseek.com/v1/tools/tts'


def normalize_streaming_wave(path):
    """Replace live-stream RIFF length sentinels with actual finite lengths."""
    with wave.open(str(path), 'rb') as audio:
        rate, channels, width = audio.getframerate(), audio.getnchannels(), audio.getsampwidth()
        raw = audio.readframes(audio.getnframes())
    frames = len(raw) // (channels * width)
    if channels != 1 or width != 2 or rate not in (16000, 22050, 24000, 44100, 48000):
        raise RuntimeError('Unexpected service PCM format; audio rejected.')
    if frames <= 0 or frames / rate > 120:
        raise RuntimeError('Unexpected per-cue audio duration; audio rejected.')
    corrected = path.with_suffix('.normalized')
    with wave.open(str(corrected), 'wb') as output:
        output.setnchannels(channels)
        output.setsampwidth(width)
        output.setframerate(rate)
        output.writeframes(raw)
    corrected.replace(path)
    return {'seconds': frames / rate, 'sampleRate': rate, 'channels': channels,
            'sampleWidth': width, 'bytes': path.stat().st_size}


def synthesize(text, voice, target):
    """Return safe response metadata. Never expose response bodies on errors."""
    credential = (PRIVATE / 'DeepSeek-Key.txt').read_text(encoding='utf-8-sig').strip()
    if not credential:
        raise RuntimeError('Credential file is empty; no request was sent.')
    body = json.dumps({'text': text, 'voice_id': voice, 'format': 'wav',
                       'filter_markdown': True}, ensure_ascii=False).encode('utf-8')
    request = urllib.request.Request(ENDPOINT, data=body, method='POST', headers={
        'Authorization': 'Bearer ' + credential, 'Content-Type': 'application/json',
        'Accept': 'audio/wav', 'User-Agent': 'HttpLocalServer-Promo-Audio/1'})
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix('.partial')
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            # Capture only non-sensitive technical headers. Never dump headers.
            content_type = response.headers.get('Content-Type', '').split(';')[0]
            if content_type not in ('audio/wav', 'audio/x-wav', 'audio/wave', 'application/octet-stream'):
                raise RuntimeError('Service returned a non-audio content type; response body was discarded.')
            length = 0
            with temporary.open('wb') as output:
                while True:
                    block = response.read(65536)
                    if not block:
                        break
                    length += len(block)
                    if length > 30 * 1024 * 1024:
                        raise RuntimeError('Audio response exceeded the per-cue size limit.')
                    output.write(block)
        metadata = normalize_streaming_wave(temporary)
        temporary.replace(target)
        return metadata
    except urllib.error.HTTPError as error:
        raise RuntimeError('TTS request failed with HTTP ' + str(error.code) + '; body withheld.') from None
    except urllib.error.URLError:
        raise RuntimeError('TTS network request failed; exception details withheld.') from None
    finally:
        if temporary.exists():
            temporary.unlink()
        credential = None
        request = None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--audition', action='store_true')
    parser.add_argument('--qr', action='store_true', help='Generate only the newly approved short QR line.')
    parser.add_argument('--brand-style', choices=('spaced', 'punctuated'), help='Generate one standard English brand audition.')
    parser.add_argument('--term-zip', action='store_true', help='Generate the standard English word zip.')
    parser.add_argument('--term-list', action='store_true', help='Generate one standard technical pronunciation reference.')
    parser.add_argument('--term-web-assembly', action='store_true', help='Generate the approved Web Assembly wording.')
    parser.add_argument('--correct-cues', help='Comma-separated cue IDs; only these receive corrected pronunciation.')
    parser.add_argument('--voice', choices=('stella', 'tide'), default='stella')
    parser.add_argument('--film', choices=('main', 'overview', 'teaser', 'vertical', 'all'), default='main')
    parser.add_argument('--max-characters', type=int, default=200)
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'audio-manifest.json').read_text(encoding='utf-8'))
    if args.correct_cues:
        requested = args.correct_cues.split(',')
        known = {cue['id']: cue for film in manifest['films'] for cue in film['cues']}
        if any(cue_id not in known for cue_id in requested):
            raise RuntimeError('Unknown requested narration cue.')
        cues = [dict(known[cue_id]) for cue_id in requested]
        for cue in cues:
            cue['spokenText'] = cue['text'].replace('HttpLocalServer', ' H T T P Local Server ').replace('ZIP', ' zip ')
            for original, pronounced in [('QuickJS', 'Quick J S'), ('WAMR', 'W A M R'), ('Wasm', 'Web Assembly'),
                                         ('HTTPS', 'H T T P S'), ('HTTP', 'H T T P'), ('MCP', 'M C P'),
                                         ('PCDN', 'P C D N'), ('CLI', 'C L I'), ('PDF', 'P D F'),
                                         ('IP', 'I P'), ('AI', 'A I')]:
                cue['spokenText'] = cue['spokenText'].replace(original, ' ' + pronounced + ' ')
    elif args.term_web_assembly:
        cues = [{'id': 'WEB-ASSEMBLY-term', 'text': 'Web Assembly.'}]
    elif args.term_list:
        cues = [{'id': 'TERMS', 'text': 'waz-um. Quick J S. W A M R. M C P. P C D N. H T T P. H T T P S. I P. C L I. P D F. Word. Server. Markdown. A I.'}]
    elif args.term_zip:
        cues = [{'id': 'ZIP-term', 'text': 'zip.'}]
    elif args.brand_style:
        text = 'H T T P Local Server.' if args.brand_style == 'spaced' else 'H, T, T, P. Local Server.'
        cues = [{'id': 'BRAND-' + args.brand_style, 'text': text}]
    elif args.qr:
        cues = [{'id': 'QR01', 'text': '扫一扫，直接打开。'}]
    elif args.audition:
        cues = [manifest['films'][0]['cues'][0]]
    else:
        cues = [cue for film in manifest['films'] if args.film in ('all', film['id']) for cue in film['cues']]
    planned = sum(len(cue['text']) for cue in cues)
    if planned > args.max_characters:
        raise RuntimeError('Character ceiling exceeded: planned ' + str(planned) + ', allowed ' + str(args.max_characters))
    status = {'provider': 'DeepSeek TTS', 'voice': args.voice, 'plannedCharacters': planned,
              'pricingVisible': False, 'billingTotalVisible': False, 'cues': []}
    for cue in cues:
        text = cue.get('spokenText', cue['text'])
        fingerprint = hashlib.sha256((args.voice + '\n' + text).encode('utf-8')).hexdigest()
        prefix = 'corrected-' if args.correct_cues else 'audition-' if args.audition else ''
        target = CLOUD / (prefix + cue['id'] + '-' + args.voice + '.wav')
        sidecar = target.with_suffix('.json')
        # Reuse the approved audition and identical wording in other variants.
        # This cache key uses only public text and voice, never credentials.
        if not target.exists() and CLOUD.exists():
            for candidate in CLOUD.glob('*.json'):
                previous = json.loads(candidate.read_text(encoding='utf-8'))
                previous_audio = candidate.with_suffix('.wav')
                if previous.get('inputSha256') == fingerprint and previous_audio.exists():
                    shutil.copy2(previous_audio, target)
                    previous.update(id=cue['id'], file=str(target.relative_to(ROOT)).replace('\\', '/'))
                    sidecar.write_text(json.dumps(previous, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                    break
        if target.exists() and sidecar.exists():
            record = json.loads(sidecar.read_text(encoding='utf-8'))
            if record.get('inputSha256') == fingerprint:
                record.update(normalize_streaming_wave(target))
                sidecar.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                print(json.dumps({'cue': cue['id'], 'cached': True, 'seconds': record['seconds'],
                                  'file': str(target.relative_to(ROOT))}, ensure_ascii=False), flush=True)
                status['cues'].append(record)
                continue
        info = synthesize(text, args.voice, target)
        record = {'id': cue['id'], 'voice': args.voice, 'text': cue['text'], 'spokenText': text, 'characters': len(text),
                  'inputSha256': fingerprint, 'file': str(target.relative_to(ROOT)).replace('\\', '/'),
                  'generatedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(), **info}
        sidecar.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        status['cues'].append(record)
        print(json.dumps({'cue': cue['id'], 'cached': False, 'seconds': info['seconds'],
                          'characters': len(text), 'file': record['file']}, ensure_ascii=False), flush=True)
    summary = CLOUD / ('corrections-' + args.voice + '-manifest.json' if args.correct_cues else 'web-assembly-term-' + args.voice + '-manifest.json' if args.term_web_assembly else 'terms-' + args.voice + '-manifest.json' if args.term_list else 'zip-term-' + args.voice + '-manifest.json' if args.term_zip else 'brand-' + args.brand_style + '-' + args.voice + '-manifest.json' if args.brand_style else 'qr-' + args.voice + '-manifest.json' if args.qr else 'audition-' + args.voice + '-manifest.json' if args.audition else args.film + '-' + args.voice + '-manifest.json')
    summary.write_text(json.dumps(status, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        if isinstance(error, RuntimeError):
            print(str(error), file=sys.stderr)
        else:
            print('Audio generation failed; exception details withheld to protect private credentials.', file=sys.stderr)
        sys.exit(1)
