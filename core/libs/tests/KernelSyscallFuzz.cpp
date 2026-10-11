#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
extern "C" {
int APS5_VABI shm_open_nid_postfix(const char*, int, int);
int APS5_VABI ftruncate_nid_postfix(int, std::int64_t);
std::int64_t APS5_VABI pread_nid_postfix(int, void*, std::size_t, std::int64_t);
std::int64_t APS5_VABI pwrite_nid_disambig1_nid_postfix(int, const void*, std::size_t, std::int64_t);
int APS5_VABI close_nid_postfix(int);
void* APS5_VABI mmap_nid_postfix(void*, std::size_t, int, int, int, std::int64_t);
int APS5_VABI munmap_nid_postfix(void*, std::size_t);
int APS5_VABI mprotect_nid_postfix(void*, std::size_t, int);
int APS5_VABI msync_nid_postfix(void*, std::size_t, int);
int APS5_VABI madvise_nid_postfix(void*, std::size_t, int);
int* APS5_VABI __error_nid_postfix();
}
// Deterministic fuzzing of guest-facing kernel calls with synthetic arguments only.
// Guest errors (-1 plus errno) and NotImplemented exceptions are expected; a crash or a
// sanitizer report is the failure.
namespace {
struct Rng {
    std::uint64_t state;
    std::uint64_t Next() {
        state += 0x9e3779b97f4a7c15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    std::uint64_t Below(std::uint64_t n) { return Next() % n; }
    std::uint64_t Interesting() {
        static const std::uint64_t values[] = {0, 1, 2, 7, 0xfff, 0x1000, 0x1001, 0x3fff, 0x4000, 0x200000,
            0x7fffffff, 0x80000000ull, 0xffffffffull, 0x7fffffffffffffffull, 0x8000000000000000ull,
            0xfffffffffffff000ull, 0xffffffffffffffffull};
        return Next() % 4 == 0 ? Next() : values[Below(sizeof(values) / sizeof(values[0]))];
    }
};
template <class F> void Guarded(std::size_t& notImplemented, F&& call) {
    try {
        call();
    } catch (const std::exception&) {
        ++notImplemented;
    }
}
}
int main() {
    const char* env = std::getenv("ANYPS5_KERNEL_FUZZ_CASES");
    const std::size_t cases = env ? std::strtoull(env, nullptr, 10) : 20000;
    Rng rng{0x6b65726e656c0001ull};
    std::size_t notImplemented = 0;
    std::vector<int> fds;
    std::vector<std::pair<void*, std::size_t>> maps;
    static char buffer[0x2000];
    for (std::size_t i = 0; i < cases; ++i) {
        const char* const anonymous = reinterpret_cast<const char*>(1);
        switch (rng.Below(9)) {
        case 0:
            Guarded(notImplemented, [&] {
                const int fd = shm_open_nid_postfix(rng.Below(2) ? anonymous : "/fuzz", static_cast<int>(rng.Interesting()), static_cast<int>(rng.Interesting()));
                if (fd >= 0) fds.push_back(fd);
            });
            break;
        case 1:
            Guarded(notImplemented, [&] {
                const int fd = shm_open_nid_postfix(anonymous, 0x100202, 0600);
                if (fd >= 0) fds.push_back(fd);
            });
            break;
        case 2:
            if (!fds.empty()) Guarded(notImplemented, [&] {
                ftruncate_nid_postfix(fds[rng.Below(fds.size())], static_cast<std::int64_t>(rng.Below(3) ? rng.Below(0x10000) : rng.Interesting()));
            });
            break;
        case 3:
            if (!fds.empty()) Guarded(notImplemented, [&] {
                pread_nid_postfix(fds[rng.Below(fds.size())], buffer, rng.Below(sizeof(buffer) + 1), static_cast<std::int64_t>(rng.Below(3) ? rng.Below(0x10000) : rng.Interesting()));
            });
            break;
        case 4:
            if (!fds.empty()) Guarded(notImplemented, [&] {
                pwrite_nid_disambig1_nid_postfix(fds[rng.Below(fds.size())], buffer, rng.Below(sizeof(buffer) + 1), static_cast<std::int64_t>(rng.Below(3) ? rng.Below(0x10000) : rng.Interesting()));
            });
            break;
        case 5:
            if (!fds.empty() && rng.Below(2)) Guarded(notImplemented, [&] {
                const auto at = rng.Below(fds.size());
                close_nid_postfix(fds[at]);
                fds.erase(fds.begin() + static_cast<std::ptrdiff_t>(at));
            });
            else Guarded(notImplemented, [&] { close_nid_postfix(static_cast<int>(rng.Interesting())); });
            break;
        case 6:
            Guarded(notImplemented, [&] {
                void* const p = mmap_nid_postfix(nullptr, rng.Below(2) ? rng.Below(0x20000) : rng.Interesting(), static_cast<int>(rng.Below(8)), 0x1002, -1, 0);
                if (p != reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1))) maps.emplace_back(p, 0x1000);
            });
            break;
        case 7:
            if (!maps.empty()) Guarded(notImplemented, [&] {
                auto& m = maps[rng.Below(maps.size())];
                const auto offset = rng.Below(3) ? 0 : rng.Interesting();
                const auto length = rng.Below(3) ? rng.Below(0x4000) : rng.Interesting();
                auto* const address = static_cast<char*>(m.first) + (offset & 0xffff);
                switch (rng.Below(3)) {
                case 0: mprotect_nid_postfix(address, length, static_cast<int>(rng.Interesting())); break;
                case 1: msync_nid_postfix(address, length, static_cast<int>(rng.Interesting())); break;
                default: madvise_nid_postfix(address, length, static_cast<int>(rng.Interesting())); break;
                }
            });
            break;
        default:
            if (!maps.empty()) Guarded(notImplemented, [&] {
                const auto at = rng.Below(maps.size());
                munmap_nid_postfix(maps[at].first, maps[at].second);
                maps.erase(maps.begin() + static_cast<std::ptrdiff_t>(at));
            });
            else Guarded(notImplemented, [&] { munmap_nid_postfix(reinterpret_cast<void*>(rng.Interesting()), rng.Interesting()); });
            break;
        }
    }
    for (const int fd : fds) close_nid_postfix(fd);
    std::printf("kernel syscall fuzz: %zu cases, %zu not-implemented\n", cases, notImplemented);
    return 0;
}
