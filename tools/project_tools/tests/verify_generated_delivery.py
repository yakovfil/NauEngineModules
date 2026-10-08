"""Run a relocated generated production delivery and inspect visible scene output."""
import argparse
import ctypes
import functools
import hashlib
import http.server
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import threading
import time
from PIL import Image, ImageChops
from playwright.sync_api import sync_playwright

def close_windows(pid):
    user = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def callback(hwnd, _):
        owner = ctypes.c_ulong()
        user.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(owner))
        if owner.value == pid:
            user.PostMessageW(ctypes.c_void_p(hwnd), 0x10, 0, 0)
        return True
    user.EnumWindows(callback_type(callback), 0)


def capture_owned_window(pid, path, resize=False):
    windows = json.loads(subprocess.check_output(["orca", "computer", "list-windows", "--app", f"pid:{pid}", "--json"], text=True))["result"]["windows"]
    assert len(windows) == 1, windows
    handle = windows[0]["id"]
    user = ctypes.WinDLL("user32")
    user.SetWindowPos.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    user.ShowWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
    if resize:
        for width, height in ((420, 680), (1000, 420)):
            assert user.SetWindowPos(handle, ctypes.c_void_p(-1), 100, 100, width, height, 0x40)
            time.sleep(.2)
        user.ShowWindow(handle, 6)
        time.sleep(.2)
        user.ShowWindow(handle, 9)
    assert user.SetWindowPos(handle, ctypes.c_void_p(-1), 100, 100, 900, 600, 0x40)
    time.sleep(.3)
    snapshot = json.loads(subprocess.check_output(["orca", "computer", "get-app-state", "--app", f"pid:{pid}", "--window-id", str(handle), "--json"], text=True))["result"]
    assert snapshot["screenshotStatus"]["state"] == "captured", snapshot
    shutil.copy2(snapshot["screenshot"]["path"], path)
    class Point(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]
    class Rect(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]
    user.GetClientRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(Rect)]
    user.ClientToScreen.argtypes = [ctypes.c_void_p, ctypes.POINTER(Point)]
    client, origin = Rect(), Point()
    assert user.GetClientRect(handle, ctypes.byref(client))
    assert user.ClientToScreen(handle, ctypes.byref(origin))
    window = snapshot["snapshot"]["window"]
    x, y = origin.x - window["x"], origin.y - window["y"]
    # Inspect only the actual drawable, excluding desktop pixels in the DWM
    # frame and native title-bar controls.
    Image.open(path).crop((x, y, x + client.right, y + client.bottom)).save(path)



def check_image(path):
    pixels=list(Image.open(path).convert('RGB').getdata())
    blue=sum(b > r * 1.5 and b > g * 1.1 and b > 60 for r,g,b in pixels)
    orange=sum(r > g * 1.2 and g > b * 1.4 and r > 60 for r,g,b in pixels)
    assert blue > 1000 and orange > 1000, (blue,orange)
    return {'bluePixels':blue,'orangePixels':orange}


def verify(project, target, platform, configuration):
    build=project/'build/basic-rendering-scene'/f'{platform.lower()}-{configuration.lower()}'
    output=project/'build/acceptance'/f'{platform.lower()}-{configuration.lower()}'
    output.mkdir(parents=True,exist_ok=True)
    archive=(build/'package'/('content.nau_pack' if platform=='Windows' else target+'.data')).read_bytes()
    logs=[]
    with tempfile.TemporaryDirectory(prefix='Nau relocated generated ') as temporary:
        root=pathlib.Path(temporary)
        delivery=root/'nested/application'
        shutil.copytree(build/'package',delivery)
        if platform=='Windows':
            env=os.environ.copy();env['PATH']=os.pathsep.join([os.environ['SystemRoot'],str(pathlib.Path(os.environ['SystemRoot'])/'System32')])
            process=subprocess.Popen([str(delivery/(target+'.exe'))],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            ready=threading.Event()
            def read():
                for line in process.stdout:
                    logs.append(line)
                    if 'Packaged scene Running' in line: ready.set()
            reader=threading.Thread(target=read,daemon=True);reader.start()
            try:
                assert ready.wait(30), ''.join(logs)
                capture_owned_window(process.pid,output/'first.png')
                time.sleep(.5)
                capture_owned_window(process.pid,output/'second.png',resize=True)
                close_windows(process.pid)
                assert process.wait(timeout=20)==0
                reader.join(timeout=3)
                assert 'presentation cleanup complete' in ''.join(logs)
                assert 'Shutdown without destroying' not in ''.join(logs)
            finally:
                if process.poll() is None:
                    close_windows(process.pid)
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait()
                (output/'runtime.log').write_text(''.join(logs))
        else:
            spec=importlib.util.spec_from_file_location('delivery_server',delivery/'serve.py')
            server_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(server_module)
            server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(server_module.Handler,directory=str(root)))
            worker=threading.Thread(target=server.serve_forever);worker.start()
            try:
                with sync_playwright() as p:
                    browser=p.chromium.launch(channel='chrome',args=['--enable-unsafe-swiftshader'])
                    page=browser.new_page(viewport={'width':900,'height':600})
                    page.on('console',lambda message: logs.append(message.text))
                    page.goto(f'http://127.0.0.1:{server.server_port}/nested/application/')
                    page.wait_for_function("Module.nauRuntime?.status?.state === 'Running'",timeout=30000)
                    page.locator('#canvas').screenshot(path=str(output/'first.png'))
                    page.wait_for_timeout(600)
                    page.locator('#canvas').screenshot(path=str(output/'second.png'))
                    page.evaluate('Module.nauRuntime.stop();Module.nauRuntime.stop()')
                    page.wait_for_function('Module.nauRuntime.status.cleanupComplete',timeout=30000)
                    assert page.evaluate('Module.nauRuntime.status.state')=='Stopped'
                    browser.close()
            finally:
                (output/'runtime.log').write_text('\n'.join(logs))
                server.shutdown();worker.join();server.server_close()
        image=check_image(output/'second.png')
        first=Image.open(output/'first.png').convert('RGB');second=Image.open(output/'second.png').convert('RGB')
        assert first.size==second.size
        assert ImageChops.difference(first,second).getbbox(), 'No observable component motion'
    results={'platform':platform,'configuration':configuration,'archiveSha256':hashlib.sha256(archive).hexdigest(),**image}
    (output/'results.json').write_text(json.dumps(results,indent=2))
    print('PASS',platform,configuration,results)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',required=True,type=pathlib.Path)
    parser.add_argument('--target',required=True)
    parser.add_argument('--platform',choices=['Windows','Web'],required=True)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    args=parser.parse_args()
    verify(args.project.resolve(),args.target,args.platform,args.configuration)
