"""Run a command on a hidden Windows desktop and print its output.

UI tests (tools/uia_check.py and the like) open the zynaddsubfx window,
move focus and send keys. Run on a desktop of their own, nothing appears
on the user's screen and nothing takes the user's keyboard focus.

Usage: python tools/hidden_desktop.py <command> [args ...]
Example:
  python tools/hidden_desktop.py python tools/uia_check.py build\\msvc\\src\\Release\\zynaddsubfx.exe

The command, and every program it starts, runs on the desktop
"zyn-hidden" (created if needed). Its exit status is returned.
"""
import ctypes
import msvcrt
import os
import subprocess
import sys
import tempfile
from ctypes import wintypes

DESKTOP = 'zyn-hidden'
GENERIC_ALL = 0x10000000
CREATE_NO_WINDOW = 0x08000000
CREATE_UNICODE_ENVIRONMENT = 0x00000400
STARTF_USESTDHANDLES = 0x00000100
INFINITE = 0xFFFFFFFF

user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('lpReserved', wintypes.LPWSTR),
                ('lpDesktop', wintypes.LPWSTR), ('lpTitle', wintypes.LPWSTR),
                ('dwX', wintypes.DWORD), ('dwY', wintypes.DWORD),
                ('dwXSize', wintypes.DWORD), ('dwYSize', wintypes.DWORD),
                ('dwXCountChars', wintypes.DWORD),
                ('dwYCountChars', wintypes.DWORD),
                ('dwFillAttribute', wintypes.DWORD),
                ('dwFlags', wintypes.DWORD), ('wShowWindow', wintypes.WORD),
                ('cbReserved2', wintypes.WORD),
                ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', wintypes.HANDLE),
                ('hStdOutput', wintypes.HANDLE),
                ('hStdError', wintypes.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', wintypes.HANDLE), ('hThread', wintypes.HANDLE),
                ('dwProcessId', wintypes.DWORD),
                ('dwThreadId', wintypes.DWORD)]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    user32.CreateDesktopW.restype = wintypes.HANDLE
    desk = user32.CreateDesktopW(DESKTOP, None, None, 0, GENERIC_ALL, None)
    if not desk:
        raise SystemExit('CreateDesktop failed: %d' % ctypes.get_last_error())

    out = tempfile.NamedTemporaryFile(delete=False, suffix='.txt')
    out.close()
    #started directly, not through cmd: arguments such as "Master>Tuning"
    #stay as they are
    argv = list(sys.argv[1:])
    if argv[0].lower() in ('python', 'python.exe', 'py'):
        argv[0] = sys.executable
    full = subprocess.list2cmdline(argv)

    log = open(out.name, 'wb')
    handle = msvcrt.get_osfhandle(log.fileno())
    os.set_handle_inheritable(handle, True)

    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = DESKTOP
    si.dwFlags = STARTF_USESTDHANDLES
    si.hStdOutput = si.hStdError = handle
    pi = PROCESS_INFORMATION()
    env = dict(os.environ, PYTHONUNBUFFERED='1')
    block = ''.join('%s=%s\0' % kv for kv in env.items()) + '\0'
    ok = kernel32.CreateProcessW(None, ctypes.create_unicode_buffer(full),
                                 None, None, True,
                                 CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                 ctypes.create_unicode_buffer(block),
                                 os.getcwd(), ctypes.byref(si),
                                 ctypes.byref(pi))
    log.close()
    if not ok:
        raise SystemExit('CreateProcess failed: %d' % ctypes.get_last_error())
    #pass the output on while the command runs
    pos = 0
    while True:
        done = kernel32.WaitForSingleObject(pi.hProcess, 500) == 0
        with open(out.name, 'rb') as f:
            f.seek(pos)
            chunk = f.read()
        if chunk:
            pos += len(chunk)
            sys.stdout.write(chunk.decode('utf-8', 'replace'))
            sys.stdout.flush()
        if done:
            break
    code = wintypes.DWORD()
    kernel32.GetExitCodeProcess(pi.hProcess, ctypes.byref(code))
    kernel32.CloseHandle(pi.hThread)
    kernel32.CloseHandle(pi.hProcess)
    user32.CloseDesktop(desk)

    os.unlink(out.name)
    sys.exit(code.value)


if __name__ == '__main__':
    main()
