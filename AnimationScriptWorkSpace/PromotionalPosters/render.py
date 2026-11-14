"""Export nine 2160px square posters and the complete 3×3 set."""
from pathlib import Path
import hashlib,json
from PIL import Image
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parent
OUTPUT=ROOT/'images';OUTPUT.mkdir(exist_ok=True)
CHROME='C:/Program Files/Google/Chrome/Application/chrome.exe'
records=[];errors=[]
with sync_playwright() as p:
    browser=p.chromium.launch(executable_path=CHROME,headless=True)
    page=browser.new_page(viewport={'width':3240,'height':3240},device_scale_factor=2)
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.goto((ROOT/'posters.html').as_uri(),wait_until='load')
    page.evaluate('document.fonts.ready')
    page.evaluate('Promise.all(Array.from(document.images).map(function(i){return i.decode()}))')
    page.evaluate('window.fitPosterArt()')
    for poster in page.locator('.poster').all():
        slug=poster.get_attribute('data-file')
        issues=poster.evaluate('''p=>{const outer=p.getBoundingClientRect(),bad=[];for(const e of p.querySelectorAll('h1,.sub,header,footer,.closing-name,.source-line,.platforms')){const r=e.getBoundingClientRect();if(r.left<outer.left+30||r.right>outer.right-30||r.top<outer.top+30||r.bottom>outer.bottom-30||e.scrollWidth>e.clientWidth+2)bad.push({class:e.className,text:e.innerText,rect:[r.left,r.top,r.right,r.bottom]});}const art=p.querySelector('.art');if(art){const a=art.getBoundingClientRect(),f=p.querySelector('footer').getBoundingClientRect();if(a.bottom>f.top+1)bad.push({class:'art',reason:'footer overlap'});for(const e of art.querySelector('.art-inner').children){if(e.classList.contains('orbit'))continue;const r=e.getBoundingClientRect();if(r.bottom>a.bottom+1)bad.push({class:e.className,reason:'art clipped at bottom'});}}return bad}''')
        assert not issues,(slug,issues)
        file=OUTPUT/(slug+'.png');poster.screenshot(path=str(file),animations='disabled')
        im=Image.open(file).convert('RGB');assert im.size==(2160,2160)
        im.save(OUTPUT/(slug+'.jpg'),quality=96,subsampling=0,optimize=True)
        records.append({'name':slug,'file':file.relative_to(ROOT).as_posix(),'width':2160,'height':2160,'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'textBoundsPassed':True})
        print('Rendered '+slug,flush=True)
    browser.close()
assert len(records)==9 and not errors
collage=Image.new('RGB',(6480,6480))
for i,record in enumerate(records):
    im=Image.open(ROOT/record['file']).convert('RGB');collage.paste(im,(i%3*2160,i//3*2160))
collage.save(ROOT/'HttpLocalServer-nine-grid.png',optimize=True)
collage.save(ROOT/'HttpLocalServer-nine-grid.jpg',quality=94,subsampling=0,optimize=True)
collage.resize((1620,1620),Image.Resampling.LANCZOS).save(ROOT/'preview.jpg',quality=94,subsampling=0)
(ROOT/'verification.json').write_text(json.dumps({'passed':True,'individualImages':9,'individualPixels':[2160,2160],'gridPixels':[6480,6480],'errors':errors,'posters':records},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('PASS: nine posters + full-resolution grid + preview.',flush=True)
