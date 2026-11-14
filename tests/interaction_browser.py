"""Real-pointer regressions for moving files and scaled document selection."""
import pathlib, subprocess, sys, tempfile
from integration import Server
from viewer_layout_browser import create as base_fixtures

def fixtures(root):
    from pptx import Presentation
    from pptx.util import Inches, Pt
    from PIL import Image, ImageDraw
    base_fixtures(root)
    for name in ['DragSource/TabFolder','Target','Outer/Inner/CutChild','Outer/MoveChild','Multi','MultiDrag']:
        p=root/name;p.mkdir(parents=True,exist_ok=True)
        (p/'note.txt').write_text(name,encoding='utf-8')
    for name in ['one.txt','two.txt']:(root/'Multi'/name).write_text(name,encoding='utf-8')
    for name in ['drag1.txt','drag2.txt']:(root/'MultiDrag'/name).write_text(name,encoding='utf-8')
    im=Image.new('RGB',(180,90),'#3680bd');ImageDraw.Draw(im).rectangle((20,20,160,70),fill='#efd65a');im.save(root/'select-image.png')
    p=Presentation();p.slide_width=Inches(10);p.slide_height=Inches(6);s=p.slides.add_slide(p.slide_layouts[6])
    # Deliberately create lower text before the upper row and place a resource between rows.
    for label,x,y in [('Bottom excluded',.7,4.7),('Upper left',.7,.7),('Upper right',5.5,.7),('Second line left',.7,2.6),('Second line right',5.5,2.6)]:
        t=s.shapes.add_textbox(Inches(x),Inches(y),Inches(4),Inches(.6));t.text=label;t.text_frame.paragraphs[0].font.size=Pt(22)
    s.shapes.add_picture(str(root/'select-image.png'),Inches(4.1),Inches(1.65),width=Inches(1.8));p.save(root/'selection.pptx')

def run(executable,https=False):
    with tempfile.TemporaryDirectory(prefix='hls-interaction-') as directory:
        root=pathlib.Path(directory);fixtures(root)
        server=Server(executable,root,extra=('--no-magika','--write-mode','shared','--zip-preview','host',*(['--https','--tls-self-signed'] if https else [])))
        try:subprocess.run(['node',str(pathlib.Path(__file__).with_suffix('.cjs')),('https://' if https else 'http://')+'127.0.0.1:'+str(server.port)],check=True,timeout=480)
        finally:server.close()

if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
