"""Validate real product captures and produce review contact sheets."""
from pathlib import Path
import json
import subprocess
from PIL import Image, ImageDraw, ImageFont

WORK = Path(__file__).resolve().parents[1]
OUT = WORK / 'capture'
FONT = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 19)


def wsl_path(path):
    path = str(path.resolve())
    return '/mnt/' + path[0].lower() + path[2:].replace('\\', '/')


def contact_sheet(items, target):
    width, height = 480, 310
    sheet = Image.new('RGB', (width * 3, height * ((len(items) + 2) // 3)), '#F4F7FC')
    draw = ImageDraw.Draw(sheet)
    for i, (label, path) in enumerate(items):
        image = Image.open(path).convert('RGB')
        image.thumbnail((460, 264))
        x, y = (i % 3) * width, (i // 3) * height
        sheet.paste(image, (x + (width - image.width) // 2, y + 8))
        draw.text((x + 10, y + 276), label, fill='#18324D', font=FONT)
    sheet.save(target)


manifest = json.loads((WORK / 'capture-manifest.json').read_text(encoding='utf-8'))
stills, frames, reports = [], [], []
for asset in manifest['assets']:
    path = WORK / asset['path']
    assert path.stat().st_size > 0, path
    if path.suffix == '.png':
        image = Image.open(path)
        assert image.size == (asset['width'], asset['height']), path
        image.verify()
        stills.append((asset['slug'], path))
    else:
        probe = subprocess.run(['wsl', '-u', 'root', '--', 'ffprobe', '-v', 'error', '-show_format', '-show_streams', '-of', 'json', wsl_path(path)], check=True, capture_output=True, text=True)
        info = json.loads(probe.stdout)
        duration = float(info['format']['duration'])
        asset['durationSeconds'] = duration
        stream = next(s for s in info['streams'] if s['codec_type'] == 'video')
        assert (stream['width'], stream['height']) == (1600, 900), path
        report = {'slug': asset['slug'], 'seconds': duration, 'codec': stream['codec_name'], 'width': stream['width'], 'height': stream['height'], 'frames': []}
        for i, fraction in enumerate([.18, .43, .68, .9]):
            timestamp = round(duration * fraction, 2)
            target = OUT / (asset['slug'] + f'-{i + 1}.jpg')
            subprocess.run(['wsl', '-u', 'root', '--', 'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-ss', str(timestamp), '-i', wsl_path(path), '-frames:v', '1', '-update', '1', wsl_path(target)], check=True)
            report['frames'].append({'second': timestamp, 'path': target.relative_to(WORK).as_posix()})
            frames.append((asset['slug'] + f' {timestamp:.2f}s', target))
        reports.append(report)
contact_sheet(stills, OUT / 'screenshots-contact-sheet.jpg')
contact_sheet(frames, OUT / 'recordings-contact-sheet.jpg')
(OUT / 'verification.json').write_text(json.dumps({'screenshots': len(stills), 'recordings': reports, 'fileChecksPassed': True}, ensure_ascii=False, indent=2), encoding='utf-8')
(WORK / 'capture-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(reports, indent=2))
