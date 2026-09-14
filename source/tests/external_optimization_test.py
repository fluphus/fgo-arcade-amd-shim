"""Offline cache/state regression and CPU/deadline comparison; no game capture."""
import argparse
import ctypes as C
import importlib.util
import json
from pathlib import Path
import statistics
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wrapper', type=Path, required=True)
    parser.add_argument('--old-cache', type=Path, required=True)
    parser.add_argument('--migrated-cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    dll = C.WinDLL(str(args.wrapper.resolve()))

    def api(name, result, *types):
        fn = getattr(dll, name)
        fn.restype, fn.argtypes = result, list(types)
        return fn

    U, I, P, S = C.c_uint, C.c_int, C.c_void_p, C.c_char_p
    submit = api('TestCacheSubmit', I, U, U, S, I, I)
    output = api('TestCacheOutput', P)
    metadata = api('TestCacheMetadata', I, U, P)
    cache_path = api('TestCachePath', S, U, U, S, I)
    hit = api('TestCacheHit', I, U, U, S, I)
    delete = api('TestCacheDelete', None, U)

    def result(shader, size):
        assert size >= 0
        meta = C.create_string_buffer(metadata(shader, None))
        metadata(shader, meta)
        return C.string_at(output(), size), meta.raw

    spec = importlib.util.spec_from_file_location('cache_migration', ROOT / 'tools/migrate-shader-cache.py')
    migration = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(migration)
    converted = unique = 0
    seen = set()
    for old in args.old_cache.glob('*.glsl'):
        raw = old.read_bytes()
        name, expected = migration.convert(raw)
        actual = (args.migrated_cache / name).read_bytes()
        assert actual == expected, old
        assert actual[48:] == raw[48:], old
        converted += 1
        seen.add(name)
    unique = len(seen)
    assert converted and unique

    fixtures = [(p, 0x8B30 if p.name in ('program_1156_shader_1155.glsl',
                 'native_shader_2603.glsl') else 0x8B31)
                for p in sorted((ROOT / 'tests/fixtures').rglob('*.glsl'))]
    shared = []
    start = time.perf_counter()
    cold_ms = warm_ms = 0.0
    for index, (path, stage) in enumerate(fixtures):
        raw = path.read_bytes()
        a, b = 60000 + index * 2, 60001 + index * 2
        before = time.perf_counter()
        reference = result(a, submit(a, stage, raw, len(raw), 0))
        cold_ms += (time.perf_counter() - before) * 1000
        pa = Path(cache_path(a, stage, raw, len(raw)).decode())
        pb = Path(cache_path(b, stage, raw, len(raw)).decode())
        assert pa == pb and pa.parent == args.wrapper.resolve().parent / 'shader-cache-r2'
        pa.unlink(missing_ok=True)
        seeded = result(a, submit(a, stage, raw, len(raw), 1))
        assert seeded == reference
        assert hit(b, stage, raw, -1) == 1, path
        before = time.perf_counter()
        warm = result(b, submit(b, stage, raw, -1, 1))
        warm_ms += (time.perf_counter() - before) * 1000
        assert warm == reference, path
        delete(b)
        assert hit(b, stage, raw, len(raw)) == 1
        assert result(b, len(reference[0])) == reference
        changed = raw + b'\n// different source\n'
        assert hit(b, stage, changed, len(changed)) == 0
        assert hit(b, 0x8B30 if stage == 0x8B31 else 0x8B31, raw, len(raw)) == 0
        # Independently check migration's reversal against the C runtime key.
        def legacy_hash(seed):
            value = seed
            for byte in raw:
                value = ((value ^ byte) * migration.PRIME) & migration.MASK
            for item in (stage, a, len(raw)):
                value = ((value ^ item) * migration.PRIME) & migration.MASK
            return value
        hashes = [migration.content_hash(legacy_hash(seed), a, len(raw)) for seed in
                  (1469598103934665603, 1099511628211 ^ 0x5348494D43414348)]
        assert pa.name == f'{stage:08x}-{hashes[0]:016x}-{hashes[1]:016x}.glsl'
        shared.append(dict(fixture=path.name, stage=stage, bytes=len(reference[0])))

    first = b'#version 450 core\nvoid main(){gl_Position=vec4(0.0);}\n'
    second = b'#version 450 core\nvoid main(){gl_Position=vec4(1.0);}\n'
    original = result(64000, submit(64000, 0x8B31, first, -1, 1))
    replacement = result(64000, submit(64000, 0x8B31, second, -1, 1))
    assert original[0] != replacement[0]
    assert result(64001, submit(64001, 0x8B31, first, -1, 1)) == original
    assert result(64001, submit(64001, 0x8B31, second, -1, 1)) == replacement
    cache = dict(migration_entries=converted, unique_entries=unique, fixtures=shared,
                 different_id_equal=True, negative_lengths=True, stage_and_source_misses=True,
                 delete_and_replace=True, cold_translation_ms=cold_ms, warm_cache_ms=warm_ms,
                 elapsed_ms=(time.perf_counter() - start) * 1000)

    diagnostic = (U * 5)()
    api('TestDiagnosticDraw', None, P)(diagnostic)
    assert list(diagnostic) == [2000, 0, 0, 0, 0x0405], list(diagnostic)
    print(json.dumps(dict(cache=cache, diagnostic_draw=list(diagnostic))), flush=True)

    pace = api('TestPacing', None, I, I, C.c_double, P, P)
    timings = []
    for work, frames in ((0.0, 180), (8.0, 90), (20.0, 12)):
        for legacy in (1, 0):
            intervals, summary = (C.c_double * frames)(), (C.c_double * 4)()
            pace(legacy, frames, work, intervals, summary)
            values = sorted(intervals)
            assert values[0] >= 16.60, values[0]
            if work > 16.7:
                assert values[0] >= work - 0.1  # no catch-up frames after late work
            timings.append(dict(legacy=bool(legacy), work_ms=work, frames=frames,
                cpu_ms=summary[0], cpu_cycles=summary[3], wall_ms=summary[1],
                high_resolution_timer=bool(summary[2]),
                min_ms=values[0], median_ms=statistics.median(values),
                p95_ms=values[int((len(values) - 1) * .95)], max_ms=values[-1]))
            print(json.dumps(timings[-1]), flush=True)
    report = dict(cache=cache, diagnostic_draw=list(diagnostic), pacing=timings, passed=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
