"""Run portable kernel package tests in installed Chrome."""
import argparse, functools, http.server, json, pathlib, tempfile, threading
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=pathlib.Path, required=True)
args = parser.parse_args()
binary = args.build.resolve()/'bin'
(binary/'index.html').write_text('''<!doctype html><script>
window.logs=[];var Module={print:s=>logs.push(s),printErr:s=>logs.push(s),
onExit:code=>window.result={code,logs},onAbort:error=>window.result={code:1,error,logs}};
</script><script src="NauAssetPackTests.js"></script>''')
class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy','same-origin')
        self.send_header('Cross-Origin-Embedder-Policy','require-corp')
        super().end_headers()
    def log_message(self,*args): pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(binary)))
threading.Thread(target=server.serve_forever,daemon=True).start()
result={'code':1}
try:
    with sync_playwright() as p:
        browser=p.chromium.launch(channel='chrome',headless=True)
        page=browser.new_page()
        page.goto(f'http://127.0.0.1:{server.server_port}/index.html')
        page.wait_for_function('window.result',timeout=30000)
        result=page.evaluate('window.result')
        browser.close()
finally:
    server.shutdown();server.server_close()
    (args.build/'browser-results.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
raise SystemExit(result['code'])
