"""Collect a private Android original-game session without changing its files."""
from pathlib import Path
import argparse
import json
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
adb = str(Path.home() / 'Android/platform-tools/adb.exe')
package = 'org.brokenbond.nativecheck'

def adb_bytes(*words):
    return subprocess.check_output([adb, '-s', args.serial, *words])

session = adb_bytes('exec-out', 'run-as', package, 'cat',
                    'files/game_boot/latest-session.txt').decode().strip()
if not session.startswith(f'/data/user/0/{package}/files/game_boot/run-'):
    raise SystemExit(f'Unexpected private session path: {session}')
out = root / 'build/android-game' / Path(session).name
out.mkdir(parents=True, exist_ok=True)
archive = out / 'session.tar'
archive.write_bytes(adb_bytes('exec-out', 'run-as', package, 'tar', '-c', '-C', session, '.'))
with tarfile.open(archive) as files:
    files.extractall(out, filter='data')
pid = (out / 'android-process.txt').read_text().strip()
if not pid.isdigit():
    raise SystemExit('Invalid recorded Android process ID')
(out / 'logcat.txt').write_bytes(adb_bytes('logcat', '-d', '-t', '3000', '--pid', pid))
(out / 'screen.png').write_bytes(adb_bytes('exec-out', 'screencap', '-p'))
phases = []
checkpoint = out / 'boot-checkpoints.jsonl'
if checkpoint.exists():
    for line in checkpoint.read_text().splitlines():
        phases.append(json.loads(line)['phase'])
events = []
frontend = out / 'frontend.jsonl'
if frontend.exists():
    for line in frontend.read_text(errors='replace').splitlines():
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            pass  # A live snapshot can end inside a record; retain raw evidence.
stops = [event for event in events if 'stop' in str(event.get('kind', ''))]
summary = dict(device=args.serial, session=session, local_evidence=str(out),
               process_id=int(pid), phases=phases, frontend_records=len(events),
               last_frontend_event=events[-1] if events else None,
               stop_records=stops[-3:])
(out / 'collection-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
