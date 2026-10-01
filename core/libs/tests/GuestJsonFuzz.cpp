#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>

struct Value { void* node; };
struct String { void* text; };

extern "C" {
void APS5_VABI _ZN3sce4Json6StringC1Ev(String*);
void APS5_VABI _ZN3sce4Json6StringD1Ev(String*);
const char* APS5_VABI _ZNK3sce4Json6String5c_strEv(const String*);
std::size_t APS5_VABI _ZNK3sce4Json6String6lengthEv(const String*);
void APS5_VABI _ZN3sce4Json5ValueC1Ev(Value*);
void APS5_VABI _ZN3sce4Json5ValueD1Ev(Value*);
int APS5_VABI _ZN3sce4Json5Value9serializeERNS0_6StringE(Value*, String*);
int APS5_VABI _ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(Value*, const char*, std::size_t);
}

namespace {

std::mt19937_64 generator(20261001);

std::size_t caseCount() {
    const char* value = std::getenv("ANYPS5_JSON_FUZZ_CASES");
    return value == nullptr ? 5000 : std::stoul(value);
}

const char* const kNumbers[] = {
    "0", "-0", "1", "-1", "1.5", "-2.5e-3", "1e308", "1e309", "-1e309", "4.9e-324", "1e-400",
    "9223372036854775807", "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
    "18446744073709551615", "18446744073709551616", "123456789012345678901234567890", "0.0000001",
    "1E+2", "1e", "-", "01", "1.", ".5", "+1",
};

const char* const kEscapes[] = {
    "\\\"", "\\\\", "\\/", "\\b", "\\f", "\\n", "\\r", "\\t", "\\u0000", "\\u001f", "\\u00e9",
    "\\u20ac", "\\ud83d\\ude00", "\\ud800", "\\udc00", "\\ud800x", "\\ud800\\u0041", "\\uFFFF",
    "\\u12", "\\x", "\\",
};

std::string text(std::size_t limit) {
    std::string out = "\"";
    const auto length = generator() % limit;
    for (std::size_t i = 0; i < length; ++i) {
        const auto kind = generator() % 5;
        if (kind == 0) out += kEscapes[generator() % std::size(kEscapes)];
        else if (kind == 1) out += static_cast<char>(0x80 + generator() % 0x80);
        else out += static_cast<char>('a' + generator() % 26);
    }
    return out + "\"";
}

std::string document(std::size_t depth) {
    const auto kind = depth > 8 ? generator() % 5 : generator() % 7;
    switch (kind) {
        case 0: return "null";
        case 1: return generator() % 2 == 0 ? "true" : "false";
        case 2: return kNumbers[generator() % std::size(kNumbers)];
        case 3: case 4: return text(12);
        case 5: {
            std::string out = "[";
            const auto count = generator() % 5;
            for (std::size_t i = 0; i < count; ++i) out += (i == 0 ? "" : ",") + document(depth + 1);
            return out + "]";
        }
        default: {
            std::string out = "{";
            const auto count = generator() % 5;
            for (std::size_t i = 0; i < count; ++i) out += (i == 0 ? "" : ",") + text(6) + ":" + document(depth + 1);
            return out + "}";
        }
    }
}

std::string nested(std::size_t depth) {
    std::string out;
    for (std::size_t i = 0; i < depth; ++i) out += generator() % 2 == 0 ? "[" : "{\"k\":";
    std::string closing;
    for (std::size_t i = out.size(); i > 0;) {
        if (out[i - 1] == '[') { closing += "]"; i -= 1; }
        else { closing += "}"; i -= 5; }
    }
    return out + "0" + closing;
}

void corrupt(std::string& value) {
    const auto mutations = 1 + generator() % 3;
    for (std::uint64_t i = 0; i < mutations && !value.empty(); ++i) {
        const auto position = generator() % value.size();
        switch (generator() % 5) {
            case 0: value.resize(position); break;
            case 1: value[position] = static_cast<char>(generator()); break;
            case 2: value.insert(position, 1, "[]{}\",:\\0-e"[generator() % 12]); break;
            case 3: value.erase(position, 1); break;
            default: value.insert(position, kEscapes[generator() % std::size(kEscapes)]); break;
        }
    }
}

std::string serialize(Value& value) {
    String out{};
    _ZN3sce4Json6StringC1Ev(&out);
    std::string result;
    if (_ZN3sce4Json5Value9serializeERNS0_6StringE(&value, &out) == 0) result.assign(_ZNK3sce4Json6String5c_strEv(&out), _ZNK3sce4Json6String6lengthEv(&out));
    _ZN3sce4Json6StringD1Ev(&out);
    return result;
}

}

int main() {
    const auto cases = caseCount();
    std::size_t parsed = 0;
    for (std::size_t index = 0; index < cases; ++index) {
        std::string input = generator() % 10 == 0 ? nested(500 + generator() % 30) : document(0);
        if (generator() % 2 == 0) corrupt(input);
        const auto buffer = std::make_unique<char[]>(input.size() + 1);
        for (std::size_t i = 0; i < input.size(); ++i) buffer[i] = input[i];
        buffer[input.size()] = '\0';
        Value root{};
        _ZN3sce4Json5ValueC1Ev(&root);
        if (_ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(&root, buffer.get(), input.size()) == 0) {
            ++parsed;
            const auto first = serialize(root);
            Value again{};
            _ZN3sce4Json5ValueC1Ev(&again);
            if (_ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(&again, first.c_str(), first.size()) != 0 || serialize(again) != first) {
                std::fprintf(stderr, "case %zu: the serialized document does not round-trip\ninput: %s\nfirst: %s\n", index, input.c_str(), first.c_str());
                return 1;
            }
            _ZN3sce4Json5ValueD1Ev(&again);
        }
        _ZN3sce4Json5ValueD1Ev(&root);
    }
    std::printf("JSON fuzz tests passed (%zu cases, %zu parsed)\n", cases, parsed);
    return 0;
}
