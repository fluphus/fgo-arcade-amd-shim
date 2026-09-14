"""Trigger bounded existing stack samples from live aggregate counter rows."""
import argparse
import collections
import datetime as dt
import json
from pathlib import Path
import subprocess
import time


def main(args):
    manifest = json.loads((args.capture / 'manifest.json').read_text(encoding='utf-8-sig'))
    deadline = dt.datetime.fromisoformat(manifest['expected_end']).timestamp()
    rows = collections.deque(maxlen=5)
    child = None
    child_log = None
    next_sample = 0
    samples = 0
    for _ in range(50):
        if (args.capture / 'interval_samples.jsonl').exists():
            break
        time.sleep(0.1)
    with (args.capture / 'interval_samples.jsonl').open(encoding='utf-8') as source, \
            (args.capture / 'stack_events.jsonl').open('w', encoding='utf-8', buffering=1) as events:
        def event(kind, **fields):
            events.write(json.dumps(dict(type=kind, time=dt.datetime.now().astimezone().isoformat(),
                                         **fields), separators=(',', ':')) + '\n')

        event('armed', game_pid=manifest['game_pid'], deadline=manifest['expected_end'])
        last_data = time.monotonic()
        while time.time() < deadline and not (args.capture / 'stop_stacks').exists() and not (args.capture / 'stop_capture').exists():
            if child is not None and child.poll() is not None:
                event('finished', sample=samples, exit_code=child.returncode)
                child_log.close()
                child = child_log = None
                next_sample = time.monotonic() + 20
            lines = []
            while True:
                position = source.tell()
                line = source.readline()
                if not line:
                    break
                if not line.endswith('\n'):
                    source.seek(position)
                    break
                lines.append(line)
            if lines:
                last_data = time.monotonic()
                rows.extend(json.loads(line) for line in lines)
            if time.monotonic() - last_data > 3:
                break
            if lines and len(rows) == 5 and child is None and samples < 12 and time.monotonic() >= next_sample:
                span = rows[-1]['elapsed_s'] - rows[0]['elapsed_s']
                window = list(rows)[1:]
                frames = sum(row['frames'] for row in window)
                counters = collections.Counter()
                for row in window:
                    counters.update(row['counters'])
                fps = frames / span if span > 0 else 0
                if frames > 0 and 0 < fps < 58 and counters['g_perf_rs_draws'] / frames > 400:
                    samples += 1
                    output = args.capture / f'stacks_{samples:02d}.jsonl'
                    child_log = output.with_suffix('.log').open('w', encoding='utf-8')
                    event('trigger', sample=samples, counter_time=rows[-1]['time'],
                          fps=fps, frames=frames, span_s=span, counters=dict(counters), output=str(output))
                    child = subprocess.Popen([str(args.sampler), str(manifest['game_pid']), '8',
                                              output.name, args.program_address],
                                             cwd=args.capture,
                                             stdout=child_log, stderr=subprocess.STDOUT,
                                             creationflags=subprocess.CREATE_NO_WINDOW)
            time.sleep(0.1)
        # Let an active bounded sample resume its threads and close its output.
        if child is not None:
            event('finished', sample=samples, exit_code=child.wait())
            child_log.close()
        event('stopped', samples=samples)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--sampler', type=Path, required=True)
    parser.add_argument('--program-address', required=True)
    main(parser.parse_args())
