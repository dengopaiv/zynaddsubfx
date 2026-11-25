"""Make the release zip of the accessible Windows edition.

Usage: python tools/package.py <build output folder> <dist folder>
  e.g. python tools/package.py build/msvc/src/Release dist

The zip holds zynaddsubfx.exe, the DLLs next to it, the instrument banks,
the manual, the licence and the authors. It needs nothing installed.
"""
import glob
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def version():
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    parts = [re.search(r'set\(VERSION_%s "(\d+)"\)' % k, text).group(1)
             for k in ('MAJOR', 'MINOR', 'REVISION')]
    return '.'.join(parts)


def vc_runtime():
    """The Visual C++ runtime DLLs, shipped next to the program so that it
    runs on a Windows without the redistributable installed"""
    roots = [os.environ.get('VCToolsRedistDir', '')]
    for pf in (os.environ.get('ProgramFiles', r'C:\Program Files'),
               os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')):
        roots += glob.glob(os.path.join(pf, 'Microsoft Visual Studio', '*', '*',
                                        'VC', 'Redist', 'MSVC', '*'))
    for root in sorted((r for r in roots if r), reverse=True):
        crt = glob.glob(os.path.join(root, 'x64', 'Microsoft.VC*.CRT'))
        if crt:
            return [os.path.join(crt[0], f) for f in os.listdir(crt[0])
                    if f.lower().endswith('.dll')]
    print('warning: Visual C++ runtime DLLs not found; not included')
    return []


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dist = sys.argv[1], sys.argv[2]
    name = 'ZynAddSubFX-Accessible-%s-win64' % version()
    stage = os.path.join(dist, name)
    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(stage)

    for f in os.listdir(src):
        if f.lower().endswith(('.exe', '.dll')):
            shutil.copy2(os.path.join(src, f), stage)
    for dll in vc_runtime():
        shutil.copy2(dll, stage)
    banks = os.path.join(src, 'banks')
    if not os.path.isdir(banks):
        banks = os.path.join(ROOT, 'instruments', 'banks')
    shutil.copytree(banks, os.path.join(stage, 'banks'))
    for doc, target in (('doc/accessible/MANUAL.txt', 'MANUAL.txt'),
                        ('COPYING', 'COPYING.txt'),
                        ('AUTHORS.txt', 'AUTHORS.txt')):
        shutil.copy2(os.path.join(ROOT, doc), os.path.join(stage, target))

    zpath = os.path.join(dist, name + '.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for base, _, files in os.walk(stage):
            for f in files:
                full = os.path.join(base, f)
                z.write(full, os.path.join(name, os.path.relpath(full, stage)))
    print(zpath)


if __name__ == '__main__':
    main()
