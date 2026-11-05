from pathlib import Path
"""Restart the development APK with one-shot diagnostic options via its launcher."""
import argparse
import re
import subprocess
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--period', type=int, default=120)
p.add_argument('--fast-compile', action='store_true')
args = p.parse_args()
if not 0 <= args.period <= 3600:
    p.error('period must be between 0 and 3600')
adb = [str(Path.home() / 'Android/platform-tools/adb.exe'), '-s', args.serial]
package = 'org.brokenbond.nativecheck'
subprocess.run(adb + ['shell', 'am', 'force-stop', package], check=True)
subprocess.run(adb + ['shell', 'am', 'start', '-W', '-n', package + '/.MainActivity',
                      '--ei', 'game_capture_period', str(args.period),
                      '--ez', 'game_fast_compile', str(args.fast_compile).lower()], check=True)
ui = '/data/local/tmp/brokenbond-apk-ui.xml'
for attempt in range(7):
    running = subprocess.run(adb + ['shell', 'pidof', package + ':game'], capture_output=True, text=True)
    if running.returncode == 0:
        print('Game already launched:', running.stdout.strip(), flush=True)
        raise SystemExit(0)
    subprocess.run(adb + ['shell', 'uiautomator', 'dump', ui], stdout=subprocess.DEVNULL, check=True)
    nodes = list(ET.fromstring(subprocess.check_output(adb + ['shell', 'cat', ui])).iter('node'))
    left, top, right, bottom = map(int, re.findall(r'\d+', nodes[0].get('bounds')))
    for node in nodes:
        if node.get('text') != 'Launch original game':
            continue
        x1, y1, x2, y2 = map(int, re.findall(r'\d+', node.get('bounds')))
        if y2 - y1 >= 50:
            print('Launch:', node.get('bounds'), flush=True)
            subprocess.run(adb + ['shell', 'input', 'tap', str((x1 + x2) // 2), str((y1 + y2) // 2)], check=True)
            raise SystemExit(0)
    subprocess.run(adb + ['shell', 'input', 'swipe', str((left + right) // 2), str(int(bottom * .85)),
                          str((left + right) // 2), str(int(bottom * .3)), '350'], check=True)
raise SystemExit('Could not find the game launch button')
