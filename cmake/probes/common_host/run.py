"""Validate the same owned application delegate on native and real Chrome."""
import argparse, functools, http.server, json, pathlib, subprocess, threading, tempfile, time
from contextlib import contextmanager
from playwright.sync_api import sync_playwright
@contextmanager
def chrome_session(playwright):
    # no_defaults preserves genuine document visibility; Playwright's default
    # controlled-page settings make all pages visible in the tested version.
    with tempfile.TemporaryDirectory(prefix='nau-host-chrome-', ignore_cleanup_errors=True) as profile:
        process = subprocess.Popen(['C:/Program Files/Google/Chrome/Application/chrome.exe',
            '--headless=new',f'--user-data-dir={profile}','--remote-debugging-port=0',
            '--no-first-run','--no-default-browser-check','about:blank'],
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        browser = None
        try:
            port = pathlib.Path(profile)/'DevToolsActivePort'
            deadline=time.monotonic()+15
            while not port.exists():
                if process.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('Chrome did not expose its debugging port')
                time.sleep(.05)
            browser=playwright.chromium.connect_over_cdp('http://127.0.0.1:'+port.read_text().splitlines()[0],no_defaults=True)
            yield browser
        finally:
            if browser:
                try: browser.contexts[0].new_cdp_session(browser.contexts[0].pages[0]).send('Browser.close')
                except Exception: pass
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait(timeout=5)


parser=argparse.ArgumentParser()
parser.add_argument('--build',type=pathlib.Path,required=True)
parser.add_argument('--scenario',default='success')
parser.add_argument('--configuration',default='Debug')
a=parser.parse_args();build=a.build.resolve();scenario=a.scenario
expected_failure=scenario in ('immediate','deferred','shutdownerror','configfail','setupfail','stall')
result={'scenario':scenario,'passed':False};logs=[]
try:
    if (build/'bin/NauCommonHostTests.js').exists():
        class Handler(http.server.SimpleHTTPRequestHandler):
            def end_headers(self):
                self.send_header('Cross-Origin-Opener-Policy','same-origin');self.send_header('Cross-Origin-Embedder-Policy','require-corp');super().end_headers()
            def log_message(self,*args):pass
        (build/'bin/index.html').write_text(pathlib.Path(__file__).with_name('fixture.html').read_text())
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(build/'bin')))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            with sync_playwright() as p:
                with chrome_session(p) as browser:
                    context=browser.contexts[0];page=context.pages[0]
                    page.on('console',lambda m:logs.append(m.text))
                    page.goto(f'http://127.0.0.1:{server.server_port}/index.html?scenario={scenario}')
                    if scenario=='visibility':
                        page.wait_for_function("Module.nauRuntime?.status?.state==='Running'")
                        other=context.new_page();other.bring_to_front();page.wait_for_function('document.hidden')
                        page.wait_for_timeout(200);page.bring_to_front();page.wait_for_function('!document.hidden')
                        page.wait_for_timeout(200);page.evaluate('Module.nauRuntime.stop()')
                    page.wait_for_function('window.result',timeout=20000)
                    terminal=page.evaluate('window.result');result.update(terminal)
                    assert terminal['status']['cleanupComplete'],terminal
                    assert terminal['status']['state']==('Failed' if expected_failure else 'Stopped'),terminal
                    if scenario in ('immediate','deferred','stopstartup','stall'):
                        assert all(s['state']!='Running' and s['completedSteps']==0 for s in terminal['history']),terminal
                    assert terminal['beats']>0
        finally:server.shutdown();server.server_close()
    else:
        executable=build/f'bin/{a.configuration}/NauCommonHostTests.exe'
        process=subprocess.run([str(executable),scenario],capture_output=True,text=True,timeout=20)
        result['code']=process.returncode;logs=[process.stdout,process.stderr]
        row=next(line for line in process.stdout.splitlines() if line.startswith('HOST_RESULT '))
        result['stats']=json.loads(row.removeprefix('HOST_RESULT '))
    assert (result['code']!=0)==expected_failure,result
    stats=result['stats'];assert stats['valid'],stats
    if scenario in ('immediate','deferred','stopstartup','stall','configfail','setupfail'):assert stats['pre']==stats['post']==0,stats
    elif scenario=='stoppre':assert stats['pre']==1 and stats['post']==0,stats
    else:assert stats['pre']==stats['post'] and stats['post']>=4,stats
    assert stats['legacy']==(stats['post'] if scenario=='legacy' else 0),stats
    assert stats['initialized']==stats['startup']==(0 if scenario in ('configfail','setupfail') else 1),stats
    result['passed']=True
except Exception as error:result['error']=str(error)
result['logs']=logs
folder=build/'results'/a.configuration;folder.mkdir(exist_ok=True,parents=True)
(folder/(scenario+'.json')).write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2));raise SystemExit(0 if result['passed'] else 1)
