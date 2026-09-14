import bisect
import argparse
import csv
from collections import Counter, defaultdict
from datetime import datetime
from functools import cache
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import pefile

parser = argparse.ArgumentParser(description='Export every sub-60 FPS counter bin and symbolize captured CPU stacks.')
parser.add_argument('capture', type=Path)
args = parser.parse_args()
root = args.capture.resolve()
manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8-sig'))
modules = json.loads((root / 'modules.json').read_text(encoding='utf-8-sig'))
rows = [json.loads(line) for line in (root / 'interval_samples.jsonl').read_text().splitlines()]
if not rows:
    raise SystemExit('No complete counter rows were captured.')
intervals = []
for index, row in enumerate(rows):
    seconds = row.get('window_s', row['elapsed_s'] - (rows[index - 1]['elapsed_s'] if index else 0))
    if seconds <= 0:
        continue
    intervals.append(dict(row, seconds=seconds, unix=datetime.fromisoformat(row['time']).timestamp(),
                          actual_fps=row['frames'] / seconds))


def summary(part):
    counters = Counter()
    for row in part:
        counters.update(row['counters'])
    frames = sum(row['frames'] for row in part)
    seconds = sum(row['seconds'] for row in part)
    return dict(seconds=seconds, frames=frames, fps=frames / max(seconds, 1e-9), counters=dict(counters),
                per_frame={k: v / max(frames, 1) for k, v in counters.items()},
                driver_ms=counters['g_perf_emitter_header_driver_ticks'] / rows[-1]['qpc_frequency'] * 1000)


bins = defaultdict(list)
for row in intervals:
    bins[int(row['unix'])].append(row)
timezone = datetime.fromisoformat(rows[0]['time']).tzinfo
one_second = {datetime.fromtimestamp(k, timezone).isoformat(): summary(v) for k, v in bins.items()}
print('ROWS', len(rows), rows[0]['time'], rows[-1]['time'])
with (root / 'all_sub60_bins.csv').open('w', newline='', encoding='utf-8') as output:
    writer = csv.writer(output)
    writer.writerow(('time', 'fps', 'frames', 'seconds', 'sampler_draws_per_frame',
                     'emitter_read_ms', 'upload_misses', 'ubo_fallbacks'))
    for key, value in one_second.items():
        if value['fps'] < 60:
            writer.writerow((key, value['fps'], value['frames'], value['seconds'],
                             value['per_frame'].get('g_perf_rs_draws', 0), value['driver_ms'],
                             value['counters'].get('g_perf_rs_upload_misses', 0),
                             value['counters'].get('g_perf_rs_ubo_query_fallbacks', 0)))
print('SUB60_BINS', sum(value['fps'] < 60 for value in one_second.values()),
      'SUB59_BINS', sum(value['fps'] < 59 for value in one_second.values()))

renderer = next(m for m in modules if m['FileName'].lower() == manifest['renderer'].lower())
renderer_file = root / 'captured-opengl32.dll'
if not renderer_file.exists():
    renderer_file = Path(manifest['renderer'])
if manifest.get('renderer_sha256'):
    assert hashlib.sha256(renderer_file.read_bytes()).hexdigest().upper() == manifest['renderer_sha256']
base = pefile.PE(str(renderer_file), fast_load=True).OPTIONAL_HEADER.ImageBase
code = []
for line in subprocess.check_output(['llvm-nm.exe', '--defined-only', str(renderer_file)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3 and fields[1].lower() == 't':
        code.append((int(fields[0], 16) - base, fields[2]))
code.sort()
offsets = [item[0] for item in code]


@cache
def locate(address):
    value = int(address, 16)
    for module in modules:
        rva = value - module['Base']
        if 0 <= rva < module['Size']:
            if module is renderer:
                index = bisect.bisect_right(offsets, rva) - 1
                if index >= 0:
                    return 'shim!' + code[index][1]
            return module['ModuleName'] + '+0x%x' % rva
    return address


event_file = root / 'stack_events.jsonl'
events = [json.loads(line) for line in event_file.read_text().splitlines()] if event_file.exists() else []
edges = [row['unix'] for row in intervals]
stack_result = {}
for finished in (e for e in events if e['type'] == 'finished'):
    raw = [json.loads(line) for line in (root / ('stacks_%02d.jsonl' % finished['sample'])).read_text().splitlines()]
    cpu = [e for e in raw if e['type'] == 'cpu']
    if not cpu:
        print('INCOMPLETE_STACK', finished['sample'], 'counter bins retained')
        continue
    start = datetime.fromisoformat(finished['time']).timestamp() - cpu[0]['elapsed']
    groups = defaultdict(lambda: dict(top=Counter(), shim=Counter(), chains=Counter(), programs=Counter(), holds=[]))
    for event in (e for e in raw if e['type'] == 'sample'):
        at = min(bisect.bisect_left(edges, start + event['t']), len(intervals) - 1)
        row = intervals[at]
        active = row['counters']['g_perf_rs_draws'] / max(row['frames'], 1) > 400
        band = ('active_low' if row['actual_fps'] < 58 else 'active_high') if active else 'light'
        group = groups[(band, event['tid'])]
        names = [locate(address) for address in event['frames']]
        if not names:
            continue
        group['top'][names[0]] += 1
        group['shim'].update(set(n for n in names if n.startswith('shim!')))
        group['chains'][' > '.join(names[:12])] += 1
        group['programs'][event.get('app_program')] += 1
        group['holds'].append(event['held_us'])
    result = {}
    for (band, tid), group in groups.items():
        holds = group.pop('holds')
        item = {k: v.most_common(25) for k, v in group.items()}
        item.update(samples=len(holds), held_mean_us=sum(holds)/len(holds), held_max_us=max(holds))
        result[f'{band}:{tid}'] = item
        print('STACK', band, tid, len(holds), 'TOP', item['top'][:5], 'SHIM', item['shim'][:8])
    stack_result[str(finished['sample'])] = dict(cpu=cpu, groups=result, start_estimate=start)

(root / 'analysis.json').write_text(json.dumps(dict(one_second=one_second, stacks=stack_result,
    limits='FPS is estimated from frame-counter polling; 59.8-59.9 may be sampling quantization. '
           'Stacks aligned from finish minus native elapsed; watcher polling adds up to about 100 ms uncertainty. '
           'Wall-clock stack samples are not CPU instruction or GPU-time weights.'), indent=2), encoding='utf-8')
