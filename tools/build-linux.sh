#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$root/build}"
build_type="${BUILD_TYPE:-Release}"
jobs="${JOBS:-$(nproc)}"
run_tests=1

usage() {
    echo "Usage: tools/build-linux.sh [--no-tests]"
    echo "Environment: BUILD_DIR (default: build), BUILD_TYPE (default: Release), JOBS (default: nproc)"
}

for arg in "$@"; do
    case "$arg" in
        --no-tests) run_tests=0 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

missing=()
for tool in git cmake gcc g++; do
    command -v "$tool" > /dev/null || missing+=("$tool")
done
if ((${#missing[@]})); then
    echo "Missing required tools: ${missing[*]}" >&2
    echo "On Debian/Ubuntu: sudo apt install git cmake build-essential" >&2
    exit 1
fi
command -v python3 > /dev/null || echo "python3 not found: the Python relinker tests will not be registered" >&2

generator=()
command -v ninja > /dev/null && generator=(-G Ninja)

git -C "$root" submodule sync --quiet
git -C "$root" submodule update --init

cmake -S "$root" -B "$build_dir" "${generator[@]}" \
    -DCMAKE_BUILD_TYPE="$build_type" -DBUILD_TESTING=ON \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build "$build_dir" -j "$jobs"
cmake --build "$build_dir" --target libs -j "$jobs"

if ((run_tests)); then
    ctest --test-dir "$build_dir" --output-on-failure --timeout 120 -j "$jobs"
fi
