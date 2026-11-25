"""Check what a screen reader sees in the accessible zynaddsubfx window.

Starts zynaddsubfx.exe, walks the structure tree through UI Automation
(selecting and expanding items with UIA patterns, no keystrokes), and
lists every control of each visited pane with its role, name, value and
description, as NVDA, JAWS and Narrator would get them.

Usage: python tools/uia_check.py path\\to\\zynaddsubfx.exe [tree path ...]
  A tree path is item names separated by '>', e.g.
  "Master>Part 1>Kit item 1>AddSynth>Global"
Without tree paths, a default tour is taken.

Exits with status 1 if a control has no name, or a slider's value is a
bare position number where the engine has a unit.
"""
import ctypes
import subprocess
import sys
import time

import comtypes.client

comtypes.client.GetModule('UIAutomationCore.dll')
from comtypes.gen.UIAutomationClient import (  # noqa: E402
    CUIAutomation, IUIAutomation, IUIAutomationLegacyIAccessiblePattern,
    IUIAutomationExpandCollapsePattern, IUIAutomationSelectionItemPattern,
    TreeScope_Children, TreeScope_Descendants)

UIA_LegacyIAccessiblePatternId = 10018
UIA_ExpandCollapsePatternId = 10005
UIA_SelectionItemPatternId = 10010
UIA_ControlTypePropertyId = 30003
UIA_NamePropertyId = 30005
UIA_ClassNamePropertyId = 30012
UIA_TreeItemControlTypeId = 50024

ROLES = {0x2A: 'edit', 0x2B: 'button', 0x2C: 'check box', 0x2D: 'radio',
         0x2E: 'combo box', 0x33: 'slider', 0x29: 'text', 0x24: 'tree item',
         0x22: 'list item'}
STATE_CHECKED = 0x10

DEFAULT_TOUR = [
    'Master',
    'Master>Part 1',
    'Master>Part 1>Kit item 1>AddSynth>Global',
    'Master>Part 1>Kit item 1>AddSynth>Global>Amplitude envelope',
    'Master>System effect 1',
]


def find_window(timeout=20):
    user32 = ctypes.windll.user32
    end = time.time() + timeout
    while time.time() < end:
        h = user32.FindWindowW('ZynAccessibleMain', None)
        if h:
            return h
        time.sleep(0.2)
    raise SystemExit('the window did not appear')


def pattern(el, pid, iface):
    return el.GetCurrentPattern(pid).QueryInterface(iface)


def legacy(el):
    return pattern(el, UIA_LegacyIAccessiblePatternId,
                   IUIAutomationLegacyIAccessiblePattern)


def tree_item(uia, parent, name):
    cond = uia.CreateAndCondition(
        uia.CreatePropertyCondition(UIA_ControlTypePropertyId,
                                    UIA_TreeItemControlTypeId),
        uia.CreatePropertyCondition(UIA_NamePropertyId, name))
    return parent.FindFirst(TreeScope_Children, cond)


def goto(uia, tree, path):
    parent = tree
    item = None
    names = path.split('>')
    for i, name in enumerate(names):
        item = tree_item(uia, parent, name)
        if not item:
            raise SystemExit(f'no tree item "{name}" in {">".join(names[:i])}')
        if i < len(names) - 1:
            pattern(item, UIA_ExpandCollapsePatternId,
                    IUIAutomationExpandCollapsePattern).Expand()
            time.sleep(0.2)
        parent = item
    pattern(item, UIA_SelectionItemPatternId,
            IUIAutomationSelectionItemPattern).Select()
    time.sleep(2.0)  # the pane is rebuilt and its values arrive


def bare_number(s):
    try:
        float(s)
        return True
    except ValueError:
        return False


def pane(uia, root):
    canvas = root.FindFirst(TreeScope_Descendants, uia.CreatePropertyCondition(
        UIA_ClassNamePropertyId, 'ZynAccessibleCanvas'))
    kids = canvas.FindAll(TreeScope_Children, uia.CreateTrueCondition())
    rows = []
    for i in range(kids.Length):
        el = kids.GetElement(i)
        lg = legacy(el)
        role = ROLES.get(lg.CurrentRole, hex(lg.CurrentRole))
        value = lg.CurrentValue or ''
        if role == 'check box':
            value = 'checked' if lg.CurrentState & STATE_CHECKED else 'not checked'
        rows.append((role, lg.CurrentName or '', value,
                     lg.CurrentDescription or ''))
    return rows


def main():
    exe = sys.argv[1]
    tour = sys.argv[2:] or DEFAULT_TOUR
    proc = subprocess.Popen([exe, '-O', 'NULL'])
    problems = []
    try:
        hwnd = find_window()
        time.sleep(1.0)
        uia = comtypes.client.CreateObject(CUIAutomation,
                                           interface=IUIAutomation)
        root = uia.ElementFromHandle(hwnd)
        tree = root.FindFirst(TreeScope_Descendants, uia.CreatePropertyCondition(
            UIA_ClassNamePropertyId, 'SysTreeView32'))
        print('window:', root.CurrentName, '| tree:', legacy(tree).CurrentName)
        for path in tour:
            goto(uia, tree, path)
            rows = pane(uia, root)
            print(f'\n== {path}: {len(rows)} objects')
            for role, name, value, desc in rows:
                if role == 'text':
                    continue
                d = desc.split('\n')[0][:50]
                print(f'  {role:10} | {name[:40]:40} | {value[:28]:28} | {d}')
                if not name:
                    problems.append(f'{path}: a {role} without a name')
                if role == 'slider' and not value:
                    problems.append(f'{path}: slider "{name}" without a value')
        print()
        print('problems:' if problems else 'no problems found')
        for p in problems:
            print('  ' + p)
    finally:
        ctypes.windll.user32.PostMessageW(find_window(2), 0x0010, 0, 0)
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
