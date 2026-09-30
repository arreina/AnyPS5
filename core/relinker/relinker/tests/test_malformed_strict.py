import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

from test_malformed_unwind import make_lsda_image
from test_tls_function_coverage import make_image

CASES = int(os.environ.get("ANYPS5_MALFORMED_STRICT_CASES", "100"))
SEED = 20260930
REGIONS = [(0x400, 0x490), (0x620, 0x650), (0x680, 0x690), (0x700, 0x720), (0x900, 0x9a0), (0x1200, 0x1260), (0x1840, 0x1870)]
EXTREMES = [0, 1, 2, 7, 8, 0x18, 0x7f, 0x80, 0xff, 0xffff, 0x7fffffff, 0xffffffff, 0x1200, 0x1240, 0x6100003f, 0xffffffffffffffff]
OPCODES = bytes.fromhex("e8 e9 eb ff c3 cc 74 75 0f 48 8d 05 0d 25 15 90 64 66 c2 ca")
MODES = [["--windows", "unused-filter=2"], ["--windows", "unused-filter=1"]]


def with_hash(image):
    struct.pack_into("<IIII", image, 0x680, 1, 1, 0, 0)
    struct.pack_into("<qQqQ", image, 0x470, 4, 0x680, 0, 0)
    struct.pack_into("<QQ", image, 160, 0x90, 8)
    struct.pack_into("<Q", image, 152, 0x90)
    return image


def mutate(rng, data):
    if len(data) < REGIONS[-1][1]:
        return data
    start, end = rng.choice(REGIONS)
    kind = rng.random()
    if kind < 0.35:
        code = start >= 0x1200 and rng.random() < 0.5
        data[rng.randrange(start, end)] = rng.choice(OPCODES) if code else rng.randrange(256)
    elif kind < 0.8:
        size = rng.choice([1, 2, 4, 4, 8, 8])
        offset = rng.randrange(start, end - size)
        value = rng.choice(EXTREMES) & ((1 << (8 * size)) - 1)
        data[offset:offset + size] = value.to_bytes(size, "little")
    elif kind < 0.95:
        length = rng.randint(2, 12)
        offset = rng.randrange(start, end - length)
        data[offset:offset + length] = bytes(rng.choice(OPCODES) for _ in range(length))
    else:
        data = data[:rng.randrange(0x400, len(data))]
    return data


def relink(relinker, source, output, mode):
    return subprocess.run([str(relinker), "--skip-sce-module", *mode, str(source), str(output)],
                          capture_output=True, text=True, timeout=60)


def main():
    relinker = Path(sys.argv[1]).resolve()
    rng = random.Random(SEED)
    seeds = [make_image(transfer, "symbol") for transfer in ("register", "table", "memory")]
    seeds += [with_hash(make_lsda_image(transfer)) for transfer in ("register", "table", "memory")]
    with tempfile.TemporaryDirectory(prefix="anyps5-strict-") as directory:
        source = Path(directory) / "input.elf"
        output = Path(directory) / "output.out"
        for index, seed in enumerate(seeds):
            source.write_bytes(seed)
            result = relink(relinker, source, output, MODES[0])
            assert result.returncode == 0 and "Strict reachability:" in result.stdout, (index, result)
            output.unlink()
        for case in range(CASES):
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 3)):
                data = mutate(rng, bytearray(data))
            source.write_bytes(data)
            for mode in MODES:
                result = relink(relinker, source, output, mode)
                if result.returncode == 0:
                    output.unlink()
                    continue
                assert result.returncode == 2, (case, mode, result)
                assert result.stderr.startswith("FAIL: ") and "std::" not in result.stderr, (case, mode, result)
                assert not output.exists(), (case, mode, result)
    print("Malformed strict filter tests passed")


if __name__ == "__main__":
    main()
