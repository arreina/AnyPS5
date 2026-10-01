#include "RdnaDecoder/RdnaInstructionDecoder.hpp"
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ShaderRecompiler;

constexpr std::uint32_t kEncodingPrefixes[] = {
    0x80000000u, 0xb0000000u, 0xbe800000u, 0xbf000000u, 0xbf800000u, 0xf4000000u, 0xf4400000u,
    0x7c000000u, 0x7e000000u, 0x00000000u, 0x40000000u, 0xd4000000u, 0xd5000000u, 0xd6000000u,
    0xd7000000u, 0xcc000000u, 0xc8000000u, 0xd8000000u, 0xdc000000u, 0xe0000000u, 0xe8000000u,
    0xf0000000u, 0xf8000000u, 0xfc000000u,
};

constexpr std::uint32_t kSpecialWords[] = {
    0x000000ffu, 0x000000fau, 0x000000f9u, 0x000000e9u, 0x000000eau, 0x000000fbu, 0x000000fcu,
    0x000000fdu, 0x0000007cu, 0x0000006au, 0x0000007eu, 0x00000080u, 0x000000c1u, 0x000000f0u,
    0x00000000u, 0xffffffffu, 0x7fffffffu, 0x80000000u,
};

std::size_t caseCount() {
    const char* value = std::getenv("ANYPS5_RDNA_FUZZ_CASES");
    return value == nullptr ? 20000 : std::stoul(value);
}

std::uint32_t instructionWord(std::mt19937_64& random) {
    const auto prefix = kEncodingPrefixes[random() % std::size(kEncodingPrefixes)];
    const auto prefixBits = prefix >= 0xf0000000u ? 6u : prefix >= 0xbe800000u ? 9u : prefix >= 0xc8000000u ? 6u : prefix == 0u ? 1u : 7u;
    const auto mask = 0xffffffffu >> prefixBits;
    return prefix | (static_cast<std::uint32_t>(random()) & mask);
}

std::uint32_t followingWord(std::mt19937_64& random) {
    const auto kind = random() % 4;
    if (kind == 0) return kSpecialWords[random() % std::size(kSpecialWords)];
    if (kind == 1) return instructionWord(random);
    return static_cast<std::uint32_t>(random());
}

void check(const RdnaInstructionDecoder& decoder, std::span<const std::uint32_t> code, std::size_t caseIndex) {
    try {
        const auto program = decoder.Decode(code);
        std::size_t words = 0;
        for (const auto& instruction : program.instructions) {
            if (instruction.wordCount == 0) throw std::logic_error("case " + std::to_string(caseIndex) + ": an instruction has no words");
            words += instruction.wordCount;
        }
        if (words > code.size()) throw std::logic_error("case " + std::to_string(caseIndex) + ": the instructions extend past the code");
        static_cast<void>(RdnaProgramToString(program));
    } catch (const std::logic_error& error) {
        if (std::string_view(error.what()).starts_with("case ")) throw;
    } catch (const std::exception&) {
    }
    try {
        static_cast<void>(DecodeRdnaFrontProgram(code));
    } catch (const std::exception&) {
    }
}

}

int main() {
    const RdnaInstructionDecoder decoder;
    std::mt19937_64 random(20261001);
    const auto cases = caseCount();
    try {
        check(decoder, {}, 0);
        for (std::size_t caseIndex = 1; caseIndex <= cases; ++caseIndex) {
            const std::size_t size = 1 + random() % 24;
            auto words = std::make_unique<std::uint32_t[]>(size);
            std::size_t index = 0;
            while (index < size) {
                words[index++] = instructionWord(random);
                const auto extra = random() % 4;
                for (std::size_t i = 0; i < extra && index < size; ++i) words[index++] = followingWord(random);
            }
            for (std::size_t prefix = 1; prefix <= size; ++prefix) {
                auto exact = std::make_unique<std::uint32_t[]>(prefix);
                for (std::size_t i = 0; i < prefix; ++i) exact[i] = words[i];
                check(decoder, std::span<const std::uint32_t>(exact.get(), prefix), caseIndex);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
    std::cout << "RDNA decoder fuzz tests passed (" << cases << " cases)\n";
    return 0;
}
