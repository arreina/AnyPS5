#include "BdaAbi.hpp"
#include "Recompiler.hpp"
#include <spirv/unified1/spirv.hpp>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t AliasCode[] = {
    0x34020082, 0x34060084, 0xe0302000, 0x80000401, 0xe0302004, 0x80000501, 0xe0302008, 0x80000601,
    0x7e1402ff, 0xabcd0000, 0x7e1602ff, 0xabcd0000, 0x7e1802ff, 0xabcd0000, 0xbf8c3f70, 0xd539020a,
    0x20020b04, 0xd53a010b, 0x40020b04, 0xd535010c, 0x20020b04, 0xd58b000d, 0x10000105, 0xd58b010e,
    0x20000104, 0xd591000f, 0x08000106, 0xd5920010, 0x18000106, 0xd5938011, 0x00000106, 0xd5940012,
    0x00000106, 0xe0702000, 0x80010a03, 0xe0702004, 0x80010b03, 0xe0702008, 0x80010c03, 0xe070200c,
    0x80010d03, 0xe0702010, 0x80010e03, 0xe0702014, 0x80010f03, 0xe0702018, 0x80011003, 0xe070201c,
    0x80011103, 0xe0702020, 0x80011203, 0xbf810000,
};

constexpr std::uint32_t MixCode[] = {
    0x34020082, 0x34060084, 0xe0302000, 0x80000401, 0xe0302004, 0x80000501, 0xe0302008, 0x80000601,
    0xe030200c, 0x80000701, 0x7e2002ff, 0xabcd0000, 0x7e2202ff, 0x0000abcd, 0xbf8c3f70, 0xcc20400a,
    0x1c1a0b04, 0xcc20780b, 0x1c1a0b04, 0xcc20400c, 0x941a0af2, 0xcc20700d, 0x941a0af2, 0xcc20100e,
    0x141e0907, 0xcc204a0f, 0x1c1a0b04, 0xcc214010, 0x1c1a0b04, 0xcc227811, 0x1c1a0b04, 0xcc203812,
    0x041e0f07, 0xe0702000, 0x80010a03, 0xe0702004, 0x80010b03, 0xe0702008, 0x80010c03, 0xe070200c,
    0x80010d03, 0xe0702010, 0x80010e03, 0xe0702014, 0x80010f03, 0xe0702018, 0x80011003, 0xe070201c,
    0x80011103, 0xe0702020, 0x80011203, 0xbf810000,
};

constexpr std::uint32_t SdwaCode[] = {
    0x34020082, 0x34060083, 0xe0302000, 0x80000401, 0xe0302004, 0x80000501, 0xe0302008, 0x80000601,
    0x7e1402ff, 0xabcd1234, 0x7e1602ff, 0xabcd1234, 0x7e1802ff, 0xabcd1234, 0x7e1a02ff, 0xabcd1234,
    0x7e1c02ff, 0xabcd1234, 0x7e1e02ff, 0xabcd1234, 0xbf8c3f70, 0x7e1414f9, 0x00060604, 0x7e161504,
    0x7e1814f9, 0x00061505, 0x7e1a14f9, 0x00060604, 0x7e1c14f9, 0x00060605, 0xd746000d, 0x0435210e,
    0x641e0cf9, 0x05040606, 0xe0702000, 0x80010a03, 0xe0702004, 0x80010b03, 0xe0702008, 0x80010c03,
    0xe070200c, 0x80010d03, 0xe0702010, 0x80010f03, 0xbf810000,
};

constexpr std::uint32_t CacheControlCode[] = {
    0x34020082, 0xbf930000, 0xbf940001, 0xe0302000, 0x80000401, 0xf4840000, 0x00000000, 0xf4800000,
    0x00000000, 0xf47c0000, 0x00000000, 0xe1c80000, 0x00000000, 0xe1c40000, 0x00000000, 0xbfa20000,
    0xbf8c3f70, 0xbfa80001, 0x4a080881, 0xbf950001, 0xe0702000, 0x80010401, 0xbf810000,
};

constexpr std::span<const std::uint32_t> kSeeds[] = {AliasCode, MixCode, SdwaCode, CacheControlCode};

constexpr std::uint32_t kInstructionPrefixes[] = {
    0x80000000u, 0xb0000000u, 0xbe800000u, 0xbf000000u, 0xbf800000u, 0xf4000000u, 0x7c000000u, 0x7e000000u,
    0x00000000u, 0x34000000u, 0xd4000000u, 0xd5000000u, 0xd6000000u, 0xcc000000u, 0xdc000000u, 0xe0000000u,
    0xe0700000u, 0xe0300000u, 0xe8000000u, 0xf0000000u,
};

const std::uint32_t kCapabilities[] = {
    spv::CapabilityGroupNonUniform, spv::CapabilityGroupNonUniformBallot, spv::CapabilityGroupNonUniformShuffle,
    spv::CapabilitySignedZeroInfNanPreserve, 4448, 11, 5347, 5283, 3, spv::CapabilityImageGatherExtended,
    spv::CapabilityMinLod, spv::CapabilityStorageImageWriteWithoutFormat, spv::CapabilityStorageImageReadWithoutFormat,
    spv::CapabilitySampledImageArrayDynamicIndexing, spv::CapabilityStorageImageArrayDynamicIndexing,
    spv::CapabilityShaderNonUniform, spv::CapabilitySampledImageArrayNonUniformIndexing,
    spv::CapabilityStorageImageArrayNonUniformIndexing,
};

const std::string_view kExtensions[] = {
    "SPV_EXT_descriptor_indexing", "SPV_KHR_8bit_storage", "SPV_KHR_float_controls", "SPV_KHR_physical_storage_buffer",
};

constexpr std::uint32_t kThreads = 64;
alignas(256) std::array<std::uint32_t, kThreads * 4> input{};
alignas(256) std::array<std::uint32_t, kThreads * 16> output{};

std::size_t caseCount() {
    const char* value = std::getenv("ANYPS5_RECOMPILER_FUZZ_CASES");
    return value == nullptr ? 300 : std::stoul(value);
}

std::array<std::uint32_t, 4> bufferDescriptor(const void* data, std::uint32_t count) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (4u << 16u), count, 0x01016facu};
}

void mutate(std::mt19937_64& random, std::vector<std::uint32_t>& code) {
    const auto mutations = 1 + random() % 3;
    for (std::uint64_t i = 0; i < mutations && code.size() > 1; ++i) {
        const auto position = random() % (code.size() - 1);
        switch (random() % 6) {
            case 0: code[position] = kInstructionPrefixes[random() % std::size(kInstructionPrefixes)] | (static_cast<std::uint32_t>(random()) & 0x01ffffffu); break;
            case 1: code[position] ^= 1u << (random() % 32); break;
            case 2: code[position] = (code[position] & 0xffff0000u) | (static_cast<std::uint32_t>(random()) & 0xffffu); break;
            case 3: code[position] = (code[position] & ~0x1ffu) | static_cast<std::uint32_t>(random() % 0x200u); break;
            case 4: code.erase(code.begin() + static_cast<std::ptrdiff_t>(position)); break;
            default: code.insert(code.begin() + static_cast<std::ptrdiff_t>(position), code[random() % code.size()]); break;
        }
    }
}

}

int main() {
    using namespace ShaderRecompiler;
    std::vector<std::uint32_t> userData(8, 0u);
    const auto in = bufferDescriptor(input.data(), static_cast<std::uint32_t>(input.size()));
    const auto out = bufferDescriptor(output.data(), static_cast<std::uint32_t>(output.size()));
    std::copy(in.begin(), in.end(), userData.begin());
    std::copy(out.begin(), out.end(), userData.begin() + 4);
    const SpirvTarget target{0x00401000u, 0x00010300u, 32, BdaAbi::Version, kCapabilities, kExtensions, false, {1024, 1024, 64}, 1024, 32768, {}, {}};
    const ShaderComputeStageInfo compute{{kThreads, 1, 1}, 0, {false, false, false}, false, 1};
    std::mt19937_64 random(20261002);
    const auto cases = caseCount();
    std::size_t compiled = 0;
    std::size_t invalid = 0;
    for (std::size_t index = 0; index <= cases; ++index) {
        const auto seed = kSeeds[random() % std::size(kSeeds)];
        std::vector<std::uint32_t> code(seed.begin(), seed.end());
        if (index != 0) mutate(random, code);
        const std::span<const std::uint32_t> span(code);
        const std::array<MemoryRegion, 1> memory{{reinterpret_cast<std::uintptr_t>(span.data()), std::as_bytes(span)}};
        RecompileRequest request{
            {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(span.data()), span, 0, {}},
            {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
            target,
            {0, 0, 0, 128}
        };
        request.useCache = false;
        try {
            static_cast<void>(Recompile(request));
            ++compiled;
        } catch (const std::exception& error) {
            if (std::string_view(error.what()).find("SPIR-V validation") != std::string_view::npos) {
                ++invalid;
                std::fprintf(stderr, "case %zu produced invalid SPIR-V: %.400s\n", index, error.what());
            }
        }
    }
    std::printf("recompiler fuzz: %zu cases, %zu compiled, %zu invalid SPIR-V\n", cases, compiled, invalid);
    return invalid == 0 ? 0 : 1;
}
