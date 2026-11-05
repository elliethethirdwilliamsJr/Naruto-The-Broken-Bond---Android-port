"""Verify driver cache reuse and corrupt-cache recovery with original shaders."""
import argparse
import json
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / 'build/android-game'
adb = [str(Path.home() / 'Android/platform-tools/adb.exe'), '-s', args.serial]
remote = '/data/local/tmp/broken_bond_renderer_probe'
cache = remote + '/bb-driver-cache-test.bin'
for source in (root / 'build/android-runtime/android_original_shader_probe', root / 'sdk/out/android-arm64/librexruntime.so'):
    subprocess.run(adb + ['push', str(source), remote + '/' + source.name], check=True)
subprocess.run(adb + ['shell', 'chmod', '700', remote + '/android_original_shader_probe'], check=True)
subprocess.run(adb + ['shell', 'rm', '-f', cache], check=True)
records = []
for label in ('cold', 'warm', 'corrupt'):
    if label == 'corrupt':
        fixture = out / 'driver-cache-corrupt-fixture.bin'
        fixture.write_bytes(b'bad')
        subprocess.run(adb + ['push', str(fixture), cache], check=True)
    command = (f'cd {remote} && LD_LIBRARY_PATH=. REX_ANDROID_PIPELINE_CACHE={cache} '
               f'REX_ANDROID_PIPELINE_NO_OPTIMIZE=0 timeout 90 ./android_original_shader_probe cache-{label}.jsonl')
    started = time.perf_counter()
    with (out / f'driver-cache-{label}.log').open('wb') as log:
        subprocess.run(adb + ['shell', command], stdout=log, stderr=subprocess.STDOUT, check=True)
    elapsed = time.perf_counter() - started
    file = out / f'driver-cache-{label}.jsonl'
    subprocess.run(adb + ['pull', remote + f'/cache-{label}.jsonl', str(file)], check=True)
    rows = [json.loads(line) for line in file.read_text().splitlines()]
    assert rows[-1]['passed'] and rows[-1]['comparisons'] == 16
    cache_info = rows[0]['evidence']['pipeline_cache']
    assert cache_info['enabled']
    assert (cache_info['loaded_bytes'] > 0) == (label == 'warm')
    records.append(dict(case=label, wall_seconds=elapsed, cache=cache_info, comparisons=16, passed=True))
    print(json.dumps(records[-1]), flush=True)
report = dict(passed=True, records=records, cold_to_warm_ratio=records[0]['wall_seconds'] / records[1]['wall_seconds'])
(out / 'driver-cache-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
