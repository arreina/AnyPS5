#include "SceTypes.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

extern "C" {
int APS5_VABI kqueue_nid_postfix();
int APS5_VABI kevent_nid_postfix(int, const KernelEvent*, int, KernelEvent*, int, const KernelTimespec*);
int APS5_VABI close_nid_postfix(int);
int APS5_VABI pipe_nid_postfix(int*);
int APS5_VABI socketpair_nid_postfix(int, int, int, int*);
int* APS5_VABI __error_nid_postfix();
}

namespace {

constexpr int GuestUnix = 1;
constexpr int GuestStream = 1;

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
};

void Require(bool value, const char* what) {
    if (!value) {
        std::fprintf(stderr, "kqueue fuzz failed: %s\n", what);
        std::abort();
    }
}

}  // namespace

// Cambios y listas de eventos aleatorios sobre una cola real. Las listas son del tamaño exacto en el heap y
// el tiempo de espera nunca bloquea; las excepciones de «no implementado» se esperan.
int main() {
    const char* env = std::getenv("ANYPS5_KQUEUE_FUZZ_CASES");
    const std::size_t cases = env ? std::strtoull(env, nullptr, 10) : 20000;
    Rng rng{0x6b71756575650001ull};
    static const std::int16_t filters[] = {-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, 0, 1, 100, -100};
    std::vector<int> pool;
    std::size_t returned = 0;
    for (std::size_t i = 0; i < cases; ++i) {
        if (i % 50 == 0) {
            for (const int descriptor : pool) close_nid_postfix(descriptor);
            pool.clear();
            int pipes[2] = {-1, -1};
            if (pipe_nid_postfix(pipes) == 0) pool = {pipes[0], pipes[1]};
            int sockets[2] = {-1, -1};
            if (socketpair_nid_postfix(GuestUnix, GuestStream, 0, sockets) == 0) {
                pool.push_back(sockets[0]);
                pool.push_back(sockets[1]);
            }
        }
        const int queue = kqueue_nid_postfix();
        Require(queue >= 0, "kqueue");
        for (int round = 0; round < 3; ++round) {
            const int nchanges = static_cast<int>(rng.Below(7));
            auto* changes = static_cast<KernelEvent*>(std::malloc(sizeof(KernelEvent) * (nchanges ? nchanges : 1)));
            for (int k = 0; k < nchanges; ++k) {
                KernelEvent event{};
                event.ident = !pool.empty() && rng.Below(3) != 0 ? static_cast<std::uintptr_t>(pool[rng.Below(pool.size())])
                                                                  : static_cast<std::uintptr_t>(rng.Below(4) == 0 ? rng.Next() : rng.Below(64));
                event.filter = filters[rng.Below(sizeof(filters) / sizeof(filters[0]))];
                event.flags = static_cast<std::uint16_t>(rng.Below(8) == 0 ? rng.Next() : rng.Below(0x100));
                event.fflags = static_cast<std::uint32_t>(rng.Below(3) == 0 ? rng.Next() : rng.Below(16));
                event.data = static_cast<std::intptr_t>(rng.Below(3) == 0 ? rng.Next() : rng.Below(1000));
                event.udata = reinterpret_cast<void*>(static_cast<std::uintptr_t>(rng.Next()));
                changes[k] = event;
            }
            const int nevents = static_cast<int>(rng.Below(7));
            KernelEvent* events = rng.Below(8) == 0 ? nullptr : static_cast<KernelEvent*>(std::malloc(sizeof(KernelEvent) * (nevents ? nevents : 1)));
            const std::int64_t seconds = rng.Below(10) == 0 ? -1 : 0;
            const std::int64_t nanoseconds = rng.Below(10) == 0 ? 1000000000 + static_cast<std::int64_t>(rng.Below(5)) : 0;
            const KernelTimespec poll{seconds, nanoseconds};
            // Sin lista de eventos o sin eventos pedidos no se espera: se pasa siempre un tiempo no bloqueante.
            int result = -2;
            try {
                result = kevent_nid_postfix(queue, nchanges ? changes : (rng.Below(2) ? nullptr : changes), nchanges, events, nevents, &poll);
            } catch (const std::exception&) {
            }
            if (result >= 0) {
                returned += static_cast<std::size_t>(result);
                Require(result <= nevents, "kevent returned more events than the list holds");
            }
            std::free(changes);
            std::free(events);
        }
        close_nid_postfix(queue);
    }
    for (const int descriptor : pool) close_nid_postfix(descriptor);
    std::printf("kqueue fuzz: %zu cases, %zu events returned\n", cases, returned);
    return 0;
}
