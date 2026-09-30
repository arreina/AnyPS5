from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    relinker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="anyps5-input-file-") as directory:
        work = Path(directory)
        cases = [
            ("directory", work / "input", "Not a regular file: "),
            ("missing", work / "missing.elf", "Cannot open file: "),
        ]
        (work / "input").mkdir()
        for name, source, error in cases:
            output = work / (name + ".out")
            result = subprocess.run([str(relinker), "--skip-sce-module", str(source), str(output)],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 2 and error + str(source) in result.stderr and not output.exists(), (name, result)
    print("Input file tests passed")


if __name__ == "__main__":
    main()
