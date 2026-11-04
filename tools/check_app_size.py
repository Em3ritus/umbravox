"""Check the actual flashable binary against app0 in a generated ESP partition table."""
import struct
from pathlib import Path


def check(image, partitions):
    data = Path(partitions).read_bytes()
    slots = []
    for pos in range(0, len(data) - 31, 32):
        magic, kind, subtype, offset, size = struct.unpack_from('<HBBII', data, pos)
        if magic != 0x50AA:
            break  # MD5 record or erased terminator
        if kind == 0 and subtype == 0x10:
            slots.append((offset, size))
    if len(slots) != 1 or slots[0][0] != 0x10000 or slots[0][1] == 0:
        raise ValueError('Expected exactly one ota_0 app partition at 0x10000')
    used = Path(image).stat().st_size
    limit = slots[0][1]
    if used == 0 or used > limit:
        raise ValueError(f'Flashable image {used} bytes does not fit app0 ({limit} bytes)')
    return used, limit


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image')
    parser.add_argument('partitions')
    args = parser.parse_args()
    used, limit = check(args.image, args.partitions)
    print(f'Actual firmware: {used:,}/{limit:,} bytes; {limit-used:,} bytes free')
