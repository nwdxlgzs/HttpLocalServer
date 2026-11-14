"""Real browser layout/interaction regressions for document and preview tabs."""
import pathlib
import subprocess
import sys
import tempfile
from integration import Server
from features_v330_browser import fixtures


def create(root):
    from pptx import Presentation
    from pptx.util import Inches, Pt
    from pptx.dml.color import RGBColor
    from openpyxl import Workbook
    from openpyxl.styles import Alignment, PatternFill, Font, Border, Side
    from docx import Document
    from docx.enum.section import WD_SECTION_START, WD_ORIENT
    fixtures(root)
    p = Presentation()
    p.slide_width, p.slide_height = Inches(13.333), Inches(7.5)
    for i in range(36):
        slide = p.slides.add_slide(p.slide_layouts[6])
        slide.background.fill.solid()
        slide.background.fill.fore_color.rgb = RGBColor(245, 248, 252)
        title = slide.shapes.add_textbox(Inches(.7), Inches(.6), Inches(12), Inches(1.2))
        title.text = 'Project overview' if i == 0 else 'Slide %02d' % (i+1)
        title.text_frame.paragraphs[0].font.size = Pt(36)
        for k in range(3):
            shape = slide.shapes.add_shape(1, Inches(.7+k*4.1), Inches(2.5), Inches(3.8), Inches(2.5))
            shape.fill.solid()
            shape.fill.fore_color.rgb = RGBColor(55+k*35, 100+k*20, 170+k*10)
            shape.text = ['Design', 'Build', 'Deliver'][k]+'\n'+str(i+1)
            shape.text_frame.paragraphs[0].font.size = Pt(28)
    p.save(root/'deck.pptx')
    w = Workbook(); s = w.active; s.title = 'Merged cells'
    for c in 'ABCDEFGH': s.column_dimensions[c].width = 16
    for row in range(1, 110):
        s.row_dimensions[row].height = 27
        s.cell(row, 1, 'Row '+str(row))
    for ref, value in [('B2:D3','Merged title'),('F2:G5','Vertical merge'),('B7:D7','Summary'),
                       ('AE10:AH12','Across column groups'),('C99:E102','Across row pages')]:
        s.merge_cells(ref); cell=s[ref.split(':')[0]];cell.value=value
        cell.alignment=Alignment(horizontal='center',vertical='center',wrap_text=True)
        cell.fill=PatternFill('solid',fgColor='DDEBFA');cell.font=Font(size=14,bold=True,color='234F7C')
    s['E2']='Neighbour';s['E3']='Below neighbour';s['B9']='Copy me';s['C9']='Other'
    s['A1']='Select any part of a merged cell'
    w.save(root/'merged.xlsx')
    d=Document();d.add_heading('Portrait page',0);d.add_paragraph('A readable document that follows the available width.')
    section=d.add_section(WD_SECTION_START.NEW_PAGE);section.orientation=WD_ORIENT.LANDSCAPE
    section.page_width,section.page_height=section.page_height,section.page_width
    d.add_heading('Landscape page',0);d.add_paragraph('Landscape section should remain fully inside the viewport.')
    d.save(root/'mixed.docx')


def run(executable, https=False):
    with tempfile.TemporaryDirectory(prefix='hls-viewer-layout-') as directory:
        root=pathlib.Path(directory);create(root)
        server=Server(executable,root,extra=('--no-magika','--write-mode','shared','--zip-preview','host',*(['--https','--tls-self-signed'] if https else [])))
        try:
            subprocess.run(['node',str(pathlib.Path(__file__).with_suffix('.cjs')),('https://' if https else 'http://')+'127.0.0.1:'+str(server.port)],check=True,timeout=480)
        finally:
            server.close()


if __name__ == '__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
