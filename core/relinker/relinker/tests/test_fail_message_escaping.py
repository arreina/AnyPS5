from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from test_guest_intel_trampolines import PLAIN_SITE, guest_fixture, main_fixture


def module_with_dependency(name):
    image = guest_fixture(PLAIN_SITE)
    strings = b"\x00" + name + b"\x00"
    image[0x800:0x800 + len(strings)] = strings
    tags = [(5, 0x2200), (10, len(strings)), (6, 0x2220), (11, 24),
            (4, 0x2240), (7, 0x2300), (8, 0), (9, 24), (1, 1), (0, 0)]
    for index, tag in enumerate(tags):
        struct.pack_into("<qQ", image, 0x600 + index * 16, *tag)
    struct.pack_into("<QQ", image, 176 + 32, len(tags) * 16, len(tags) * 16)
    return image


def main():
    relinker = Path(sys.argv[1]).resolve()
    cases = [
        (b"\x1b[31mred\x1b[0m/x", b"Invalid or duplicate dependency: \\x1b[31mred\\x1b[0m/x\n"),
        (b"a/\nFAIL: forged", b"Invalid or duplicate dependency: a/\\x0aFAIL: forged\n"),
    ]
    with tempfile.TemporaryDirectory(prefix="anyps5-escaping-") as directory:
        work = Path(directory)
        source = work / "input.elf"
        output = work / "output.elf"
        (work / "sce_module").mkdir()
        source.write_bytes(main_fixture())
        for name, expected in cases:
            (work / "sce_module" / "sample.prx").write_bytes(module_with_dependency(name))
            result = subprocess.run([str(relinker), str(source), str(output)], capture_output=True, timeout=30)
            assert result.returncode == 2 and not output.exists(), result
            assert result.stderr.startswith(b"FAIL: ") and result.stderr.endswith(expected), result.stderr
            assert result.stderr.count(b"\n") == 1 and b"\x1b" not in result.stderr, result.stderr
    print("FAIL message escaping tests passed")


if __name__ == "__main__":
    main()
