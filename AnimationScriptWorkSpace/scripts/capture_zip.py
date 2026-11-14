"""Capture archive contents after opening them using the real archive browser."""
from capture_all import Server, WORK, OUT, CHROME, sync_playwright
import json
import urllib.parse

server = Server()
try:
    with sync_playwright() as p:
        browser = p.chromium.launch(executable_path=CHROME, headless=True, args=['--no-proxy-server'])
        context = browser.new_context(viewport={'width': 1600, 'height': 900}, device_scale_factor=1)
        page = context.new_page()
        page.goto(server.base + '/_hls/viewer?' + urllib.parse.urlencode({'path': '/网站与说明.zip', 'kind': 'zip'}))
        page.locator('#zip-list').get_by_text('index.html', exact=True).wait_for(timeout=30000)
        page.locator('#zip-list').get_by_text('index.html', exact=True).click()
        page.frame_locator('#stage iframe').locator('h1').wait_for(timeout=30000)
        page.wait_for_timeout(1400)
        page.screenshot(path=str(OUT / 'zip.png'), animations='disabled')
        manifest = json.loads((WORK / 'capture-manifest.json').read_text(encoding='utf-8'))
        for asset in manifest['assets']:
            if asset['slug'] == 'zip':
                asset['url'] = page.url
                asset['description'] = '实际主机ZIP预览目录与包内网页；通过点击index.html打开'
        (WORK / 'capture-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
        context.close()
        browser.close()
finally:
    server.close()
print('ZIP content capture refreshed.')
