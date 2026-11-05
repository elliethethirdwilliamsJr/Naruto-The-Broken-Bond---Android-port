"""Compile changed Vulkan integration objects without rerunning the shader cache.

Run from the repository with scripts/enter-portable-build.ps1 loaded. The existing
Windows development configuration supplies the exact compiler arguments. Outputs
go to build/android-clip/desktop, preserving the earlier executable and objects.
"""
from pathlib import Path
import ctypes
import json
import os
import subprocess

if os.name != 'nt':
    raise SystemExit('This check requires the configured Windows development build.')
root = Path(__file__).resolve().parents[2]
build = root / 'build/native-development'
out = root / 'build/android-clip/desktop'
out.mkdir(parents=True, exist_ok=True)
ninja = Path(str(Path.home() / 'Android/cmake/3.22.1/bin/ninja.exe'))
database = json.loads(subprocess.check_output([str(ninja), '-C', str(build), '-t', 'compdb',
                      'CXX_COMPILER__broken_bond_unscanned_Release'], text=True))
parse = ctypes.windll.shell32.CommandLineToArgvW
parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
free = ctypes.windll.kernel32.LocalFree
free.argtypes = [ctypes.c_void_p]
records = []
for name in ('backend', 'shader_contract'):
    source = root / f'src/gpu/backend/vulkan/{name}.cpp'
    entry, = [e for e in database if Path(e['file']).resolve() == source]
    count = ctypes.c_int()
    raw = parse(entry['command'], ctypes.byref(count))
    if not raw:
        raise RuntimeError('Cannot parse compiler invocation.')
    try:
        args = [raw[i] for i in range(count.value)]
    finally:
        free(raw)
    if Path(args[0]).name.lower() != 'clang++.exe' or '-c' not in args:
        raise RuntimeError('Expected a direct Clang compilation command.')
    if '-o' not in args:
        raise RuntimeError('Expected a separate Clang output argument.')
    args[args.index('-o') + 1] = str(out / f'{name}.obj')
    clean = []
    i = 0
    while i < len(args):
        if args[i] in ('-MD', '-MMD', '-MP'):
            i += 1
        elif args[i] in ('-MT', '-MF', '-MQ'):
            i += 2
        else:
            clean.append(args[i])
            i += 1
    print(f'Compiling current Vulkan {name}.cpp', flush=True)
    with (out / f'{name}.log').open('w', encoding='utf-8') as log:
        subprocess.run(clean, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
    records.append(dict(source=str(source), object=str(out / f'{name}.obj'), command=clean))
(out / 'compile-report.json').write_text(json.dumps(records, indent=2) + '\n')
print('Both current integration objects compiled; desktop shader cache and executable preserved.')
