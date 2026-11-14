"""Small valid documents for shipped preview engines (development only)."""
from pathlib import Path
import io
import zipfile


def create(root):
    from docx import Document
    from openpyxl import Workbook
    from pptx import Presentation
    from pptx.util import Inches
    from PIL import Image,features
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    doc=Document();doc.add_heading('HttpLocalServer document',0);doc.add_paragraph('Preview text can be selected and copied. 中文测试。')
    table=doc.add_table(rows=2,cols=2);table.cell(0,0).text='Name';table.cell(0,1).text='Value';table.cell(1,0).text='example';table.cell(1,1).text='42';doc.save(root/'example.docx')
    workbook=Workbook();sheet=workbook.active;sheet.title='Data';sheet.append(['Name','Value']);sheet.append(['example',42]);sheet.append(['中文',123]);workbook.create_sheet('Second')['A1']='Second sheet';workbook.save(root/'example.xlsx')
    presentation=Presentation();slide=presentation.slides.add_slide(presentation.slide_layouts[1]);slide.shapes.title.text='HttpLocalServer slides';slide.placeholders[1].text='Slide text can be selected and copied.'
    slide=presentation.slides.add_slide(presentation.slide_layouts[1]);slide.shapes.title.text='Second slide';slide.placeholders[1].text='Second page content';presentation.save(root/'example.pptx')
    # Ordinary PDF using a built-in Type1 font; deterministic xref positions.
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> /XObject << /Im0 6 0 R >> >> /Contents 5 0 R >>',b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    check_image=features.check('jpg_2000')
    if not check_image:raise RuntimeError('Development PDF fixture requires Pillow JPEG2000 support')
    image=Image.new('RGB',(32,32),(40,130,210));image_bytes=io.BytesIO();image.save(image_bytes,format='JPEG2000');jpx=image_bytes.getvalue()
    text=b'q 80 0 0 80 72 500 cm /Im0 Do Q BT /F1 24 Tf 72 700 Td (HttpLocalServer PDF preview) Tj 0 -40 Td /F1 12 Tf (Selectable text and bounded rendering.) Tj ET'
    objects.append(b'<< /Length '+str(len(text)).encode()+b' >>\nstream\n'+text+b'\nendstream')
    objects.append(b'<< /Type /XObject /Subtype /Image /Width 32 /Height 32 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /JPXDecode /Length '+str(len(jpx)).encode()+b' >>\nstream\n'+jpx+b'\nendstream')
    data=bytearray(b'%PDF-1.4\n');offsets=[0]
    for i,obj in enumerate(objects,1):offsets.append(len(data));data.extend(str(i).encode()+b' 0 obj\n'+obj+b'\nendobj\n')
    start=len(data);data.extend(b'xref\n0 7\n0000000000 65535 f \n')
    for offset in offsets[1:]:data.extend(('%010d 00000 n \n'%offset).encode())
    data.extend(b'trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n'+str(start).encode()+b'\n%%EOF\n');(root/'example.pdf').write_bytes(data)
    with zipfile.ZipFile(root/'website.zip','w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('site/index.html','<!doctype html><html><head><link rel="stylesheet" href="/site/style.css"></head><body><h1 id="zip-heading">ZIP website works</h1><img id="pixel" src="pixel.svg"><script src="app.js"></script></body></html>')
        z.writestr('site/style.css','body{background:rgb(220,240,255)}h1{color:rgb(20,90,130)}')
        z.writestr('site/pixel.svg','<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"><rect width="10" height="10" fill="red"/></svg>')
        z.writestr('site/app.js',"fetch('data.json').then(function(r){return r.json();}).then(function(v){document.body.setAttribute('data-loaded',v.loaded);});")
        z.writestr('site/data.json','{"loaded":"yes"}')
        for i in range(230):z.writestr('many/item%03d.txt'%i,'item %d'%i)
        for name in ['example.docx','example.xlsx','example.pptx','example.pdf']:z.write(root/name,'documents/'+name)


if __name__=='__main__':
    import sys
    create(sys.argv[1])
