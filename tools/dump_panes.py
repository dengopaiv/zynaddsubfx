"""List every group of the accessible zynaddsubfx window and its controls.

Usage: python tools/dump_panes.py path\\to\\zynaddsubfx.exe [start ...]
  start: tree paths to begin at ("Master>Part 1"); default "Master".

Visits the groups the way tools/uia_check.py --sweep does (only the first
of repeated groups such as Voice 1, Part 1) and prints, per group, each
control's role and name as a screen reader gets them. Used to keep the
manual's chapters in step with the program. Run it through
tools/hidden_desktop.py to keep the windows off the screen.
"""
import ctypes
import subprocess
import sys
import time

import uia_check as u


def main():
    exe = sys.argv[1]
    starts = sys.argv[2:] or ['Master']
    proc = subprocess.Popen([exe, '-O', 'NULL'])
    try:
        hwnd = u.find_window()
        time.sleep(1.5)
        uia = u.comtypes.client.CreateObject(u.CUIAutomation,
                                             interface=u.IUIAutomation)
        root = uia.ElementFromHandle(hwnd)
        tree = root.FindFirst(u.TreeScope_Descendants,
                              uia.CreatePropertyCondition(
                                  u.UIA_ClassNamePropertyId, 'SysTreeView32'))
        paths = []
        for s in starts:
            u.goto(uia, tree, s)
            u.sweep_paths(uia, tree, s, paths)
        for path in paths:
            u.goto(uia, tree, path)
            rows = u.pane(uia, root)
            print(f'\n## {path}', flush=True)
            for role, name, value, desc in rows:
                if role == 'text' and not name.startswith('Section'):
                    continue
                d = desc.split('\n')[0]
                print(f'  {role} | {name} | {value} | {d}', flush=True)
    finally:
        ctypes.windll.user32.PostMessageW(u.find_window(2), 0x10, 0, 0)
        try:
            proc.wait(15)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == '__main__':
    main()
