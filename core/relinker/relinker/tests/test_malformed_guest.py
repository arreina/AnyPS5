import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile

from test_guest_intel_trampolines import PLAIN_SITE, SITE, guest_fixture, main_fixture

CASES = int(os.environ.get("ANYPS5_MALFORMED_GUEST_CASES", "100"))
SEED = 20260930
REGIONS = [(0, 64), (64, 232), (0x400, 0x420), (0x600, 0x6a0), (0x800, 0x821), (0x830, 0x878), (0x880, 0x890), (0x900, 0x948)]
EXTREMES = [0, 1, 2, 3, 5, 6, 7, 8, 16, 24, 0x7f, 0xff, 0xfe18, 0xffff, 0x1000, 0x2000, 0x2200, 0x2240, 0x6100003f,
            0x7fffffff, 0xffffffff, 0x7fffffffffffffff, 0xffffffffffffffff]
MODES = [[], ["--windows"], ["--to-intel"]]


def rich_guest_fixture(site, needed, imported):
    image = guest_fixture(site)
    image[0x600:0x9a0] = bytes(0x3a0)
    strings = b"\x00sample_func\x00libdep.prx\x00ext_func\x00"
    image[0x800:0x800 + len(strings)] = strings
    symbols = [(0, 0, 0, 0, 0, 0), (1, 0x12, 0, 1, 0x1000, 0x10)]
    if imported:
        symbols.append((24, 0x12, 0, 0, 0, 0))
    for index, symbol in enumerate(symbols):
        struct.pack_into("<IBBHQQ", image, 0x830 + index * 24, *symbol)
    struct.pack_into("<II", image, 0x880, 1, len(symbols))
    relocations = [(0x2380, 8, 0x1000), (0x2390, (1 << 32) | 1, 0)]
    if imported:
        relocations.append((0x2388, (2 << 32) | 6, 0))
    for index, relocation in enumerate(relocations):
        struct.pack_into("<QQQ", image, 0x900 + index * 24, *relocation)
    tags = [(5, 0x2200), (10, len(strings)), (6, 0x2230), (11, 24), (4, 0x2280),
            (7, 0x2300), (8, len(relocations) * 24), (9, 24)]
    if needed:
        tags.append((1, 13))
    tags.append((0, 0))
    for index, tag in enumerate(tags):
        struct.pack_into("<qQ", image, 0x600 + index * 16, *tag)
    struct.pack_into("<QQ", image, 176 + 32, len(tags) * 16, len(tags) * 16)
    return image


def mutate(rng, data):
    if len(data) < REGIONS[-1][1]:
        return data
    start, end = rng.choice(REGIONS)
    kind = rng.random()
    if kind < 0.35:
        data[rng.randrange(start, end)] = rng.randrange(256)
    elif kind < 0.85:
        size = rng.choice([1, 2, 4, 4, 8, 8])
        offset = rng.randrange(start, end - size)
        value = rng.choice(EXTREMES) & ((1 << (8 * size)) - 1)
        data[offset:offset + size] = value.to_bytes(size, "little")
    elif kind < 0.93:
        offset = rng.randrange(0x800, 0x819)
        data[offset:offset + 8] = bytes(rng.choice(b"\x00ab#/:$\xff") for _ in range(8))
    else:
        data = data[:rng.randrange(64, len(data))]
    return data


def relink(relinker, source, output, mode):
    result = subprocess.run([str(relinker), *mode, str(source), str(output)],
                            capture_output=True, text=True, errors="replace", timeout=60)
    shutil.rmtree(source.parent / "app0", ignore_errors=True)
    return result


def main():
    relinker = Path(sys.argv[1]).resolve()
    rng = random.Random(SEED)
    seeds = [rich_guest_fixture(site, needed, imported)
             for site in (SITE, PLAIN_SITE) for needed in (True, False) for imported in (True, False)]
    with tempfile.TemporaryDirectory(prefix="anyps5-guest-") as directory:
        work = Path(directory)
        source = work / "input.elf"
        output = work / "output.out"
        module = work / "sce_module" / "sample.prx"
        module.parent.mkdir()
        source.write_bytes(main_fixture())
        for index, seed in enumerate(seeds):
            module.write_bytes(seed)
            for mode in MODES:
                result = relink(relinker, source, output, mode)
                assert result.returncode == 0 and output.exists(), (index, mode, result)
                output.unlink()
        for case in range(CASES):
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 3)):
                data = mutate(rng, bytearray(data))
            module.write_bytes(data)
            for mode in MODES:
                result = relink(relinker, source, output, mode)
                if result.returncode == 0:
                    output.unlink()
                    continue
                assert result.returncode == 2, (case, mode, result)
                assert "FAIL: " in result.stderr and "std::" not in result.stderr, (case, mode, result)
                assert not output.exists(), (case, mode, result)
    print("Malformed guest module tests passed")


if __name__ == "__main__":
    main()
