#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4Opcodes.hpp"
#include "prx/libSceAgcDriver/Execution/include/QueueState.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::uint32_t> makePacket(std::uint32_t opcode, std::initializer_list<std::uint32_t> payload, std::uint32_t flags = 0) {
    std::vector<std::uint32_t> result{0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u) | flags};
    result.insert(result.end(), payload);
    return result;
}

const std::vector<std::vector<std::uint32_t>>& seeds() {
    static const std::vector<std::vector<std::uint32_t>> result{
        makePacket(0x79, {0x242, 4}),
        makePacket(0x76, {0x200, 1, 2, 3}),
        makePacket(0x69, {0x10, 7}),
        makePacket(0x7a, {0x20000243u, 1}),
        makePacket(0x12, {0}),
        makePacket(0x13, {64}),
        makePacket(0x26, {0x1000, 0}),
        makePacket(0x2a, {1}),
        makePacket(0x2f, {2}),
        makePacket(0x11, {1, 0x1000, 0}, 2),
        makePacket(0x81, {0x100, 1, 2, 3}),
        makePacket(0x59, {0}),
        makePacket(0x42, {0}),
        makePacket(0x46, {0x407}),
        makePacket(0x46, {0x16}),
        makePacket(0x58, {0, 1, 0, 0x10, 0, 10}),
        makePacket(0x15, {1, 1, 1, 0x41}),
        makePacket(0x27, {16, 0, 0, 3, 0}),
        makePacket(0x2d, {3, 2}),
        makePacket(0x10, {0x00414243u, 0}, 0x0b << 2),
        makePacket(0x10, {0}, 0x0c << 2),
        makePacket(0x10, {1, 0}, 0x1a << 2),
        makePacket(0x10, {2, 0}, 0x1a << 2),
        makePacket(0x10, {0}, 0x09 << 2),
        makePacket(0x10, {0x12345678u}),
        {0x80000000u},
    };
    return result;
}

constexpr std::uint32_t Boundaries[] = {0, 1, 2, 3, 4, 0x41, 0xff, 0x100, 0x2fff, 0x3000, 0xbff0, 0xbffc, 0xc000, 0xfffc, 0xffff, 0x10000, 0x3ffffff, 0x7fffffff, 0x80000000u, 0xfffffffcu, 0xffffffffu};

std::uint32_t word(std::mt19937_64& random) {
    switch (random() % 4) {
        case 0: return static_cast<std::uint32_t>(random() % 16);
        case 1: return Boundaries[random() % std::size(Boundaries)] + static_cast<std::uint32_t>(random() % 3) - 1u;
        case 2: return Boundaries[random() % std::size(Boundaries)];
        default: return static_cast<std::uint32_t>(random());
    }
}

std::size_t caseCount() {
    const char* value = std::getenv("ANYPS5_PM4_FUZZ_CASES");
    if (value == nullptr) return 20000;
    return static_cast<std::size_t>(std::strtoull(value, nullptr, 10));
}

std::vector<std::uint32_t> generate(std::mt19937_64& random) {
    const auto& opcodes = AgcDriver::Pm4::Opcodes;
    const auto& opcode = opcodes[random() % std::size(opcodes)];
    std::vector<std::uint32_t> packet(1 + random() % (random() % 8 == 0 ? 40 : 12));
    for (auto& value : packet) value = word(random);
    packet[0] = 0xc0000000u | (opcode.value << 8u) | static_cast<std::uint32_t>(random() % 8);
    if (opcode.value == 0x10) packet[0] |= static_cast<std::uint32_t>(random() % 0x20) << 2u;
    if (packet.size() > 1) packet[0] |= static_cast<std::uint32_t>(packet.size() - 2) << 16u;
    return packet;
}

void mutate(std::mt19937_64& random, std::vector<std::uint32_t>& packet) {
    const auto edits = 1 + random() % 3;
    for (std::size_t edit = 0; edit < edits; ++edit) {
        const auto index = random() % packet.size();
        switch (random() % 6) {
            case 0: packet[index] ^= 1u << (random() % 32); break;
            case 1: if (index != 0) packet[index] = word(random); break;
            case 2: packet[index] = static_cast<std::uint32_t>(random() % 0x10000); break;
            case 3: packet.insert(packet.end(), 1 + random() % 4, word(random)); break;
            case 4: if (packet.size() > 1) packet.pop_back(); break;
            case 5: packet[0] = (packet[0] & 0xc000ffffu) | (static_cast<std::uint32_t>(random() % 16) << 16u); break;
        }
    }
    if (packet.size() > 1 && random() % 4 != 0) packet[0] = (packet[0] & 0xc000ffffu) | (static_cast<std::uint32_t>(packet.size() - 2) << 16u);
}

}

int main() {
    try {
        const auto cases = caseCount();
        std::mt19937_64 random(20261002);
        AgcDriver::QueueState state;
        std::size_t accepted = 0;
        std::size_t executed = 0;
        for (std::size_t index = 0; index < cases; ++index) {
            std::vector<std::uint32_t> packet;
            if (random() % 2 == 0) packet = seeds()[random() % seeds().size()];
            else packet = generate(random);
            mutate(random, packet);
            const auto queue = static_cast<std::uint32_t>(random() % 2);
            static_cast<void>(AgcDriver::Pm4::Name(packet[0]));
            static_cast<void>(AgcDriver::Pm4::UnsupportedReason(packet[0]));
            try {
                AgcDriver::Pm4::Validate(packet, queue);
            } catch (const std::runtime_error&) {
                continue;
            }
            ++accepted;
            static_cast<void>(AgcDriver::Pm4::IsTagMarker(packet));
            if (((packet[0] >> 8u) & 0xffu) == 0x58) static_cast<void>(AgcDriver::Pm4::UsesGpuCacheBarrier(packet));
            if (AgcDriver::Pm4::AccessesMemory(packet[0]) || AgcDriver::Pm4::FillerPacket(packet[0])) continue;
            try {
                AgcDriver::Pm4::Execute(packet, state);
                ++executed;
            } catch (const std::runtime_error&) {
            }
        }
        std::printf("PM4 fuzz: %zu cases, %zu accepted, %zu executed\n", cases, accepted, executed);
        LibcRunShutdown_nid_postfix();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        try { LibcRunShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
