"""Check deterministic GSAP seeking for each isolated technical scene."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import json
import sys

ROOT = Path(__file__).resolve().parent
GSAP = sys.argv[1] if len(sys.argv)>1 else str(ROOT.parent / 'vendor' / 'gsap.min.js')
results=[]
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,executable_path=r'C:\Program Files\Google\Chrome\Application\chrome.exe')
    for name,viewport in [('landscape',{'width':1920,'height':1080}),('portrait',{'width':1080,'height':1920})]:
        page=browser.new_page(viewport=viewport)
        page.goto((ROOT/'review.html').as_uri())
        if GSAP.startswith('http'):
            page.add_script_tag(url=GSAP)
        else:
            page.add_script_tag(path=GSAP)
        values=page.evaluate('''() => {
          const results=[];
          function state() {
            return Array.from(document.querySelectorAll('#stage *')).map(el=>{
              const r=el.getBoundingClientRect(),s=getComputedStyle(el);
              return [el.className.baseVal||el.className, [r.x,r.y,r.width,r.height].map(n=>Math.round(n*1000)/1000),s.opacity,s.transform,s.backgroundColor,s.color,el.getAttribute('cx'),el.getAttribute('cy'),s.strokeDashoffset];
            });
          }
          for(const id of HLSTechnical.supported){
            review(id);
            const tl=gsap.timeline({paused:true});
            HLSTechnical.animate(tl,document.querySelector('#stage'),0,10,{id});
            tl.seek(9,false);const first=JSON.stringify(state());
            tl.seek(0,false);tl.seek(6,false);tl.seek(1,false);tl.seek(9,false);
            const replay=JSON.stringify(state());
            results.push({scene:id,deterministic:first===replay,timelineDuration:tl.duration(),withinBudget:tl.duration()<=10});
            tl.kill();
          }
          return results;
        }''')
        results.extend({'variant':name,**x} for x in values)
        page.close()
    browser.close()
(ROOT/'animation-report.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(results,ensure_ascii=False))
sys.exit(0 if all(x['deterministic'] and x['withinBudget'] for x in results) else 1)
