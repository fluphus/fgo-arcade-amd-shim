"""Read bounded ordinary-sampler failure metadata from the range-cache release."""
import struct


PREPARATION_RENDERERS = {
    'fad47b7558bb4e756c889b920d47184e87c51da0f6dac192422c06c047bdfa0c',
}
PRIVATE_UNITS_OFFSET = 4716

RANGE_RENDERERS = {
    # Buffer deletion retires sampler exposure; metadata layout unchanged.
    '0a95ffdc96d1f6848081c89038450027eb3e065b1fbfd818c3dc436303253d32',
    # Persistent-read A/B: the same layout passed the AMD driver regression.
    '2114111ca0996d510e95ac88fedf8eca060bbfc9c395f5897c7560c9ecc06c77',
    # Flare baseline: wrapper layout verified as 4912/4256/4296/4300 bytes.
    '27ec5ee81c4ed5849390ba1a9c3dcbd0d766d44a6e9632b387bc94ec1695e26e',
    '92e7acf6a71c202ead3183ad8e6bd92f3b9a0123852fddd94d0be76b2bf1f02c',
    '3f942203890370498562c381c5d55d34ca3f367ae21f2d7eb2b439292253e59a',
    '642488a23b24ebd1640ec1757455a8b50793327f568b255eb2874d367ff6b333',
    'ec10fc85619e0b42dcc3b9aab0d6654368ba5e1c8e4207aca1fae707f7c83707',
    '8c3e4df8b2930ee0e1d5a824fe795814467eff1a22a722690ba81956bd83a59e',
    'c89b60500458a3779b750e73e0bee258ebf9f3b3a3e32228784fb3fe5def692c',
    '3b2ae4edcfa401b838bb97c6fec286f48800c332ae4c855f965b1d7ee00be30d',
} | PREPARATION_RENDERERS
FAILURE_OFFSET = 4256
RANGE_COUNT_OFFSET = 4296
RANGES_OFFSET = 4300


def read_sampler_failures(read, table_address):
    result = {}
    pointers = struct.unpack('<65536Q', read(table_address, 65536 * 8))
    for program, pointer in enumerate(pointers):
        if not pointer:
            continue
        count, buffer, binding, offset, size, mapped, gpu = struct.unpack(
            '<QIIqqii', read(pointer + FAILURE_OFFSET, 40))
        if not count:
            continue
        original, private = struct.unpack('<II', read(pointer, 8))
        result[str(program)] = dict(count=count, private_program=private,
            original_program=original, buffer=buffer, binding=binding, offset=offset,
            size=size, mapped=bool(mapped), gpu_exposed=bool(gpu))
    return result


def sampler_failure_delta(before, after):
    result = {}
    for program, row in after.items():
        previous = before.get(program)
        reset = previous is not None and (row['count'] < previous['count'] or
            row['private_program'] != previous['private_program'])
        delta = None if reset else row['count'] - (previous['count'] if previous else 0)
        if delta or reset:
            result[program] = dict(row, delta=delta, counter_reset=reset)
    return result
