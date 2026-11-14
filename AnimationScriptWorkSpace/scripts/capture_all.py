"""Reproducible isolated 3.5.1 browser captures for the product film.

No routing, DOM replacement or product CSS overrides: all product images are
rendered by the distributed native executable in an isolated demo root.
"""
import hashlib
import atexit
import json
import math
import pathlib
import shutil
import socket
import subprocess
import sys
import time
import urllib.parse
from PIL import Image, ImageDraw, ImageFont
import numpy as np
from docx import Document
from docx.shared import Inches as DInches, Pt as DPt, RGBColor as DColor
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from playwright.sync_api import sync_playwright
import zipfile
import argparse

WORK = pathlib.Path(__file__).resolve().parents[1]
PROJECT = WORK.parent
ROOT = WORK/'demo'/'share'
OUT = WORK/'assets'/'capture'
RAW = WORK/'capture'/'raw'
EXE = PROJECT/'dist'/'HttpLocalServer-windows-x64.exe'
CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
FONT = 'C:/Windows/Fonts/msyh.ttc'


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));return s.getsockname()[1]


class Server:
    def __init__(self, mode='filetrans', root=ROOT, extra=()):
        pathlib.Path(root).mkdir(parents=True,exist_ok=True)
        self.port=port(); self.base='http://127.0.0.1:'+str(self.port)
        self.log=(WORK/'capture'/('server-'+str(self.port)+'.log')).open('w',encoding='utf-8')
        args=[str(EXE),'--no-config',mode,'--root',str(root),'--bind','127.0.0.1','--port',str(self.port),
              '--state-dir',str(WORK/'demo'/('state-'+str(self.port))), '--no-magika',
              '--write-mode','shared','--zip-preview','host',*extra]
        self.process=subprocess.Popen(args,stdout=self.log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        atexit.register(self.close)
        for _ in range(200):
            if self.process.poll() is not None: raise RuntimeError('Server failed '+str(args))
            try:
                with socket.create_connection(('127.0.0.1',self.port),.1):break
            except OSError:time.sleep(.05)
        else:raise RuntimeError('Server startup timeout')
        self.args=args
    def close(self):
        if self.process.poll() is None:
            self.process.terminate();self.process.wait(timeout=10)
        if not self.log.closed:self.log.close()


def poster():
    w,h=1600,1000
    y,x=np.mgrid[0:h,0:w];q=np.clip((x*.6+y*.6)/(w*.6+h*.6),0,1)[...,None]
    arr=np.uint8(np.array([18,51,86])*(1-q)+np.array([50,125,173])*q)
    image=Image.fromarray(arr,'RGB');draw=ImageDraw.Draw(image)
    draw.ellipse((1120,125,1350,355),fill='#92F0DC')
    draw.polygon([(0,780),(300,520),(600,690),(930,410),(1230,650),(1600,470),(1600,1000),(0,1000)],fill='#3C8CF5')
    draw.polygon([(0,850),(350,690),(760,800),(1100,570),(1600,830),(1600,1000),(0,1000)],fill='#D8EAF3')
    draw.polygon([(0,940),(480,780),(920,910),(1280,760),(1600,870),(1600,1000),(0,1000)],fill='#F4F7FC')
    draw.text((110,130),'LOCAL. CONNECTED.',font=ImageFont.truetype(FONT,72),fill='#F4F7FC')
    draw.text((115,240),'让内容，在同一网络相遇',font=ImageFont.truetype(FONT,34),fill='#B9D3E6')
    draw.text((115,900),'HttpLocalServer  /  Design sample',font=ImageFont.truetype(FONT,24),fill='#18324D')
    image.save(ROOT/'设计视觉.png');image.save(WORK/'demo'/'poster.png')


def fixtures():
    ROOT.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True);RAW.mkdir(parents=True,exist_ok=True)
    (WORK/'capture').mkdir(exist_ok=True)
    for d in ['01 项目资料','02 设计素材','03 代码与数据','04 团队提交','05 大目录']:(ROOT/d).mkdir(exist_ok=True)
    poster()
    note='# 让本机成为服务入口\n\n文件、网页与轻量应用，按你的规则开放。\n\n![设计视觉](设计视觉.png)\n\n## 这次交付\n\n- 资料在线预览\n- 大文件按块传输\n- 上传前确认同名冲突\n- 主机统一控制访问\n\n> 示例内容，非个人文件。\n'
    (ROOT/'项目说明.md').write_text(note,encoding='utf-8')
    (ROOT/'会议记录.txt').write_text('项目会议记录\n\n1. 本周完成资料归档。\n2. 演示网站由 Server 模式托管。\n3. 上传内容由主机按权限接收。\n\n下一步：验证资料完整性，发布团队公告。',encoding='utf-8')
    data={'project':'HttpLocalServer','version':'3.5.1','network':{'mode':'LAN','protocol':'HTTP','port':8000},'features':{'preview':True,'writeMode':'shared','zipPreview':'host','pcdn':False},'deliverables':[{'name':'培训资料','status':'ready'},{'name':'演示站点','status':'ready'},{'name':'提交目录','status':'open'}]}
    (ROOT/'配置示例.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'示例代码.js').write_text("// HttpLocalServer · trusted application\nimport { fs, crypto } from 'hls';\n\nexport function onRequest(request) {\n    if (request.url === '/api/app/status') {\n        return {\n            body: JSON.stringify({ status: 'ready', mode: 'local' }),\n            contentType: 'application/json'\n        };\n    }\n    return {}; // static content lives under public/\n}\n",encoding='utf-8')
    (ROOT/'01 项目资料'/'阅读顺序.txt').write_text('先阅读培训须知，再查看项目说明和演示文稿。',encoding='utf-8')
    shutil.copyfile(ROOT/'设计视觉.png',ROOT/'02 设计素材'/'封面视觉.png')
    for i in range(340):(ROOT/'05 大目录'/f'项目记录 {i+1:04d}.txt').write_text('Generated demonstration content\n',encoding='utf-8')
    doc=Document();section=doc.sections[0];section.header.paragraphs[0].text='HTTPLOCALSERVER  /  PROJECT NOTES'
    style=doc.styles['Normal'];style.font.name='Microsoft YaHei';style.font.size=DPt(11);style.font.color.rgb=DColor.from_string('18324D')
    doc.add_heading('项目说明',0);doc.add_paragraph('把本机内容，交到需要的人手中。','Subtitle')
    doc.add_heading('一个入口，两种服务',1)
    doc.add_paragraph('HttpLocalServer 为局域网提供文件空间，也托管网页和轻量应用。浏览、预览、下载与提交都围绕明确的访问范围工作。')
    doc.add_picture(str(ROOT/'设计视觉.png'),width=DInches(6))
    doc.add_heading('本周交付清单',1)
    table=doc.add_table(rows=1,cols=3);table.style='Light Shading Accent 1'
    for cell,text in zip(table.rows[0].cells,['内容','负责人','状态']):cell.text=text
    for row in [('培训资料','资料组','已归档'),('演示网站','设计组','可预览'),('素材目录','工作室','持续更新')]:
        for cell,text in zip(table.add_row().cells,row):cell.text=text
    doc.add_page_break();doc.add_heading('交付与协作',0)
    for title,text in [('先预览，再下载','文档、媒体和文本采用适合的阅读方式。'),('按规则开放写入','只读、共享写入或独立 IP 空间按场景选择。'),('版本确认后发布','文件写入完整发布，同名冲突需要明确确认。')]:
        doc.add_heading(title,1);doc.add_paragraph(text)
    doc.save(ROOT/'项目说明.docx')
    prs=Presentation();prs.slide_width=Inches(13.333);prs.slide_height=Inches(7.5)
    for i,(title,sub) in enumerate([('让本机，成为服务入口','文件 · 网页 · 轻量应用'),('找到需要的内容','浏览、检索与多标签'),('把资料交到别人手中','预览后下载，按规则提交'),('开放多少，由主机决定','权限、额度与冻结保护'),('热点留下，冷内容让位','缓存、复用与退火'),('LOCAL. CONNECTED.','HttpLocalServer')]):
        sl=prs.slides.add_slide(prs.slide_layouts[6]);sl.background.fill.solid();sl.background.fill.fore_color.rgb=RGBColor.from_string('F4F7FC')
        sh=sl.shapes.add_shape(1,Inches(8.3),Inches(.8),Inches(4.3),Inches(5.9));sh.fill.solid();sh.fill.fore_color.rgb=RGBColor.from_string('2158B4');sh.line.fill.background()
        for yy,txt,size,color in [(1.1,'HTTPLOCALSERVER / '+f'{i+1:02d}',16,'536B84'),(2.3,title,34,'18324D'),(4.0,sub,22,'536B84')]:
            tb=sl.shapes.add_textbox(Inches(.8),Inches(yy),Inches(7),Inches(1.1));tb.text=txt;p=tb.text_frame.paragraphs[0];p.font.name='Microsoft YaHei';p.font.size=Pt(size);p.font.color.rgb=RGBColor.from_string(color)
        if i==0:sl.shapes.add_picture(str(ROOT/'设计视觉.png'),Inches(8.3),Inches(2.0),width=Inches(4.3))
        else:
            tb=sl.shapes.add_textbox(Inches(8.7),Inches(2.6),Inches(3.4),Inches(2));tb.text=f'{i+1:02d}';p=tb.text_frame.paragraphs[0];p.font.size=Pt(110);p.font.color.rgb=RGBColor.from_string('92F0DC')
    prs.save(ROOT/'演示文稿.pptx')
    wb=Workbook();s=wb.active;s.title='项目预算';s.merge_cells('A1:F2');s['A1']='项目预算与交付计划';s['A1'].font=Font(name='Microsoft YaHei',size=24,bold=True,color='18324D');s['A1'].fill=PatternFill('solid',fgColor='D8EAF3');s['A1'].alignment=Alignment(vertical='center')
    rows=[['类别','项目','数量','单价','预算','状态'],['资料','说明文档',12,80,960,'已确认'],['设计','素材整理',8,150,1200,'进行中'],['开发','演示网站',1,2400,2400,'已完成'],['验证','设备测试',6,120,720,'进行中'],['交付','发布准备',1,600,600,'待开始']]
    for i,row in enumerate(rows,4):
        for j,v in enumerate(row,1):
            c=s.cell(i,j,v);c.font=Font(name='Microsoft YaHei',size=14,bold=i==4,color='FFFFFF' if i==4 else '18324D');c.fill=PatternFill('solid',fgColor='2158B4' if i==4 else ('EDF3FA' if i%2==0 else 'FFFFFF'));c.alignment=Alignment(vertical='center');c.border=Border(bottom=Side(style='hair',color='DAE4EF'))
        s.row_dimensions[i].height=36
    for col,width in [('A',18),('B',28),('C',14),('D',16),('E',18),('F',22)]:s.column_dimensions[col].width=width
    s.freeze_panes='C5';wb.create_sheet('交付清单').append(['文件','版本','负责人']);wb.save(ROOT/'预算示例.xlsx')
    site=WORK/'demo'/'site';site.mkdir(exist_ok=True)
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><style>*{box-sizing:border-box}body{margin:0;background:#F4F7FC;color:#18324D;font:20px "Microsoft YaHei",sans-serif}main{max-width:1400px;margin:0 auto;padding:76px 70px}.nav{display:flex;justify-content:space-between;border-bottom:1px solid #CEDBE8;padding-bottom:28px;letter-spacing:.1em}.eyebrow{color:#536B84;margin-top:82px;font-size:16px}h1{font-size:74px;line-height:1.25;max-width:980px;margin:28px 0}p{font-size:23px;line-height:1.8;color:#536B84;max-width:850px}.button{display:inline-block;padding:17px 30px;border-radius:9px;background:#2158B4;color:#fff;margin-top:25px}.stats{display:flex;gap:80px;border-top:1px solid #CEDBE8;margin-top:80px;padding-top:30px}.stats strong{font-size:29px;display:block}.stats small{font-size:16px;color:#536B84}.dot{display:inline-block;background:#92F0DC;border:4px solid #2158B4;border-radius:50%;width:24px;height:24px;vertical-align:middle}</style><main><div class="nav"><b><span class="dot"></span> LOCAL STUDIO</b><span>项目 / 资料 / 关于</span></div><div class="eyebrow">SERVED BY HTTPLOCALSERVER</div><h1>让好的内容，<br>在同一网络相遇。</h1><p>一份项目说明，一个演示站点，一次面对面的分享。<br>从本机开始，把需要的服务交给浏览器。</p><span class="button">查看项目资料 ↗</span><div class="stats"><div><strong>LOCAL</strong><small>本地托管</small></div><div><strong>HTTP</strong><small>浏览器访问</small></div><div><strong>CONNECTED</strong><small>按规则开放</small></div></div></main></html>'''
    (site/'index.html').write_text(html,encoding='utf-8');(ROOT/'演示网页.html').write_text(html,encoding='utf-8')
    with zipfile.ZipFile(ROOT/'网站与说明.zip','w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('index.html',html);z.writestr('readme.md',note);z.write(ROOT/'设计视觉.png','设计视觉.png')
    app=WORK/'demo'/'demo-app.zip'
    with zipfile.ZipFile(app,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('server.qjs',"export function onRequest(r){if(r.url==='/api/app/status')return {body:JSON.stringify({status:'ready',version:'3.5.1'}),contentType:'application/json'};return {};}")
        z.writestr('public/index.html',html.replace('LOCAL STUDIO','LOCAL APP').replace('本地托管','ZIP 应用').replace('SERVED BY HTTPLOCALSERVER','SERVER.QJS + PUBLIC/'))
    # Ordinary playable H.264 generated entirely from our original visual.
    video=ROOT/'演示录像.mp4'
    if not video.exists():
        src='/mnt/'+str((WORK/'demo'/'poster.png').resolve())[0].lower()+str((WORK/'demo'/'poster.png').resolve())[2:].replace('\\','/')
        dst='/mnt/'+str(video.resolve())[0].lower()+str(video.resolve())[2:].replace('\\','/')
        subprocess.run(['wsl','-u','root','--','ffmpeg','-hide_banner','-loglevel','error','-y','-loop','1','-i',src,'-f','lavfi','-i','sine=frequency=220:sample_rate=48000','-t','18','-vf','scale=1280:800','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-r','24','-c:a','aac','-movflags','+faststart',dst],check=True)


def main():
    global OUT
    parser=argparse.ArgumentParser()
    parser.add_argument('--scale',type=int,choices=(1,2),default=1)
    parser.add_argument('--skip-fixtures',action='store_true')
    parser.add_argument('--skip-recordings',action='store_true')
    parser.add_argument('--output',default=str(OUT))
    parser.add_argument('--manifest',default=str(WORK/'capture-manifest.json'))
    args=parser.parse_args()
    OUT=pathlib.Path(args.output).resolve();OUT.mkdir(parents=True,exist_ok=True)
    manifest_path=pathlib.Path(args.manifest).resolve()
    if not args.skip_fixtures:fixtures()
    servers=[];manifest={'binary':EXE.relative_to(PROJECT).as_posix(),'pathBase':'repository root','sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'productVersion':'3.5.1','deviceScaleFactor':args.scale,'captureType':'Actual isolated Chromium rendering of distributed native product; no UI replacement or CSS alterations','demonstrationSettings':{'bind':'127.0.0.1','writeMode':'shared','zipPreview':'host','magika':False,'isolatedDemoOnly':True},'notes':['Captures use explicit demonstration settings, not product defaults.','127.0.0.1 is a truthful local capture endpoint; these are not throughput or LAN reachability measurements.'],'assets':[],'errors':[]}
    def add(slug,page,desc,scenes,kind='png'):
        target=OUT/(slug+'.'+kind)
        page.screenshot(path=str(target),animations='disabled')
        size=Image.open(target).size
        manifest['assets'].append({'slug':slug,'path':target.relative_to(WORK).as_posix(),'width':size[0],'height':size[1],'viewportCSS':page.viewport_size,'deviceScaleFactor':args.scale,'description':desc,'scenes':scenes,'provenance':'actual product capture','url':page.url})
        print('Captured '+slug,flush=True)
        manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    def safe(name,action):
        try:action()
        except Exception as e:
            manifest['errors'].append({'slug':name,'error':str(e)});print('Capture failed '+name+': '+str(e)[:220],flush=True)
    with sync_playwright() as p:
        browser=p.chromium.launch(executable_path=CHROME,headless=True,args=['--no-proxy-server','--autoplay-policy=no-user-gesture-required'])
        manifest['browserVersion']=browser.version
        ctx=browser.new_context(viewport={'width':1600,'height':900},device_scale_factor=args.scale,permissions=['clipboard-read','clipboard-write'])
        page=ctx.new_page()
        # Generate an original selectable PDF with browser fonts before capture.
        pdfhtml='''<html><meta charset="utf-8"><style>@page{size:A4;margin:20mm}body{font:16px/1.8 "Microsoft YaHei";color:#18324D}h1{font-size:42px;color:#2158B4}.bar{height:8px;background:#3C8CF5;width:70px}.label{letter-spacing:3px;color:#536B84;font-size:12px}.box{background:#EDF3FA;padding:25px;border-radius:10px;margin:32px 0}h2{font-size:23px}.page{break-after:page}table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:15px;border-bottom:1px solid #CEDBE8}</style><section class="page"><div class="label">HTTPLOCALSERVER · PROJECT BRIEF</div><h1>培训资料与交付须知</h1><div class="bar"></div><p>把本机内容，交到需要的人手中。</p><div class="box"><h2>开始之前</h2><p>请先确认资料版本，在线预览内容，再按需要下载。上传或替换文件时，检查目标目录和同名提示。</p></div><h2>本次内容</h2><table><tr><th>内容</th><th>用途</th></tr><tr><td>项目说明</td><td>了解目标与交付范围</td></tr><tr><td>演示文稿</td><td>现场交流与放映</td></tr><tr><td>预算示例</td><td>表格阅读与复制</td></tr></table><p>示例资料 / 2026.11</p></section><section><div class="label">READ · VERIFY · SHARE</div><h1>按规则协作</h1><h2>01 选择正确目录</h2><p>共享写入跟随目标位置。独立写入仅允许自己的IP目录。</p><h2>02 保留版本检查</h2><p>文件变动时重新确认。不要将不同版本混接成一个下载。</p><h2>03 主机统一设置</h2><p>公告、关注文件、预览开关和额度由主机调整。</p></section></html>'''
        page.set_content(pdfhtml);page.pdf(path=str(ROOT/'培训须知.pdf'),print_background=True)
        server=Server();servers.append(server);base=server.base
        page.goto(base);page.locator('.file').first.wait_for();page.wait_for_timeout(600)
        session=page.request.get(base+'/api/session').json()
        page.request.post(base+'/api/admin/settings',headers={'X-HLS-Token':session['token']},data={'title':'项目资料与在线下载','announcement':'项目资料已更新。请先阅读培训须知，再查看演示文稿。','writeMode':'shared'})
        page.reload();page.locator('.file').first.wait_for();page.wait_for_timeout(700)
        add('manager-list',page,'默认列表；真实示例资料、连接与本机管理入口',['S05','S09','S12'])
        page.locator('#grid-view').click();page.wait_for_timeout(350);add('manager-grid',page,'真实网格布局',['S05','S21'])
        page.locator('#list-view').click()
        for name in ['01 项目资料','02 设计素材']:
            page.locator('#new-tab').click();page.locator('#address').click(position={'x':180,'y':18});page.locator('#path-input').fill('/'+name);page.locator('#path-input').press('Enter');page.wait_for_timeout(300)
        add('tabs',page,'三个实际目录标签及路径导航',['S07'])
        page.locator('.tab').first.click();page.locator('#search').fill('说明');page.wait_for_timeout(900);add('search',page,'文件名搜索“说明”结果',['S08']);page.locator('#clear-search').click()
        page.locator('#theme-dark').click();page.wait_for_timeout(400);add('theme-dark',page,'真实夜间主题',['S06']);page.locator('#theme-light').click();page.wait_for_timeout(300)
        page.locator('#host-settings').click();page.wait_for_timeout(300);add('settings',page,'本机主机设置，敏感输入为空/遮蔽',['S24','S30','S32']);page.locator('#close-settings').click()
        mob=browser.new_context(viewport={'width':390,'height':844},is_mobile=True,has_touch=True,device_scale_factor=args.scale);mp=mob.new_page();mp.goto(base);mp.locator('.file').first.wait_for();mp.wait_for_timeout(400)
        if mp.locator('#dismiss-notice').is_visible():mp.locator('#dismiss-notice').click();mp.wait_for_timeout(250)
        add('mobile',mp,'390×844真实手机布局；已通过产品按钮收起公告',['S06','S41']);mob.close()
        def viewer(slug,file,kind,desc,scenes):
            page.goto(base+'/_hls/viewer?'+urllib.parse.urlencode({'path':'/'+file,'kind':kind}));page.wait_for_function("document.querySelector('#stage').dataset.ready==='true'",timeout=65000);page.wait_for_timeout(800)
            if page.locator('#status').evaluate("e=>e.classList.contains('error')"):raise RuntimeError(page.locator('#status').inner_text())
            if kind=='zip':
                page.locator('#zip-list').get_by_text('index.html',exact=True).click()
                page.frame_locator('#stage iframe').locator('h1').wait_for(timeout=30000)
                page.wait_for_timeout(1200)
            add(slug,page,desc,scenes)
        for entry in [
            ('preview-pdf','培训须知.pdf','pdf','PDF连续阅读与可选择文字',['S16']),
            ('preview-docx','项目说明.docx','office','Word文档与图文真实渲染',['S16']),
            ('preview-ppt','演示文稿.pptx','office','PPT比例缩略图、页面和播放工具',['S17']),
            ('preview-xlsx','预算示例.xlsx','office','Excel含合并标题与数据',['S18']),
            ('editor','示例代码.js','editor','实际内置Monaco中文代码编辑器',['S14']),
            ('zip','网站与说明.zip','zip','实际主机ZIP预览目录与包内网页；通过点击index.html打开',['S22','S23'])]:safe(entry[0],lambda a=entry:viewer(*a))
        for slug,file,selector,desc,scenes in [
            ('preview-markdown','项目说明.md','.markdown-document, .markdown-preview','Markdown文档及关联图片',['S19']),
            ('preview-json','配置示例.json','.json-document, .json-tree','按需展开JSON树',['S19']),
            ('preview-image','设计视觉.png','#preview img','原创设计视觉的图片预览',['S20']),
            ('preview-video','演示录像.mp4','.player-stage, .player-controls','实际自定义视频播放器',['S20']),
            ('preview-web','演示网页.html','#preview iframe','受控HTML网页与自身样式',['S20'])]:
            def action(slug=slug,file=file,selector=selector,desc=desc,scenes=scenes):
                page.goto(base+'/preview?'+urllib.parse.urlencode({'path':'/'+file}));page.locator(selector).first.wait_for(timeout=30000);page.wait_for_timeout(900)
                if slug=='preview-video':
                    page.locator('.player-play').click();page.wait_for_timeout(1200);page.locator('.player-play').click()
                add(slug,page,desc,scenes)
            safe(slug,action)
        login=Server(extra=('--password','demonstration-only'));servers.append(login);page.goto(login.base);page.locator('#password').wait_for();add('login',page,'实际登录页；口令未输入',['S30'])
        static=Server('server',WORK/'demo'/'site');servers.append(static);page.goto(static.base);page.wait_for_timeout(300);add('server-site',page,'本机原生静态Server托管的原创演示站',['S33'])
        app=Server('server',WORK/'demo'/'app-data',('--workspace-app',str(WORK/'demo'/'demo-app.zip')));servers.append(app);page.goto(app.base);page.wait_for_timeout(300);add('workspace-app',page,'public/前端由真实ZIP应用提供',['S34'])
        # Separate short recordings: real input and product behavior, no fabricated cursor.
        videos=[] if args.skip_recordings else [('theme-switch',6),('tabs-drag',7),('file-browse',8),('ppt-navigation',8)]
        for slug,duration in videos:
            def record(slug=slug,duration=duration):
                videoctx=browser.new_context(viewport={'width':1600,'height':900},record_video_dir=str(RAW),record_video_size={'width':1600,'height':900})
                vp=videoctx.new_page()
                try:
                    if slug=='ppt-navigation':
                        vp.goto(base+'/_hls/viewer?'+urllib.parse.urlencode({'path':'/演示文稿.pptx','kind':'office'}));vp.wait_for_function("document.querySelector('#stage').dataset.ready==='true'",timeout=65000);vp.wait_for_timeout(600)
                        for _ in range(3):vp.locator('#next').click();vp.wait_for_timeout(1100)
                    else:
                        vp.goto(base);vp.locator('.file').first.wait_for();vp.wait_for_timeout(600)
                        if slug=='theme-switch':
                            for id in ['theme-dark','theme-light','theme-auto']:vp.locator('#'+id).click();vp.wait_for_timeout(900)
                        elif slug=='tabs-drag':
                            for name in ['01 项目资料','02 设计素材']:
                                vp.locator('#new-tab').click();vp.locator('#address').click(position={'x':160,'y':18});vp.locator('#path-input').fill('/'+name);vp.locator('#path-input').press('Enter');vp.wait_for_timeout(600)
                            boxes=[vp.locator('.tab').nth(i).bounding_box() for i in range(3)];a,b=boxes[2],boxes[0]
                            vp.mouse.move(a['x']+55,a['y']+20);vp.mouse.down();vp.mouse.move(b['x']+50,b['y']+20,steps=30);vp.wait_for_timeout(600);vp.mouse.up();vp.wait_for_timeout(600)
                        else:
                            vp.locator('#grid-view').click();vp.wait_for_timeout(700);vp.locator('#list-view').click();vp.wait_for_timeout(500);vp.locator('#search').fill('说明');vp.wait_for_timeout(900);vp.locator('#clear-search').click();vp.wait_for_timeout(600)
                    vp.wait_for_timeout(1000);video=vp.video;vp.close();videoctx.close();target=OUT/(slug+'.webm');video.save_as(str(target))
                    manifest['assets'].append({'slug':slug,'path':target.relative_to(WORK).as_posix(),'width':1600,'height':900,'description':'真实Chrome操作录制；含页面加载，可按剪辑需要取有效片段','scenes':['S06' if slug=='theme-switch' else 'S07' if slug=='tabs-drag' else 'S17' if slug=='ppt-navigation' else 'S05'],'provenance':'actual screen recording','format':'webm'})
                    print('Recorded '+slug,flush=True)
                finally:
                    try:videoctx.close()
                    except Exception:pass
            safe(slug,record)
        ctx.close();browser.close()
        for s in servers:s.close()
    manifest['captureCompleted']=True
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Done '+str(len(manifest['assets']))+' assets; '+str(len(manifest['errors']))+' warnings',flush=True)


if __name__=='__main__':main()
