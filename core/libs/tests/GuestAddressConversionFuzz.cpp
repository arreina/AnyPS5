#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

extern "C" {
int APS5_VABI __inet_pton_nid_postfix(int, const char*, void*);
const char* APS5_VABI __inet_ntop_nid_postfix(int, const void*, char*, std::uint32_t);
int* APS5_VABI __error_nid_postfix();
}

namespace {

constexpr int GuestInet = 2;
constexpr int GuestInet6 = 28;

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

void Require(bool value, const char* what, const std::string& detail = {}) {
    if (!value) {
        std::fprintf(stderr, "address conversion fuzz failed: %s %s\n", what, detail.c_str());
        std::abort();
    }
}

// Copia con el tamaño exacto en el heap, para que ASan vea cualquier lectura pasada del terminador.
char* Heap(const std::string& text) {
    auto* memory = static_cast<char*>(std::malloc(text.size() + 1));
    std::memcpy(memory, text.c_str(), text.size() + 1);
    return memory;
}

std::string RandomText(Rng& rng, bool v6) {
    static const char v4Alphabet[] = "0123456789.";
    static const char v6Alphabet[] = "0123456789abcdefABCDEF::::..";
    const char* alphabet = v6 ? v6Alphabet : v4Alphabet;
    const std::size_t size = std::strlen(alphabet);
    std::string text;
    for (std::size_t i = rng.Below(v6 ? 50 : 20); i > 0; --i) text.push_back(alphabet[rng.Below(size)]);
    return text;
}

std::string Mutate(std::string text, Rng& rng) {
    static const char extra[] = "0123456789abcdefABCDEF:.%x -/\n";
    for (std::size_t edits = 1 + rng.Below(3); edits > 0; --edits) {
        const std::size_t at = text.empty() ? 0 : rng.Below(text.size());
        switch (rng.Below(3)) {
        case 0: if (!text.empty()) text[at] = extra[rng.Below(sizeof(extra) - 1)]; break;
        case 1: text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), extra[rng.Below(sizeof(extra) - 1)]); break;
        default: if (!text.empty()) text.erase(text.begin() + static_cast<std::ptrdiff_t>(at)); break;
        }
    }
    return text;
}

std::array<std::uint8_t, 16> RandomAddress(Rng& rng, bool v6) {
    std::array<std::uint8_t, 16> bytes{};
    const std::size_t size = v6 ? 16 : 4;
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>(rng.Below(3) == 0 ? rng.Next() : 0);
    if (v6 && rng.Below(6) == 0) {  // direcciones IPv4 incrustadas
        std::fill(bytes.begin(), bytes.begin() + 10, std::uint8_t{0});
        bytes[10] = bytes[11] = rng.Below(2) ? 0xff : 0;
    }
    return bytes;
}

}  // namespace

int main() {
    const char* env = std::getenv("ANYPS5_ADDRESS_FUZZ_CASES");
    const std::size_t cases = env ? std::strtoull(env, nullptr, 10) : 100000;
    Rng rng{0x696e657400000001ull};
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < cases; ++i) {
        const bool v6 = rng.Below(2) == 0;
        const int family = v6 ? GuestInet6 : GuestInet;
        const std::size_t size = v6 ? 16 : 4;

        // 1. Texto -> bytes: aleatorio, o mutado a partir de una dirección válida.
        const auto source = RandomAddress(rng, v6);
        char formatted[64];
        std::string text;
        if (rng.Below(2) == 0) {
            text = RandomText(rng, v6);
        } else {
            Require(__inet_ntop_nid_postfix(family, source.data(), formatted, sizeof(formatted)) == formatted, "ntop of a valid address");
            text = Mutate(formatted, rng);
        }
        char* const input = Heap(text);
        std::array<std::uint8_t, 16> parsed{};
        auto* destination = static_cast<std::uint8_t*>(std::malloc(size));
        std::memset(destination, 0xA5, size);
        const int result = __inet_pton_nid_postfix(family, input, destination);
        Require(result == 0 || result == 1, "pton result");
        if (result == 1) {
            ++accepted;
            std::memcpy(parsed.data(), destination, size);
        } else {
            for (std::size_t k = 0; k < size; ++k) Require(destination[k] == 0xA5, "pton wrote on failure", text);
        }
#ifndef _WIN32
        {
            std::array<std::uint8_t, 16> host{};
            const int hostResult = ::inet_pton(v6 ? AF_INET6 : AF_INET, input, host.data());
            Require(hostResult == result, "pton disagrees with the host about acceptance", (v6 ? "v6 `" : "v4 `") + text + "'");
            if (result == 1) Require(std::memcmp(host.data(), parsed.data(), size) == 0, "pton bytes differ from the host", text);
        }
#endif
        std::free(destination);
        std::free(input);

        // 2. Bytes -> texto: igual que el host, ida y vuelta y capacidades pequeñas.
        char reference[64];
        const char* const ntop = __inet_ntop_nid_postfix(family, source.data(), reference, sizeof(reference));
        Require(ntop == reference, "ntop with ample capacity");
#ifndef _WIN32
        char host[64];
        Require(::inet_ntop(v6 ? AF_INET6 : AF_INET, source.data(), host, sizeof(host)) != nullptr, "host ntop");
        Require(std::strcmp(reference, host) == 0, "ntop text differs from the host", std::string(reference) + " vs " + host);
#endif
        std::array<std::uint8_t, 16> back{};
        Require(__inet_pton_nid_postfix(family, reference, back.data()) == 1, "round trip parse", reference);
        Require(std::memcmp(back.data(), source.data(), size) == 0, "round trip bytes", reference);
        const std::uint32_t capacity = static_cast<std::uint32_t>(rng.Below(50));
        auto* small = static_cast<char*>(std::malloc(capacity ? capacity : 1));
        std::memset(small, 'x', capacity ? capacity : 1);
        *__error_nid_postfix() = 0;
        const char* const result2 = __inet_ntop_nid_postfix(family, source.data(), small, capacity);
        if (capacity >= std::strlen(reference) + 1) {
            Require(result2 == small && std::strcmp(small, reference) == 0, "ntop fits");
        } else {
            Require(result2 == nullptr && *__error_nid_postfix() == 28, "ntop too small");
            for (std::uint32_t k = 0; k < capacity; ++k) Require(small[k] == 'x', "ntop wrote on failure");
        }
        std::free(small);
    }
    std::printf("address conversion fuzz: %zu cases, %zu texts accepted\n", cases, accepted);
    return 0;
}
