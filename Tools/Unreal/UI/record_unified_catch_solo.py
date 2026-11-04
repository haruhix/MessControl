"""Silent 30fps WGC of one existing owned standalone PIE window and one real catch.

Prepare/run only when authorized. Never starts/stops PIE or activates/moves windows.
Example: python -B Tools/Unreal/UI/record_unified_catch_solo.py --pid 1234 --output E:/CodexWork/MessControl/Validation/UICatch_20261104/Take01
The asynchronous host prints progress promptly; launch with exec yield_time_ms=1000.
"""
import argparse
import ctypes
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback
import uuid


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (FileNotFoundError, PermissionError, json.JSONDecodeError):
        return None


def frame_count(path):
    try:
        frames = re.findall(r'(?m)^frame=(\d+)\s*$', path.read_text(encoding='utf-8'))
        return int(frames[-1]) if frames else 0
    except (FileNotFoundError, PermissionError, UnicodeError):
        return 0


def owned_window(pid):
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetWindowTextLengthW.argtypes = [w.HWND]
    user.GetWindowTextLengthW.restype = ctypes.c_int
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.IsWindowVisible.argtypes = user.IsIconic.argtypes = [w.HWND]
    user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
    windows = []
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    @callback_type
    def visit(hwnd, unused):
        found = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(found))
        if found.value == pid and user.IsWindowVisible(hwnd) and not user.IsIconic(hwnd):
            title = ctypes.create_unicode_buffer(user.GetWindowTextLengthW(hwnd) + 1)
            user.GetWindowTextW(hwnd, title, len(title))
            if 'MessControl' in title.value and 'NetMode: Standalone' in title.value:
                rect = w.RECT()
                assert user.GetWindowRect(hwnd, ctypes.byref(rect))
                windows.append({'hwnd': int(hwnd), 'pid': pid, 'title': title.value,
                    'rect': [rect.left, rect.top, rect.right, rect.bottom], 'readUTC': utc()})
        return True
    assert user.EnumWindows(visit, 0)
    assert len(windows) == 1, ('Expected one visible non-minimized owned MessControl standalone PIE window', windows)
    return windows[0]


def process_identity(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
    kernel.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4
    kernel.CloseHandle.argtypes = [w.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    assert handle, ('Cannot inspect task Editor PID', pid, ctypes.get_last_error())
    try:
        image = ctypes.create_unicode_buffer(32768)
        size = w.DWORD(len(image))
        assert kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(size))
        assert Path(image.value).name.lower() == 'unrealeditor.exe', image.value
        created, exited, cpu_kernel, cpu_user = [w.FILETIME() for unused in range(4)]
        assert kernel.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(exited), ctypes.byref(cpu_kernel), ctypes.byref(cpu_user))
        return {'pid': pid, 'image': image.value,
            'creationFiletime': (created.dwHighDateTime << 32) | created.dwLowDateTime, 'readUTC': utc()}
    finally:
        kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='Explicit task-owned UnrealEditor PID')
    parser.add_argument('--output', required=True, help='Fresh directory on E:; no overwrite')
    args = parser.parse_args()
    assert os.name == 'nt' and args.pid > 0
    out = Path(args.output).resolve()
    assert out.drive.upper() == 'E:', 'All outputs must be on E:'
    assert not out.exists() or (out.is_dir() and not any(out.iterdir())), 'Output must be fresh/empty'
    out.mkdir(parents=True, exist_ok=True)
    task_temp = out / 'Temp'
    task_temp.mkdir()
    env = dict(os.environ, TEMP=str(task_temp), TMP=str(task_temp), PYTHONDONTWRITEBYTECODE='1', PYTHONIOENCODING='utf-8')
    os.environ.update(TEMP=str(task_temp), TMP=str(task_temp))
    project = Path(__file__).resolve().parents[3]
    qa = Path(__file__).with_name('check_unified_catch_solo.py').resolve()
    runner = Path(__file__).with_name('run_owned_ui_recipe.py').resolve()
    ffmpeg = Path('C:/ffmpeg/ffmpeg.exe')  # Read-only existing installation; all outputs are E:.
    assert ffmpeg.is_file() and runner.is_file() and qa.is_file()
    source = qa.read_text(encoding='utf-8-sig')
    assert 'MC_UNIFIED_CATCH_CAPTURE_PNG' in source and 'MC_UNIFIED_CATCH_OUTPUT' in source, 'QA options must be implemented by its owner before run'
    token = uuid.uuid4().hex
    wrapper = out / 'RunCatchOwned.py'
    cancel = out / 'CancelCatchOwned.py'
    qa_hash = sha(qa)
    wrapper.write_text("import os,hashlib,builtins\nfrom pathlib import Path\n"
        + "assert os.getpid()==" + repr(args.pid) + ", 'Wrong task Editor PID'\n"
        + "_qa_path=" + repr(qa.as_posix()) + "\n"
        + "assert hashlib.sha256(Path(_qa_path).read_bytes()).hexdigest()==" + repr(qa_hash) + "\n"
        + "_qa_scope={'__name__':'__main__','__file__':_qa_path,'MC_UNIFIED_CATCH_CAPTURE_PNG':False,'MC_UNIFIED_CATCH_OUTPUT':" + repr(out.as_posix()) + "}\n"
        + "exec(compile(Path(_qa_path).read_text(encoding='utf-8-sig'),_qa_path,'exec'),_qa_scope,_qa_scope)\n"
        + "_owner=getattr(builtins,'mc_unified_catch_solo',None)\n"
        + "if _owner is _qa_scope.get('probe') and _owner is not None: _owner._mc_wgc_owner_token=" + repr(token) + "\n",
        encoding='utf-8')
    cancel.write_text("import os,builtins\nassert os.getpid()==" + repr(args.pid) + "\n"
        + "_owner=getattr(builtins,'mc_unified_catch_solo',None)\n"
        + "if _owner is not None and getattr(_owner,'_mc_wgc_owner_token',None)==" + repr(token) + ": _owner.cancel()\n",
        encoding='utf-8')
    progress = out / 'Capture.progress.log'
    video = out / 'Catch.mp4'
    report_path = out / 'CaptureReport.json'
    report = {'passed': False, 'startedUTC': utc(), 'method': 'Windows Graphics Capture explicit owned PIE HWND',
        'audio': False, 'captureFPS': 30, 'desktopCapture': False, 'activationOrInput': False,
        'PIEStartStop': False, 'sourceHashes': {'host': sha(__file__), 'qa': qa_hash, 'runner': sha(runner), 'wrapper': sha(wrapper)},
        'referenceWorkflow': 'E:/CodexWork/MessControl/Validation/VFXBeginnerControls_20261030/Record.py'}
    capture = probe_process = None
    probe_done = False
    started = time.monotonic()
    def stamp(name):
        report[name] = {'UTC': utc(), 'hostSeconds': time.monotonic() - started}
    def write():
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    try:
        report['editorProcess'] = process_identity(args.pid)
        report['window'] = window = owned_window(args.pid)
        command = [str(ffmpeg), '-hide_banner', '-n', '-stats_period', '0.1', '-f', 'lavfi', '-i',
            f"gfxcapture=hwnd={window['hwnd']}:max_framerate=30:capture_cursor=0:capture_border=0:display_border=0",
            '-t', '100', '-an', '-vf', 'hwdownload,format=bgra,pad=ceil(iw/2)*2:ceil(ih/2)*2',
            '-c:v', 'libx264', '-preset', 'veryfast', '-tune', 'zerolatency', '-threads', '4', '-crf', '18', '-bf', '0',
            '-pix_fmt', 'yuv420p', '-r', '30', '-fps_mode', 'cfr', '-movflags', '+faststart',
            '-progress', str(progress), '-nostats', str(video)]
        report['captureCommand'] = command
        with (out / 'Capture.ffmpeg.log').open('w', encoding='utf-8') as log:
            capture = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                env=env, cwd=out, creationflags=subprocess.CREATE_NO_WINDOW)
            report['capturePID'] = capture.pid
            stamp('captureStarted');write()
            first_deadline = time.monotonic() + 8
            while frame_count(progress) < 1:
                assert capture.poll() is None, 'ffmpeg exited before first frame'
                assert time.monotonic() < first_deadline, 'WGC first frame timeout (8s)'
                time.sleep(.05)
            stamp('firstFrame');write()
            assert owned_window(args.pid)['hwnd'] == window['hwnd'], 'Owned PIE window changed before QA'
            probe_command = [sys.executable, '-B', str(runner), str(wrapper)]
            report['probeCommand'] = probe_command
            with (out / 'ProbeRunner.log').open('w', encoding='utf-8') as probe_log:
                probe_process = subprocess.Popen(probe_command, stdout=probe_log, stderr=subprocess.STDOUT,
                    env=env, cwd=project, creationflags=subprocess.CREATE_NO_WINDOW)
                report['probePID'] = probe_process.pid
                stamp('probeStarted');write()
                print('CAPTURE_STARTED ' + json.dumps({'window': window, 'capturePID': capture.pid, 'probePID': probe_process.pid, 'output': str(out)}), flush=True)
                deadline = time.monotonic() + 75
                while True:
                    result = read_json(out / 'SoloReport.json')
                    if result and (result.get('phase') == 'done' or result.get('error')):
                        probe_done = True
                        report['soloResult'] = {'passed': bool(result.get('passed')), 'phase': result.get('phase'), 'error': result.get('error'), 'castCount': result.get('castCount')}
                        stamp('probeReportedDone')
                        break
                    assert capture.poll() is None, 'ffmpeg exited before QA finished'
                    assert probe_process.poll() in (None, 0), 'Owned recipe runner failed; see ProbeRunner.log'
                    assert time.monotonic() < deadline, 'Actual catch QA did not finish within75s'
                    time.sleep(.1)
                # Retain at least one frame after the actual done/cleanup report.
                count_before = frame_count(progress)
                completion_deadline = time.monotonic() + 2
                while capture.poll() is None and frame_count(progress) <= count_before and time.monotonic() < completion_deadline:
                    time.sleep(.05)
                report['completionFrameCaptured'] = frame_count(progress) > count_before
                stamp('completionFrame')
                report['passed'] = bool(report['soloResult']['passed'] and report['completionFrameCaptured'])
    except Exception:
        report['error'] = traceback.format_exc()
    finally:
        # q flushes/finalizes this host's encoder only; never control the Editor/PIE.
        if capture is not None:
            if capture.poll() is None:
                try:
                    capture.stdin.write(b'q\n');capture.stdin.flush();capture.wait(timeout=10)
                except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
                    capture.terminate()
                    try: capture.wait(timeout=5)
                    except subprocess.TimeoutExpired: capture.kill();capture.wait(timeout=5)
            report['captureExitCode'] = capture.returncode
            report['capturedFrames'] = frame_count(progress)
            if capture.returncode != 0:
                report['passed'] = False
        if not probe_done and probe_process is not None:
            # Cancel only the Python callback tagged by this wrapper; keep any native active retrieval.
            cleanup_command = [sys.executable, '-B', str(runner), str(cancel)]
            report['cleanupCommand'] = cleanup_command
            try:
                with (out / 'CancelRunner.log').open('w', encoding='utf-8') as cleanup_log:
                    cleanup = subprocess.Popen(cleanup_command, stdout=cleanup_log, stderr=subprocess.STDOUT,
                        env=env, cwd=project, creationflags=subprocess.CREATE_NO_WINDOW)
                    try: report['cancelExitCode'] = cleanup.wait(timeout=12)
                    except subprocess.TimeoutExpired:
                        cleanup.terminate();cleanup.wait(timeout=5);report['cancelTimedOut'] = True
            except Exception:
                report['cancelError'] = traceback.format_exc()
        if probe_process is not None:
            try: report['probeRunnerExitCode'] = probe_process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                probe_process.terminate();probe_process.wait(timeout=5);report['probeRunnerTimedOut'] = True;report['passed'] = False
        if video.is_file() and video.stat().st_size:
            completion = out / 'Completion.png'
            extract_command = [str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-nostdin', '-n', '-sseof', '-0.10',
                '-i', str(video), '-frames:v', '1', str(completion)]
            report['completionCommand'] = extract_command
            try:
                with (out / 'Completion.ffmpeg.log').open('w', encoding='utf-8') as extract_log:
                    extraction = subprocess.run(extract_command, stdout=extract_log, stderr=subprocess.STDOUT,
                        env=env, cwd=out, creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
                report['completionExitCode'] = extraction.returncode
                report['completionPNG'] = str(completion) if completion.is_file() else None
                report['video'] = {'path': str(video), 'bytes': video.stat().st_size}
            except Exception:
                report['completionError'] = traceback.format_exc()
        stamp('finished')
        write()
        print('CAPTURE_FINISHED ' + json.dumps({'passed': report['passed'], 'report': str(report_path), 'video': str(video)}), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    raise SystemExit(main())
