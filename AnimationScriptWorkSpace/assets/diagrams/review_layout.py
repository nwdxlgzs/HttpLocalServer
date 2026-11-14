"""Local visual QA for the original technical diagrams; no product code imports."""
from pathlib import Path
from PIL import Image, ImageDraw
from playwright.sync_api import sync_playwright
import json

ROOT = Path(__file__).resolve().parent
SCENES = ['S24','S25','S27','S28','S29','S30','S31','S34','S35','S36','S37','S38','S39','S40','S41','S42','S43']
RESULTS = []
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, executable_path=r'C:\Program Files\Google\Chrome\Application\chrome.exe')
    for variant, size in [('landscape', {'width':1920,'height':1080}), ('portrait', {'width':1080,'height':1920})]:
        page = browser.new_page(viewport=size, device_scale_factor=1)
        page.goto((ROOT / 'review.html').as_uri())
        for scene in SCENES:
            page.evaluate('(id)=>review(id)', scene)
            page.evaluate('()=>document.fonts.ready')
            errors = page.evaluate('''() => {
              const stage=document.querySelector('#stage').getBoundingClientRect(), out=[];
              for(const el of document.querySelectorAll('#stage *')) {
                if(el.closest('svg')||el.classList.contains('tech-stage'))continue;
                const style=getComputedStyle(el), r=el.getBoundingClientRect();
                if(!r.width||!r.height||style.display==='none')continue;
                if(r.left<stage.left-2||r.top<stage.top-2||r.right>stage.right+2||r.bottom>stage.bottom+2)
                  out.push({type:'stage-overflow',selector:el.className,text:el.textContent.trim().slice(0,75),rect:[r.x,r.y,r.width,r.height]});
                if(el.scrollWidth>el.clientWidth+3&&style.overflowX==='hidden')
                  out.push({type:'clip',selector:el.className,text:el.textContent.trim().slice(0,75),width:[el.scrollWidth,el.clientWidth]});
              }
              return out;
            }''')
            RESULTS.append({'variant':variant,'scene':scene,'errors':errors})
            page.screenshot(path=str(ROOT / f'{variant}-{scene}.png'))
        page.close()
    browser.close()
(ROOT / 'layout-report.json').write_text(json.dumps(RESULTS,ensure_ascii=False,indent=2), encoding='utf-8')
for variant, thumb in [('landscape',(576,324)),('portrait',(324,576))]:
    cols=4 if variant=='landscape' else 6
    rows=(len(SCENES)+cols-1)//cols
    sheet=Image.new('RGB',(cols*thumb[0],rows*(thumb[1]+30)), '#E4EDFA')
    draw=ImageDraw.Draw(sheet)
    for i,scene in enumerate(SCENES):
        im=Image.open(ROOT/f'{variant}-{scene}.png').convert('RGB')
        im.thumbnail(thumb)
        x=(i%cols)*thumb[0]; y=(i//cols)*(thumb[1]+30)
        sheet.paste(im,(x,y+30))
        draw.text((x+12,y+7),scene,fill='#18324D')
    sheet.save(ROOT/f'contact-{variant}.jpg',quality=92)
print(json.dumps([{**x,'errors':len(x['errors'])} for x in RESULTS],ensure_ascii=False))
