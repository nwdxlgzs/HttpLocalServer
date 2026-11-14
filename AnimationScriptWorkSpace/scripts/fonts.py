"""Create local-use WOFF2 subsets from the installed Windows fonts.
These files are production dependencies, not redistributable application assets.
"""
from pathlib import Path
from fontTools import subset
from fontTools.ttLib import TTFont
ROOT=Path(__file__).resolve().parents[1]
text=''.join(p.read_text(encoding='utf-8') for pattern in ['data/*.json','js/*.js','styles/*.css','scripts/build.mjs'] for p in ROOT.glob(pattern))
text+=''.join(chr(i) for i in range(32,1024))+'✓→×·…—'
out=ROOT/'assets/fonts';out.mkdir(parents=True,exist_ok=True)
fonts=[('msyh.ttc','yahei-regular',0),('msyhbd.ttc','yahei-bold',0),('segoeui.ttf','segoe-regular',None),('segoeuib.ttf','segoe-bold',None),('consola.ttf','consolas',None)]
for source,name,index in fonts:
    path=Path('C:/Windows/Fonts')/source
    font=TTFont(path,**({'fontNumber':index} if index is not None else {}))
    options=subset.Options(); options.flavor='woff2';options.recalc_timestamp=False
    sub=subset.Subsetter(options=options);sub.populate(text=text);sub.subset(font)
    font.flavor='woff2';font.save(out/(name+'.woff2'))
    print(name,flush=True)
