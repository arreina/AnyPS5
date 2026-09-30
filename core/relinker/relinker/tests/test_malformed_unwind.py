import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

from test_tls_function_coverage import make_image

CASES = int(os.environ.get("ANYPS5_MALFORMED_UNWIND_CASES", "150"))
SEED = 20260930
UNWIND = (0x900, 0x9a0)
EXTREMES = [0, 1, 2, 0x7f, 0x80, 0xff, 0xffff, 0x7fffffff, 0xffffffff, 0xfffffff0, 0xffffffffffffffff]
WIDTHS = [1, 1, 1, 2, 4, 4, 8]
LEB_FIELDS = [0x90d, 0x90e, 0x910, 0x92c, 0x943, 0x944, 0x945, 0x946, 0x947]


def make_lsda_image(transfer):
    image = make_image(transfer, "unwind")
    function_size = struct.unpack_from("<Q", image, 0x920)[0]
    image[0x900:0x980] = bytes(0x80)
    cie = struct.pack("<IB", 0, 1) + b"zLR\x00" + bytes([1, 0x78, 0x10, 2, 0x00, 0x00, 0x00])
    image[0x900:0x904 + len(cie)] = struct.pack("<I", len(cie)) + cie
    fde = struct.pack("<IQQ", 0x1c, 0x1200, function_size) + bytes([8]) + struct.pack("<Q", 0x940) + bytes(3)
    image[0x918:0x91c + len(fde)] = struct.pack("<I", len(fde)) + fde
    image[0x940:0x948] = bytes([0xff, 0xff, 0x01, 0x04, 0x00, 0x10, 0x40, 0x00])
    struct.pack_into("<BBBBQIQQ", image, 0x980, 1, 0, 3, 0, 0x900, 1, 0x1200, 0x918)
    return image


def mutate(rng, data):
    if len(data) < UNWIND[1]:
        return data
    kind = rng.random()
    if kind < 0.4:
        data[rng.randrange(*UNWIND)] = rng.randrange(256)
    elif kind < 0.75:
        size = rng.choice(WIDTHS)
        offset = rng.randrange(UNWIND[0], UNWIND[1] - size)
        value = rng.choice(EXTREMES) & ((1 << (8 * size)) - 1)
        data[offset:offset + size] = value.to_bytes(size, "little")
    elif kind < 0.82:
        data[rng.randrange(0x909, 0x90d)] = rng.choice(b"zRPLS\x00\x01\x03\x1b\x9b\xff")
    elif kind < 0.95:
        length = rng.randint(9, 16)
        offset = rng.choice(LEB_FIELDS)
        data[offset:offset + length] = bytes(rng.randrange(0x80, 0x100) for _ in range(length))
    else:
        data = data[:rng.randrange(UNWIND[0], len(data))]
    return data


def relink(relinker, source, output, mode):
    return subprocess.run([str(relinker), "--skip-sce-module", *mode, str(source), str(output)],
                          capture_output=True, text=True, timeout=20)


def main():
    relinker = Path(sys.argv[1]).resolve()
    rng = random.Random(SEED)
    seeds = [make_lsda_image(transfer) for transfer in ("register", "table", "memory")]
    with tempfile.TemporaryDirectory(prefix="anyps5-unwind-") as directory:
        source = Path(directory) / "input.elf"
        output = Path(directory) / "output.out"
        for index, seed in enumerate(seeds):
            source.write_bytes(seed)
            result = relink(relinker, source, output, ["--windows"])
            assert result.returncode == 0 and output.exists(), (index, result)
            output.unlink()
        for case in range(CASES):
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 3)):
                data = mutate(rng, bytearray(data))
            source.write_bytes(data)
            for mode in (["--windows"], []):
                result = relink(relinker, source, output, mode)
                if result.returncode == 0:
                    output.unlink()
                    continue
                assert result.returncode == 2, (case, mode, result)
                assert result.stderr.startswith("FAIL: ") and "std::" not in result.stderr, (case, mode, result)
                assert not output.exists(), (case, mode, result)
    print("Malformed unwind tests passed")


if __name__ == "__main__":
    main()
