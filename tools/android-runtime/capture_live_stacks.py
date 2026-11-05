"""Briefly attach NDK LLDB to the debuggable game and retain local symbols."""
import argparse
import os
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--label', required=True)
args = p.parse_args()
if not args.label.replace('-', '').replace('_', '').isalnum():
    raise SystemExit('Use a simple label for the diagnostic files')
root = Path(__file__).resolve().parents[2]
out = root / 'build/android-game'
llvm = Path(str(Path.home() / 'Android/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64'))
adb = [str(Path.home() / 'Android/platform-tools/adb.exe'), '-s', args.serial]
package = 'org.brokenbond.nativecheck'
pid = subprocess.check_output(adb + ['shell', 'pidof', package + ':game'], text=True).strip()
if not pid.isdigit():
    raise SystemExit('Exactly one game process is required')
maps = subprocess.check_output(adb + ['exec-out', 'run-as', package, 'cat', f'/proc/{pid}/maps'], text=True)
(out / f'{args.label}-maps.txt').write_text(maps)
local = [root / 'sdk/out/android-arm64/librexruntime.so',
         root / 'build/android-runtime/libbroken_bond_game.so',
         root / 'build/android-runtime/libbroken_bond_guest.so']
bases = {}
for line in maps.splitlines():
    fields = line.split(None, 5)
    if len(fields) < 6:
        continue
    for library in local:
        if fields[5].endswith('/' + library.name):
            base = int(fields[0].split('-')[0], 16) - int(fields[2], 16)
            bases[library.name] = min(base, bases.get(library.name, base))
if len(bases) != len(local):
    raise SystemExit('Could not identify every original-engine library in the live process')
subprocess.run(adb + ['forward', 'tcp:5039', 'localabstract:bb-native-debug'], check=True)
env = os.environ.copy()
env['PATH'] = str(llvm / 'python3') + ';' + str(llvm / 'bin') + ';' + env['PATH']
env['PYTHONHOME'] = str(llvm / 'python3')
commands = ['platform select remote-android',
            'settings set target.preload-symbols false',
            'settings set plugin.process.gdb-remote.packet-timeout 3',
            f'target create --arch aarch64 "{local[1].as_posix()}"']
for library in local:
    if library != local[1]:
        commands += [f'target modules add "{library.as_posix()}"']
commands += ['gdb-remote 127.0.0.1:5039']
for library in local:
    commands += [f'target modules load -f "{library.name}" -s {bases[library.name]:#x}']
commands += ['thread backtrace all -c 14', 'process detach']
command = [str(llvm / 'bin/lldb.exe'), '--no-lldbinit', '--batch']
for value in commands:
    command += ['-o', value]
with (out / f'{args.label}-server.log').open('w') as server_log, (out / f'{args.label}-stacks.txt').open('w') as log:
    server = subprocess.Popen(adb + ['shell', 'run-as', package, 'files/lldb-server', 'gdbserver',
                                     'unix-abstract://bb-native-debug', '--attach', pid],
                              stdout=server_log, stderr=subprocess.STDOUT)
    time.sleep(0.3)
    client = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        result = client.wait(timeout=18)
    except subprocess.TimeoutExpired:
        client.terminate()
        client.wait()
        result = 124
    try:
        server.wait(timeout=3)
    except subprocess.TimeoutExpired:
        subprocess.run(adb + ['shell', 'run-as', package, 'killall', 'lldb-server'], capture_output=True)
        server.terminate()
if result:
    raise SystemExit(f'LLDB returned {result}; check {args.label}-stacks.txt. The diagnostic process may have exited.')
print(out / f'{args.label}-stacks.txt')
