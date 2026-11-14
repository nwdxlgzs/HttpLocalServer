"""Create truthful example.com scan destination and decodable QR demo assets."""
from pathlib import Path
import hashlib
import json
import cv2
import qrcode
from playwright.sync_api import sync_playwright

WORK = Path(__file__).resolve().parents[1]
OUT = WORK / 'assets' / 'capture'
URL = 'https://example.com'
CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
png = OUT / 'qr-example-com.png'
svg = OUT / 'qr-example-com.svg'
screenshot = OUT / 'example-com-mobile.png'

qr = qrcode.QRCode(version=None, error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=20, border=4)
qr.add_data(URL)
qr.make(fit=True)
qr.make_image(fill_color='black', back_color='white').convert('RGB').save(png)
matrix = qr.get_matrix()
size = len(matrix)
rects = ''.join(f'<rect x="{x}" y="{y}" width="1" height="1"/>' for y, row in enumerate(matrix) for x, dark in enumerate(row) if dark)
svg.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{size*20}" height="{size*20}" viewBox="0 0 {size} {size}" shape-rendering="crispEdges"><title>QR code for https://example.com</title><rect width="{size}" height="{size}" fill="#fff"/><g fill="#000">{rects}</g></svg>', encoding='utf-8')
decoded, points, _ = cv2.QRCodeDetector().detectAndDecode(cv2.imread(str(png)))
assert decoded == URL and points is not None, repr(decoded)

with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=CHROME, headless=True)
    context = browser.new_context(viewport={'width':390,'height':844}, is_mobile=True, has_touch=True, device_scale_factor=1)
    page = context.new_page()
    response = page.goto(URL, wait_until='networkidle', timeout=60000)
    page.locator('body').wait_for()
    assert 'documentation examples' in page.locator('body').inner_text(), page.content()
    page.screenshot(path=str(screenshot), animations='disabled')
    browser_version = browser.version
    actual_url = page.url
    title = page.title()
    status = response.status if response else None
    # Independently rasterize the SVG in Chromium then decode with OpenCV too.
    svgpage = context.new_page()
    svgpage.set_viewport_size({'width':size*20,'height':size*20})
    svgpage.goto(svg.as_uri())
    svgpage.screenshot(path=str(WORK/'capture'/'qr-example-svg-validation.png'))
    svg_decoded, svg_points, _ = cv2.QRCodeDetector().detectAndDecode(cv2.imread(str(WORK/'capture'/'qr-example-svg-validation.png')))
    assert svg_decoded == URL and svg_points is not None, repr(svg_decoded)
    context.close()
    browser.close()

def asset(path):
    return {'path':path.relative_to(WORK).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

manifest = {
    'purpose':'Simulated phone QR scan transition for the product film; not a LAN connectivity test.',
    'encodedURL':URL,
    'qr':{'png':asset(png),'svg':asset(svg),'errorCorrection':'M','quietZoneModules':4,'modulesIncludingQuietZone':size,'pixels':size*20,'decodedPNG':decoded,'decodedSVGRaster':svg_decoded,'decoder':'OpenCV '+cv2.__version__+' QRCodeDetector','validationPassed':True},
    'destination':{'asset':asset(screenshot),'requestedURL':URL,'finalURL':actual_url,'httpStatus':status,'title':title,'viewport':{'width':390,'height':844},'browser':'Chrome '+browser_version,'provenance':'Real rendered public example.com page, unmodified browser screenshot'},
    'limitations':['Animated scan gesture is simulated by composition.','QR content intentionally points to example.com; it is not the address of the shared host.','Capture contains page content, without mobile browser chrome.']
}
(WORK/'qr-demo.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(manifest, ensure_ascii=False, indent=2))
