#include "Decoder/Png.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

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
};

void Require(bool value) {
    if (!value) std::abort();
}

std::vector<std::uint8_t> Mutate(std::vector<std::uint8_t> bytes, Rng& rng) {
    const std::size_t edits = 1 + rng.Below(6);
    for (std::size_t i = 0; i < edits && !bytes.empty(); ++i) {
        const std::size_t at = rng.Below(bytes.size());
        switch (rng.Below(5)) {
        case 0: bytes[at] ^= static_cast<std::uint8_t>(1u << rng.Below(8)); break;
        case 1: bytes[at] = static_cast<std::uint8_t>(rng.Next()); break;
        case 2: bytes.resize(at); break;
        case 3: bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(rng.Next())); break;
        default: bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at)); break;
        }
    }
    return bytes;
}

}  // namespace

int main() {
    const char* env = std::getenv("ANYPS5_PNG_FUZZ_CASES");
    const std::size_t cases = env ? std::strtoull(env, nullptr, 10) : 20000;
    Rng rng{0x706e670000000001ull};
    std::size_t decoded = 0;
    for (std::size_t i = 0; i < cases; ++i) {
        const std::uint32_t width = 1 + static_cast<std::uint32_t>(rng.Below(24));
        const std::uint32_t height = 1 + static_cast<std::uint32_t>(rng.Below(24));
        const std::uint32_t channels = 1 + static_cast<std::uint32_t>(rng.Below(4));
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * channels);
        const bool smooth = rng.Below(2) == 0;
        for (auto& pixel : pixels) pixel = smooth ? static_cast<std::uint8_t>(rng.Below(4) * 40) : static_cast<std::uint8_t>(rng.Next());
        Decoder::Png::EncodeOptions options{};
        options.compressionLevel = static_cast<int>(rng.Below(10));
        const auto png = Decoder::Png::Encode(pixels, width, height, channels, options);

        const auto header = Decoder::Png::ParseHeader(png);
        Require(header && header->width == width && header->height == height);
        const auto image = Decoder::Png::Decode(png);
        Require(image && image->width == width && image->height == height);

        const auto damaged = Mutate(png, rng);
        (void)Decoder::Png::ParseHeader(damaged);
        if (Decoder::Png::Decode(damaged)) ++decoded;
    }
    std::vector<std::uint8_t> noise(256);
    for (std::size_t i = 0; i < 20000; ++i) {
        for (auto& byte : noise) byte = static_cast<std::uint8_t>(rng.Next());
        (void)Decoder::Png::ParseHeader({noise.data(), static_cast<std::size_t>(rng.Below(noise.size() + 1))});
    }
    std::printf("png fuzz: %zu cases, %zu damaged images still decoded\n", cases, decoded);
    return 0;
}
