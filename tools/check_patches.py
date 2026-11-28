"""Check that every ZynAddSubFX patch in a folder loads and plays.

Runs build/msvc/src/Tests/Release/PatchLoadCheck.exe once per file (one
process per file, so a crash or hang in one patch is reported, not fatal),
in parallel, and writes a tab-separated report.

Usage: python tools/check_patches.py <folder> [report.tsv]
"""
import concurrent.futures as cf
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "msvc", "src", "Tests", "Release", "PatchLoadCheck.exe")
EXTS = {".xiz", ".xmz", ".xsz", ".xpz"}
TIMEOUT = 180


def check(path):
    t = time.time()
    try:
        p = subprocess.run([EXE, path], capture_output=True, text=True,
                           errors="replace", timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return path, "HANG", f"no result after {TIMEOUT}s", time.time() - t
    lines = [l for l in p.stdout.splitlines() if l.startswith(("OK\t", "WARN\t", "FAIL\t", "SKIP\t"))]
    if p.returncode not in (0, 1, 2) or not lines:
        code = p.returncode & 0xFFFFFFFF
        err = (p.stderr.strip().splitlines() or [""])[-1]
        return path, "CRASH", f"exit code 0x{code:08X} {err}".strip(), time.time() - t
    status, rest = lines[-1].split("\t", 1)
    return path, status, rest, time.time() - t


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 64
    folder = sys.argv[1]
    report = sys.argv[2] if len(sys.argv) > 2 else "patch-check.tsv"
    if not os.path.exists(EXE):
        print("Build PatchLoadCheck first:", EXE)
        return 1
    files = sorted(os.path.join(d, f) for d, _, fs in os.walk(folder) for f in fs
                   if os.path.splitext(f)[1].lower() in EXTS)
    print(f"{len(files)} patch files under {folder}")
    results = []
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for i, r in enumerate(pool.map(check, files), 1):
            results.append(r)
            if r[1] not in ("OK",):
                print(f"{r[1]}: {os.path.relpath(r[0], folder)}  {r[2]}")
            if i % 200 == 0:
                print(f"  {i}/{len(files)} done")
    with open(report, "w", encoding="utf-8") as f:
        f.write("status\tfile\tdetails\tseconds\n")
        for path, status, rest, secs in results:
            f.write(f"{status}\t{os.path.relpath(path, folder)}\t{rest}\t{secs:.1f}\n")
    counts = {}
    for r in results:
        counts[r[1]] = counts.get(r[1], 0) + 1
    print("Summary:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    print("Report:", os.path.abspath(report))
    return 0 if set(counts) <= {"OK", "WARN", "SKIP"} else 1


if __name__ == "__main__":
    sys.exit(main())
