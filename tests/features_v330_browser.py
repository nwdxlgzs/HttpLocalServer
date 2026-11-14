"""Responsive viewers, clipboard, inferred actions and editing in real Chromium."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile
from integration import Server
from preview_fixtures import create


def fixtures(root):
    from docx import Document
    from PIL import Image
    create(root)
    doc=Document()
    for i in range(8):
        doc.add_heading('Continuous page '+str(i+1),0)
        doc.add_paragraph('Selectable continuous Word content '+str(i+1))
        if i<7:doc.add_page_break()
    doc.save(root/'continuous.docx')
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'',b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'];ids=[]
    for i in range(12):
        pageid=len(objects)+1;ids.append(pageid)
        objects.append(('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents '+str(pageid+1)+' 0 R >>').encode())
        text=('BT /F1 24 Tf 60 700 Td (Continuous PDF page '+str(i+1)+') Tj ET').encode()
        objects.append(b'<< /Length '+str(len(text)).encode()+b' >>\nstream\n'+text+b'\nendstream')
    objects[1]=('<< /Type /Pages /Kids ['+' '.join(str(i)+' 0 R' for i in ids)+'] /Count 12 >>').encode()
    data=bytearray(b'%PDF-1.4\n');offsets=[0]
    for i,obj in enumerate(objects,1):offsets.append(len(data));data.extend(str(i).encode()+b' 0 obj\n'+obj+b'\nendobj\n')
    start=len(data);data.extend(('xref\n0 '+str(len(offsets))+'\n0000000000 65535 f \n').encode())
    for off in offsets[1:]:data.extend(('%010d 00000 n \n'%off).encode())
    data.extend(('trailer\n<< /Size '+str(len(offsets))+' /Root 1 0 R >>\nstartxref\n'+str(start)+'\n%%EOF\n').encode());(root/'continuous.pdf').write_bytes(data)
    Image.new('RGB',(40,30),(64,128,192)).save(root/'photo.png');(root/'photo').write_bytes((root/'photo.png').read_bytes())
    (root/'code.js').write_text('const hello = "world";\n',encoding='utf-8')
    (root/'note.txt').write_text('Editable text. 中文',encoding='utf-8')
    with zipfile.ZipFile(root/'markdown.bbb','w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('readme.md','# Archive Markdown\n\n![sample](images/photo.png)\n\n[next](next.md)\n\n<script>evil()</script>')
        z.writestr('next.md','# Next page')
        z.write(root/'photo.png','images/photo.png')
        z.write(root/'photo.png','stripped')
        z.write(root/'example.pdf','document.bbb')


def run(exe,https=False):
    with tempfile.TemporaryDirectory(prefix='hls-v330-browser-') as directory:
        root=pathlib.Path(directory);fixtures(root)
        server=Server(exe,root,extra=('--no-magika','--write-mode','shared','--zip-preview','host',*(['--https','--tls-self-signed'] if https else [])))
        try:subprocess.run(['node',str(pathlib.Path(__file__).with_name('features_v330_browser.cjs')),('https://' if https else 'http://')+'127.0.0.1:'+str(server.port)],check=True,timeout=480)
        finally:server.close()


if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
