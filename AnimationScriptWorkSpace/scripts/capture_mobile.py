"""Refresh the mobile hero capture using only the real product controls."""
from capture_all import Server, WORK, OUT, CHROME, sync_playwright
import json

server = Server()
try:
    with sync_playwright() as p:
        browser = p.chromium.launch(executable_path=CHROME, headless=True, args=['--no-proxy-server'])
        context = browser.new_context(viewport={'width': 390, 'height': 844}, is_mobile=True, has_touch=True, device_scale_factor=1)
        page = context.new_page()
        page.goto(server.base)
        page.locator('.file').first.wait_for()
        session = page.request.get(server.base + '/api/session').json()
        page.request.post(server.base + '/api/admin/settings', headers={'X-HLS-Token': session['token']}, data={'title': '项目资料与在线下载', 'announcement': '项目资料已更新。请先阅读培训须知，再查看演示文稿。', 'writeMode': 'shared'})
        page.reload()
        page.locator('.file').first.wait_for()
        page.wait_for_timeout(400)
        if page.locator('#dismiss-notice').is_visible():
            page.locator('#dismiss-notice').click()
            page.wait_for_timeout(250)
        page.screenshot(path=str(OUT / 'mobile.png'), animations='disabled')
        manifest = json.loads((WORK / 'capture-manifest.json').read_text(encoding='utf-8'))
        for asset in manifest['assets']:
            if asset['slug'] == 'mobile':
                asset['url'] = page.url
                asset['description'] = '390×844真实手机布局；已通过产品按钮收起公告'
        manifest['demonstrationSettings'] = {'bind': '127.0.0.1', 'writeMode': 'shared', 'zipPreview': 'host', 'magika': False, 'isolatedDemoOnly': True}
        manifest['notes'] = ['Captures use explicit demonstration settings, not product defaults.', '127.0.0.1 is a truthful local capture endpoint; these are not throughput or LAN reachability measurements.']
        (WORK / 'capture-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
        context.close()
        browser.close()
finally:
    server.close()
print('Mobile capture refreshed; announcement dismissed via product button.')
