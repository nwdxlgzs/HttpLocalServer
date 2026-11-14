"""Create disposable browser fixtures under a new, dedicated output directory."""
import math
import json
import pathlib
import struct
import sys
import wave
import zlib

root = pathlib.Path(sys.argv[1]).resolve()
root.mkdir(exist_ok=False)
for name in ["项目资料", "文档", "网页示例"]:
    (root / name).mkdir()
(root / "说明.txt").write_text("在线文件下载。\n\n文本、图片和流式媒体预览测试。\n" * 20, encoding="utf-8")
(root / "源代码.cpp").write_text('#include <iostream>\nint main(){ std::cout << "hello"; }', encoding="utf-8")
(root / "设计文档.pdf").write_bytes(b"%PDF-1.7\nfixture")
(root / "assets").mkdir()
(root / "assets/data.json").write_text('{"message":"根路径 JSON 请求成功"}', encoding="utf-8")
(root / "assets/app.js").write_text('var x=new XMLHttpRequest();x.open("GET","/assets/data.json");x.onload=function(){document.getElementById("resource").textContent=JSON.parse(x.responseText).message;};x.send();var a=new XMLHttpRequest();a.open("GET","/api/session");a.onload=function(){document.getElementById("guard").textContent="管理接口状态："+a.status;};a.onerror=function(){document.getElementById("guard").textContent="管理接口已隔离";};a.send();', encoding="utf-8")
(root / "网页示例/index.html").write_text('<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="style.css"></head><body><h1>网页预览</h1><p id="resource">等待关联资源</p><p id="guard">等待隔离核验</p><script src="/assets/app.js"></script></body></html>', encoding="utf-8")
(root / "网页示例/style.css").write_text("body{font:14px sans-serif;padding:24px;color:#395b82;background:#f2f6fc}h1{font-size:24px}", encoding="utf-8")
(root / "示意图.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" width="480" height="320" viewBox="0 0 480 320"><rect width="480" height="320" rx="20" fill="#eaf1fb"/><circle cx="240" cy="140" r="68" fill="#a1c0e5"/><path d="M100 260l110-130 70 80 40-50 70 100" fill="#5c8fc9"/></svg>', encoding="utf-8")
def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
pixels = bytearray()
for y in range(420):
    pixels.append(0)
    for x in range(640):
        color = (232,239,248)
        if (x-320)**2+(y-150)**2 < 80**2: color = (180,204,233)
        if y > 420-abs(x-280)*.7: color = (123,159,201)
        pixels.extend(color)
(root / "风景.png").write_bytes(b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB',640,420,8,2,0,0,0)) + png_chunk(b'IDAT',zlib.compress(pixels)) + png_chunk(b'IEND',b''))
(root / "readme.md").write_text('# 预览验收\n\n**Markdown 粗体**\n\n<script>alert("不应执行")</script>\n\n[危险链接](javascript:alert(1))\n\n![站内图片](风景.png)\n',encoding='utf-8')
(root / "data.json").write_text(json.dumps({'tree':{'items':list(range(250))},'中文':'值'},ensure_ascii=False),encoding='utf-8')
(root / "archive-fixture/empty").mkdir(parents=True)
(root / "archive-fixture/中文文件.txt").write_text('客户端本地 ZIP 校验\n'*1000,encoding='utf-8')
(root / "archive-fixture/data.bin").write_bytes(bytes(range(256))*4000)
with wave.open(str(root / "演示音频.wav"), "wb") as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(22050)
    output.writeframes(b"".join(struct.pack("<h", int(1500 * math.sin(i * 2 * math.pi * 440 / 22050))) for i in range(22050 * 20)))
for index in range(150):
    (root / f"资料-{index:03}.txt").write_text("fixture", encoding="utf-8")
print(root)
