"""Exercise only the owned DX12 checkpoint window and record cleanup diagnostics."""
import argparse
import ctypes
import json
import pathlib
import subprocess
import time


def verify(executable, output):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.ShowWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
    user.SetWindowPos.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
    user.IsWindowVisible.argtypes = [ctypes.c_void_p]
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t)
    output.parent.mkdir(parents=True, exist_ok=True)
    log = output.with_suffix(".log")
    result = dict(passed=False, executable=str(executable), log=str(log))
    with log.open("w") as stream:
        process = subprocess.Popen([str(executable)], cwd=executable.parent, stdout=stream, stderr=subprocess.STDOUT)
        window = None
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and process.poll() is None:
                windows = []
                @callback_type
                def enumerate_window(handle, parameter):
                    identity = ctypes.c_uint()
                    user.GetWindowThreadProcessId(handle, ctypes.byref(identity))
                    if identity.value == process.pid and user.IsWindowVisible(handle):
                        windows.append(handle)
                    return 1
                user.EnumWindows(enumerate_window, 0)
                if windows and "Native driver checkpoint initialized" in log.read_text(errors="replace"):
                    window = windows[0]
                    break
                time.sleep(.1)
            if not window:
                raise RuntimeError("Checkpoint did not initialize its visible window")
            for width, height in ((900, 560), (420, 680), (640, 480)):
                if not user.SetWindowPos(window, None, 0, 0, width, height, 0x6):
                    raise ctypes.WinError(ctypes.get_last_error())
                time.sleep(.2)
                if process.poll() is not None:
                    raise RuntimeError("Checkpoint exited during resize")
            user.ShowWindow(window, 6)
            time.sleep(.3)
            user.ShowWindow(window, 9)
            time.sleep(.3)
            user.PostMessageW(window, 0x10, 0, 0)
            code = process.wait(timeout=15)
            messages = log.read_text(errors="replace")
            failures = [line for line in messages.splitlines() if "[Error]" in line or "[Critical]" in line]
            result.update(exitCode=code, errors=failures, resizeCount=messages.count("ResizeBuffers("))
            result["passed"] = code == 0 and not failures and result["resizeCount"] >= 3 and "Native driver checkpoint finished: 0" in messages
        except Exception as error:
            result["error"] = str(error)
        finally:
            if process.poll() is None:
                if window:
                    user.PostMessageW(window, 0x10, 0, 0)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    output.write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    raise SystemExit(verify(args.executable.resolve(), args.output.resolve()))
