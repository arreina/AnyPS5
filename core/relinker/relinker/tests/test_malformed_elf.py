import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

from test_optional_plt import fixture

CASES = int(os.environ.get("ANYPS5_MALFORMED_ELF_CASES", "150"))
SEED = 20260930
EXTREMES = [0, 1, 0x7f, 0xff, 0xffff, 0x7fffffff, 0xffffffff, 0x7fffffffffffffff, 0xffffffffffffffff]
FIELDS = [(0x10, 2), (0x12, 2), (0x18, 8), (0x20, 8), (0x28, 8), (0x36, 2), (0x38, 2), (0x3a, 2), (0x3c, 2), (0x3e, 2)]
FIELDS += [(64 + index * 56 + offset, size) for index in range(2)
           for offset, size in [(0, 4), (4, 4), (8, 8), (16, 8), (32, 8), (40, 8), (48, 8)]]
FIELDS += [(0x400 + index * 16 + offset, 8) for index in range(8) for offset in (0, 8)]


def mutate(rng, data):
    kind = rng.choice(["truncate", "flip", "field", "field", "field"])
    if kind == "truncate":
        return data[:rng.randrange(0, len(data))]
    if kind == "flip":
        for _ in range(rng.randint(1, 8)):
            data[rng.randrange(len(data))] = rng.randrange(256)
        return data
    offset, size = rng.choice(FIELDS)
    value = rng.choice(EXTREMES) & ((1 << (8 * size)) - 1)
    data[offset:offset + size] = value.to_bytes(size, "little")
    return data


def main():
    relinker = Path(sys.argv[1]).resolve()
    rng = random.Random(SEED)
    with tempfile.TemporaryDirectory(prefix="anyps5-malformed-") as directory:
        source = Path(directory) / "input.elf"
        output = Path(directory) / "output.out"
        for case in range(CASES):
            data = fixture()
            for _ in range(rng.randint(1, 4)):
                data = mutate(rng, bytearray(data))
            source.write_bytes(data)
            for mode in ([], ["--windows"]):
                result = subprocess.run([str(relinker), "--skip-sce-module", *mode, str(source), str(output)],
                                        capture_output=True, text=True, timeout=20)
                if result.returncode == 0:
                    output.unlink()
                    continue
                assert result.returncode == 2, (case, mode, result)
                assert result.stderr.startswith("FAIL: ") and "std::" not in result.stderr, (case, mode, result)
                assert not output.exists(), (case, mode, result)
    print("Malformed ELF tests passed")


if __name__ == "__main__":
    main()
