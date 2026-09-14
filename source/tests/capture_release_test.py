"""Exercise packaged attach/stop/analyze against an isolated fake game."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def run(*args):
    result = subprocess.run(args, text=True, encoding='utf-8', errors='replace', capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = args.source.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
app = output / 'game space [test] & $() \u6d4b\u8bd5'
app.mkdir()
fixture = Path(__file__).with_name('capture_release_fixture.c')
run('x86_64-w64-mingw32-gcc.exe', '-O2', '-shared', '-DFIXTURE_DLL', str(fixture), '-o', str(app / 'opengl32.dll'))
run('x86_64-w64-mingw32-gcc.exe', '-O2', str(fixture), '-o', str(app / 'ago.exe'))
capture = output / 'capture space [test] & $() \u65e5\u672c\u8a9e'
game = subprocess.Popen([str(app / 'ago.exe')], cwd=app, creationflags=subprocess.CREATE_NO_WINDOW)
powershell = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File']
try:
    time.sleep(0.2)
    run(*powershell, str(root / 'tools/start-capture.ps1'), '-GameDirectory', str(app),
        '-Seconds', '600', '-OutputDirectory', str(capture))
    deadline = time.monotonic() + 15
    events = []
    while time.monotonic() < deadline:
        path = capture / 'stack_events.jsonl'
        if path.exists():
            events = [json.loads(line) for line in path.read_text().splitlines() if line.endswith('}')]
        if any(event['type'] == 'trigger' for event in events):
            break
        time.sleep(0.2)
    assert any(event['type'] == 'trigger' for event in events), 'No low-FPS stack trigger'
    assert (capture / 'interval_samples.jsonl').stat().st_size > 0, 'No live output'
    run(*powershell, str(root / 'tools/stop-capture.ps1'), '-CaptureDirectory', str(capture))
    assert game.poll() is None, 'Stop command terminated the fixture game'
    events = [json.loads(line) for line in (capture / 'stack_events.jsonl').read_text().splitlines()]
    assert events[-1]['type'] == 'stopped'
    assert any(event['type'] == 'finished' and event['exit_code'] == 0 for event in events)
    run('python.exe', str(root / 'tools/analyze-capture.py'), str(capture))
    analysis = json.loads((capture / 'analysis.json').read_text())
    assert analysis['stacks'] and analysis['one_second']
    report = dict(passed=True, continuous_rows=True, lowfps_trigger=True,
                  manual_stop_retains_completed_stack=True, game_left_running=True,
                  unicode_and_metacharacter_paths=True, analyzer_passed=True,
                  native_game_used=False)
    (output / 'capture-validation.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))
finally:
    if (capture / 'manifest.json').exists():
        subprocess.run([*powershell, str(root / 'tools/stop-capture.ps1'), '-CaptureDirectory', str(capture)],
                       capture_output=True)
    # Only this fixture process is terminated, after its observers stop.
    if game.poll() is None:
        game.terminate()
    game.wait()
