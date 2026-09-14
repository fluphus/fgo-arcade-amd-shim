"""Reuse revision-1 translation payloads in the content-shared revision-2 cache."""
import argparse
import json
from pathlib import Path
import struct

HEADER = struct.Struct('<8s6I2Q')
MASK = (1 << 64) - 1
PRIME = 1099511628211
INVERSE = pow(PRIME, -1, 1 << 64)


def content_hash(value, shader, length):
    # Undo the final length mix and object-name mix; finish with length only.
    before_id = (((((value * INVERSE) & MASK) ^ length) * INVERSE) & MASK) ^ shader
    return ((before_id ^ length) * PRIME) & MASK


def convert(raw):
    magic, version, stage, shader, length, size, pointers, ha, hb = HEADER.unpack_from(raw)
    if magic != b'FGOSHDR1' or version != 1:
        raise ValueError('Expected a revision-1 shader cache entry')
    if not 0 < size <= 64 * 1024 * 1024 or len(raw) != HEADER.size + size + pointers + 5:
        raise ValueError('Incomplete shader cache payload')
    ha, hb = content_hash(ha, shader, length), content_hash(hb, shader, length)
    header = HEADER.pack(b'FGOSHDR2', 2, stage, 0, length, size, pointers, ha, hb)
    name = f'{stage:08x}-{ha:016x}-{hb:016x}.glsl'
    return name, header + raw[HEADER.size:]


def migrate(source, destination):
    if source.resolve() == destination.resolve():
        raise ValueError('Use a separate destination directory')
    destination.mkdir(parents=True, exist_ok=True)
    files = written = duplicates = source_bytes = 0
    unique = {}
    for path in sorted(source.glob('*.glsl')):
        raw = path.read_bytes()
        name, converted = convert(raw)
        target = destination / name
        if target.exists():
            if target.read_bytes() != converted:
                raise ValueError(f'Conflicting translations for {name}')
            duplicates += 1
        else:
            temporary = target.with_suffix('.tmp')
            temporary.write_bytes(converted)
            temporary.replace(target)
            written += 1
        unique[name] = len(converted)
        files += 1
        source_bytes += len(raw)
    if not files:
        raise ValueError('No revision-1 cache entries found')
    return dict(source_files=files, unique_files=len(unique), written=written,
                reused=duplicates, source_bytes=source_bytes,
                shared_bytes=sum(unique.values()), originals_preserved=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = migrate(args.source, args.destination)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(text, encoding='utf-8')
    print(text, end='')


if __name__ == '__main__':
    main()
