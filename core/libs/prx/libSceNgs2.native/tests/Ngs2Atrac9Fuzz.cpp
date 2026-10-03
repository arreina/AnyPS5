#include "libatrac9.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

struct DecoderDeleter {
    void operator()(void* handle) const { Atrac9ReleaseHandle(handle); }
};
using Decoder = std::unique_ptr<void, DecoderDeleter>;

static std::size_t CaseCount() {
    const char* value = std::getenv("ANYPS5_ATRAC9_FUZZ_CASES");
    if (value == nullptr) return 20000;
    return static_cast<std::size_t>(std::strtoull(value, nullptr, 10));
}

struct Config {
    std::uint8_t bytes[4];
    Atrac9CodecInfo info;
};

static std::vector<Config> Configs(std::mt19937_64& random) {
    std::vector<Config> configs;
    for (int attempt = 0; attempt < 4096 && configs.size() < 32; attempt++) {
        Config config{{0xFE, static_cast<std::uint8_t>(random()), static_cast<std::uint8_t>(random()), static_cast<std::uint8_t>(random())}, {}};
        Decoder decoder(Atrac9GetHandle());
        if (Atrac9InitDecoder(decoder.get(), config.bytes) != 0 || Atrac9GetCodecInfo(decoder.get(), &config.info) != 0) continue;
        if (config.info.channels <= 0 || config.info.framesInSuperframe <= 0 || config.info.superframeSize <= 0) continue;
        configs.push_back(config);
    }
    return configs;
}

int main() {
    std::mt19937_64 random(20261003);
    auto configs = Configs(random);
    configs.push_back({{0xFE, 0x70, 0x07, 0xF0}, {}});
    {
        Decoder decoder(Atrac9GetHandle());
        if (Atrac9InitDecoder(decoder.get(), configs.back().bytes) != 0 || Atrac9GetCodecInfo(decoder.get(), &configs.back().info) != 0) {
            std::fprintf(stderr, "the reference ATRAC9 config was rejected\n");
            return 1;
        }
    }
    const auto cases = CaseCount();
    std::size_t frames = 0;
    std::size_t rejected = 0;
    for (std::size_t index = 0; index < cases; index++) {
        const auto& config = configs[random() % configs.size()];
        const auto superframeSize = static_cast<std::size_t>(config.info.superframeSize);
        Decoder decoder(Atrac9GetHandle());
        std::uint8_t bytes[4] = {config.bytes[0], config.bytes[1], config.bytes[2], config.bytes[3]};
        if (Atrac9InitDecoder(decoder.get(), bytes) != 0) return 1;
        std::vector<float> pcm(static_cast<std::size_t>(config.info.frameSamples) * static_cast<std::size_t>(config.info.channels));
        const auto superframes = 1 + random() % 4;
        for (std::size_t superframe = 0; superframe < superframes; superframe++) {
            auto data = std::make_unique<std::uint8_t[]>(superframeSize);
            for (std::size_t i = 0; i < superframeSize; i++) data[i] = static_cast<std::uint8_t>(random());
            std::size_t offset = 0;
            bool failed = false;
            for (int frame = 0; frame < config.info.framesInSuperframe; frame++) {
                int used = 0;
                const int status = Atrac9DecodeF32(decoder.get(), data.get() + offset, static_cast<int>(superframeSize - offset), pcm.data(), &used, 0);
                if (status != 0 || used <= 0 || offset + static_cast<std::size_t>(used) > superframeSize) {
                    failed = true;
                    break;
                }
                offset += static_cast<std::size_t>(used);
                frames++;
            }
            if (failed) {
                rejected++;
                break;
            }
        }
    }
    std::printf("ATRAC9 fuzz: %zu configs, %zu cases, %zu frames decoded, %zu cases rejected\n", configs.size(), cases, frames, rejected);
    return 0;
}
