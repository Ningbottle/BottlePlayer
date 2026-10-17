"""Real separate DLL processes must not both own one Native profile."""
from pathlib import Path
import ctypes
import os
import subprocess
import sys
import tempfile

def open_runtime(dll):
    root = Path(__file__).resolve().parents[2]
    dependency_directory = os.add_dll_directory(str(root/'native/vcpkg_installed/x64-windows/bin'))
    runtime = ctypes.CDLL(str(Path(dll).resolve()))
    runtime.EchoInitializeWithPathsV2.argtypes = [ctypes.c_char_p]
    runtime.EchoInitializeWithPathsV2.restype = ctypes.c_int
    runtime.EchoShutdown.restype = ctypes.c_int
    return runtime, dependency_directory

if len(sys.argv) > 2:
    dll, profile, mode = sys.argv[1:]
    runtime, dependency_directory = open_runtime(dll)
    result = runtime.EchoInitializeWithPathsV2(profile.encode('utf-8'))
    print(result, flush=True)
    if result == 0:
        if mode == 'hold': sys.stdin.readline()
        assert runtime.EchoShutdown() == 0
else:
    dll = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='bm-writer-') as profile:
        command = [sys.executable, __file__, dll, profile]
        first = subprocess.Popen([*command, 'hold'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert first.stdout.readline().strip() == '0', 'first owner must initialize'
            blocked = subprocess.run([*command, 'once'], capture_output=True, text=True, timeout=20, check=True)
            assert blocked.stdout.strip() == '1', 'second process must be refused'
            first.communicate('\n', timeout=20)
            assert first.returncode == 0
            reopened = subprocess.run([*command, 'once'], capture_output=True, text=True, timeout=20, check=True)
            assert reopened.stdout.strip() == '0', 'writer lease must be released on clean shutdown'
        finally:
            if first.poll() is None:
                first.kill()
                first.communicate()
    print('cross-process profile writer exclusion and clean reacquisition passed')
