#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

struct GuestOption {
    const char* name;
    int hasArgument;
    int* flag;
    int value;
};

extern "C" {
int APS5_VABI getopt_nid_postfix(int, char* const*, const char*);
int APS5_VABI getopt_long_nid_postfix(int, char* const*, const char*, const GuestOption*, int*);
extern int opterr_nid_postfix;
extern int optind_nid_postfix;
extern int optopt_nid_postfix;
extern int optreset_nid_postfix;
extern char* optarg_nid_postfix;
}

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

void Require(bool value, const char* what) {
    if (!value) {
        std::fprintf(stderr, "getopt fuzz failed: %s\n", what);
        std::abort();
    }
}

// Cadena de tamaño exacto en el heap (una lectura o escritura pasada del final la detecta ASan).
char* Heap(const std::string& text) {
    auto* memory = static_cast<char*>(std::malloc(text.size() + 1));
    std::memcpy(memory, text.c_str(), text.size() + 1);
    return memory;
}

std::string Text(Rng& rng, const char* alphabet, std::size_t maximum) {
    const std::size_t length = rng.Below(maximum + 1);
    std::string result;
    const std::size_t size = std::strlen(alphabet);
    for (std::size_t i = 0; i < length; ++i) result.push_back(alphabet[rng.Below(size)]);
    return result;
}

}  // namespace

int main() {
    const char* env = std::getenv("ANYPS5_GETOPT_FUZZ_CASES");
    const std::size_t cases = env ? std::strtoull(env, nullptr, 10) : 20000;
    Rng rng{0x6765746f70740001ull};
    std::size_t options = 0;
    for (std::size_t i = 0; i < cases; ++i) {
        const int count = static_cast<int>(rng.Below(9));
        std::vector<char*> storage;
        for (int k = 0; k < count; ++k) {
            const std::string argument = rng.Below(3) == 0 ? Text(rng, "-=ab:xy", 6) : "-" + Text(rng, "-=abcxy:", 7);
            storage.push_back(Heap(argument));
        }
        auto** arguments = static_cast<char**>(std::malloc(sizeof(char*) * (storage.size() + 1)));
        std::copy(storage.begin(), storage.end(), arguments);
        arguments[storage.size()] = nullptr;

        const std::string prefix = rng.Below(4) == 0 ? (rng.Below(2) ? "+" : "-") : "";
        char* const optionString = Heap(prefix + Text(rng, "abcxy:", 8));

        std::vector<std::string> names;
        for (std::size_t k = rng.Below(4); k > 0; --k) names.push_back(Text(rng, "abcxy-", 5));
        std::vector<GuestOption> longOptions;
        std::vector<int> flags(names.size() + 1);
        for (std::size_t k = 0; k < names.size(); ++k)
            longOptions.push_back({Heap(names[k]), static_cast<int>(rng.Below(3)), rng.Below(2) ? &flags[k] : nullptr, static_cast<int>(rng.Below(100))});
        longOptions.push_back({nullptr, 0, nullptr, 0});

        opterr_nid_postfix = 0;
        optind_nid_postfix = static_cast<int>(rng.Below(2));
        optreset_nid_postfix = 1;
        int longIndex = -1;
        // Una sola función por caso: cada una guarda su propio estado y se reinicia con optreset.
        const bool shortOnly = rng.Below(3) == 0;
        for (int step = 0; step < 64; ++step) {
            optarg_nid_postfix = nullptr;  // POSIX: solo es válido si la opción devuelta lleva argumento
            const int result = shortOnly ? getopt_nid_postfix(count, arguments, optionString)
                                                  : getopt_long_nid_postfix(count, arguments, optionString, longOptions.data(), &longIndex);
            if (result == -1) break;
            ++options;
            Require(optind_nid_postfix >= 0 && optind_nid_postfix <= count + 1, "optind out of range");
            if (optarg_nid_postfix != nullptr) {
                // El argumento de la opción debe apuntar dentro de alguna de las cadenas de argv.
                bool inside = false;
                for (char* original : storage) inside |= optarg_nid_postfix >= original && optarg_nid_postfix <= original + std::strlen(original);
                Require(inside, "optarg points outside the argument strings");
            }
        }
        // getopt reordena argv pero nunca puede perder ni duplicar un argumento.
        std::vector<char*> after(arguments, arguments + count);
        std::vector<char*> before = storage;
        std::sort(after.begin(), after.end());
        std::sort(before.begin(), before.end());
        Require(after == before, "arguments are no longer a permutation of the originals");
        Require(arguments[count] == nullptr, "argv terminator overwritten");

        for (char* argument : storage) std::free(argument);
        std::free(arguments);
        std::free(optionString);
        for (std::size_t k = 0; k < names.size(); ++k) std::free(const_cast<char*>(longOptions[k].name));
    }
    std::printf("getopt fuzz: %zu cases, %zu options parsed\n", cases, options);
    return 0;
}
