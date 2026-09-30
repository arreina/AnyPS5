#include <codegen/CodegenException.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint8_t kInterestingBytes[] = {
    0x0f, 0x38, 0x3a, 0x66, 0x67, 0xf2, 0xf3, 0xf0, 0x2e, 0x3e, 0x26, 0x36, 0x64, 0x65,
    0x40, 0x41, 0x48, 0x4c, 0x4f, 0xc4, 0xc5, 0x62, 0x8f, 0xe8, 0xe9, 0xeb, 0xff, 0xc3,
    0xcc, 0x70, 0x80, 0x0b, 0x05, 0x25, 0x04, 0x44, 0x84, 0x14, 0xa0, 0xa1, 0xb8, 0xc7,
    0x9a, 0xea, 0xc2, 0xca, 0x68, 0x6a, 0xd8, 0xdf, 0x78, 0x79, 0xae, 0x01, 0x00,
};

std::size_t caseCount() {
    const char* value = std::getenv("ANYPS5_DECODER_FUZZ_CASES");
    return value == nullptr ? 20000 : std::stoul(value);
}

void check(const Codegen::X64InstructionDecoder& decoder, const std::uint8_t* data, const std::size_t size, const std::size_t caseIndex) {
    try {
        const auto info = decoder.DecodeInstruction(data, size);
        if (info.Length == 0 || info.Length > size)
            throw std::runtime_error("case " + std::to_string(caseIndex) + ": DecodeInstruction returned length " + std::to_string(info.Length) + " for " + std::to_string(size) + " bytes");
    } catch (const Codegen::CodegenException&) {
    }
    try {
        const auto length = decoder.Decode(data, size);
        if (length == 0 || length > size)
            throw std::runtime_error("case " + std::to_string(caseIndex) + ": Decode returned length " + std::to_string(length) + " for " + std::to_string(size) + " bytes");
    } catch (const Codegen::CodegenException&) {
    }
}

}

int main() {
    const Codegen::X64InstructionDecoder decoder;
    std::mt19937_64 random(20260930);
    const std::size_t cases = caseCount();
    try {
        check(decoder, nullptr, 0, 0);
        for (std::size_t caseIndex = 1; caseIndex <= cases; ++caseIndex) {
            const std::size_t size = random() % 18;
            auto bytes = std::make_unique<std::uint8_t[]>(size);
            for (std::size_t i = 0; i < size; ++i) {
                bytes[i] = random() % 3 == 0
                    ? static_cast<std::uint8_t>(random())
                    : kInterestingBytes[random() % sizeof(kInterestingBytes)];
            }
            for (std::size_t prefix = 0; prefix <= size; ++prefix) {
                auto exact = std::make_unique<std::uint8_t[]>(prefix);
                for (std::size_t i = 0; i < prefix; ++i) exact[i] = bytes[i];
                check(decoder, exact.get(), prefix, caseIndex);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
    std::cout << "X64 instruction decoder fuzz tests passed (" << cases << " cases)\n";
    return 0;
}
