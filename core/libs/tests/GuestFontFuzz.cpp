#include "prx/libSceFont/include/FontDriver.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
int APS5_VABI sceFontMemoryInit(FontMemory*, void*, std::uint32_t, const FontMemoryInterface*, void*, FontMemoryDestroyFunction, void*);
int APS5_VABI sceFontMemoryTerm(FontMemory*);
int APS5_VABI sceFontCreateLibrary(const FontMemory*, const void*, FontLibrary*);
int APS5_VABI sceFontDestroyLibrary(FontLibrary*);
int APS5_VABI sceFontCreateRenderer(const FontMemory*, const void*, FontRenderer*);
int APS5_VABI sceFontDestroyRenderer(FontRenderer*);
int APS5_VABI sceFontGetPixelResolution(FontLibrary, std::uint32_t*);
int APS5_VABI sceFontAttachDeviceCacheBuffer(FontLibrary, void*, std::uint32_t);
int APS5_VABI sceFontClearDeviceCache(FontLibrary);
int APS5_VABI sceFontDettachDeviceCacheBuffer(FontLibrary, void**, std::uint32_t*);
int APS5_VABI sceFontSupportSystemFonts(FontLibrary);
int APS5_VABI sceFontSupportExternalFonts(FontLibrary, std::uint32_t, std::uint32_t);
int APS5_VABI sceFontOpenFontSet(FontLibrary, std::uint32_t, std::uint32_t, const FontOpenDetail*, FontHandle*);
int APS5_VABI sceFontOpenFontMemory(FontLibrary, const void*, std::uint32_t, const FontOpenDetail*, FontHandle*);
int APS5_VABI sceFontCloseFont(FontHandle);
int APS5_VABI sceFontSetScriptLanguage(FontHandle, int, int);
int APS5_VABI sceFontGetScriptLanguage(FontHandle, int, int*);
int APS5_VABI sceFontSetTypographicDesign(FontHandle, int, int);
int APS5_VABI sceFontGetTypographicDesign(FontHandle, int, int*);
int APS5_VABI sceFontSetResolutionDpi(FontHandle, std::uint32_t, std::uint32_t);
int APS5_VABI sceFontGetResolutionDpi(FontHandle, std::uint32_t*, std::uint32_t*);
int APS5_VABI sceFontSetScalePixel(FontHandle, float, float);
int APS5_VABI sceFontBindRenderer(FontHandle, FontRenderer);
int APS5_VABI sceFontUnbindRenderer(FontHandle);
int APS5_VABI sceFontSetupRenderScalePixel(FontHandle, float, float);
int APS5_VABI sceFontSetupRenderScalePoint(FontHandle, float, float);
int APS5_VABI sceFontGetKerning(FontHandle, std::uint32_t, std::uint32_t, FontKerning*);
int APS5_VABI sceFontGetFontGlyphsCount(FontHandle, std::uint32_t*);
int APS5_VABI sceFontGetCharGlyphCode(FontHandle, std::uint32_t, std::uint32_t*);
int APS5_VABI sceFontGetFontResolution(FontHandle, std::uint32_t*, float*);
int APS5_VABI sceFontGetRenderScaledKerning(FontHandle, std::uint32_t, std::uint32_t, FontKerning*);
int APS5_VABI sceFontGenerateCharGlyph(FontHandle, std::uint32_t, const FontGenerateGlyphDetail*, FontGlyph*);
int APS5_VABI sceFontGlyphDefineAttribute(FontGlyph, std::uint32_t, std::uint64_t);
int APS5_VABI sceFontDeleteGlyph(const FontMemory*, FontGlyph*);
int APS5_VABI sceFontRenderCharGlyphImage(FontHandle, std::uint32_t, FontRenderSurface*, float, float, FontGlyphMetrics*, FontRenderOutput*);
void APS5_VABI sceFontRenderSurfaceInit(FontRenderSurface*, void*, int, int, int, int);
void APS5_VABI sceFontRenderSurfaceSetScissor(FontRenderSurface*, int, int, int, int);
const void* APS5_VABI sceFontSelectLibraryFt(int);
const void* APS5_VABI sceFontSelectRendererFt(int);
}

static void Put16(std::vector<unsigned char>& out, int value) {
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(value & 0xFF));
}

static std::vector<unsigned char> Words(std::initializer_list<int> values) {
    std::vector<unsigned char> out;
    for (const int value : values) Put16(out, value);
    return out;
}

static std::vector<unsigned char> BuildFont(int glyphCount, std::map<std::string, std::vector<unsigned char>> tables) {
    tables["glyf"] = Words({0, 0});
    tables["head"] = Words({1, 0, 1, 0, 0, 0, 0x5F0F, 0x3CF5, 0, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0});
    tables["hhea"] = Words({1, 0, 800, -200, 0, 500, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1});
    tables["hmtx"] = Words({500});
    tables["hmtx"].resize(2 + 2 * static_cast<std::size_t>(glyphCount));
    tables["loca"].assign(2 * (static_cast<std::size_t>(glyphCount) + 1), 0);
    tables["maxp"] = Words({1, 0, glyphCount, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0});
    const int tableCount = static_cast<int>(tables.size());
    int entrySelector = 0;
    while ((2 << entrySelector) <= tableCount) ++entrySelector;
    std::vector<unsigned char> font = Words({1, 0, tableCount, 16 << entrySelector, entrySelector, 16 * tableCount - (16 << entrySelector)});
    int offset = 12 + 16 * tableCount;
    for (const auto& table : tables) {
        font.insert(font.end(), table.first.begin(), table.first.end());
        for (const int value : {0, 0, offset >> 16, offset & 0xFFFF, 0, static_cast<int>(table.second.size())}) Put16(font, value);
        offset += static_cast<int>((table.second.size() + 3) & ~std::size_t{3});
    }
    for (const auto& table : tables) {
        font.insert(font.end(), table.second.begin(), table.second.end());
        font.resize((font.size() + 3) & ~std::size_t{3});
    }
    return font;
}

static std::vector<unsigned char> EmptyGlyphFont() {
    return BuildFont(1, {});
}

static std::vector<unsigned char> KerningFont() {
    return BuildFont(3, {
        {"cmap", Words({0, 1, 3, 1, 0, 12, 4, 40, 0, 6, 4, 1, 2, 'A', 'V', 0xFFFF, 0, 'A', 'V', 0xFFFF, 1 - 'A', 2 - 'V', 1, 0, 0, 0})},
        {"kern", Words({0, 1, 0, 20, 1, 1, 6, 0, 0, 1, 2, -200})},
    });
}


static int allocations = 0;
static void* APS5_VABI Allocate(void*, std::uint32_t size) {
    ++allocations;
    return std::malloc(size);
}
static void APS5_VABI Release(void*, void* pointer) {
    if (pointer) --allocations;
    std::free(pointer);
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

float InterestingFloat(Rng& rng) {
    static const float values[] = {0.0f, -1.0f, 1.0f, 15.625f, 100.0f, 4096.0f, 1e6f, 1e30f, 1e-4f,
                                   std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                   std::numeric_limits<float>::quiet_NaN()};
    return values[rng.Below(sizeof(values) / sizeof(values[0]))];
}

std::uint32_t InterestingU32(Rng& rng) {
    static const std::uint32_t values[] = {0, 1, 'A', 'V', 0x7F, 0xFFFF, 0x10000, 0x10FFFF, 0x110000, 0x7FFFFFFF, 0xFFFFFFFFu};
    return rng.Below(3) == 0 ? static_cast<std::uint32_t>(rng.Next()) : values[rng.Below(sizeof(values) / sizeof(values[0]))];
}

std::vector<unsigned char> Mutate(std::vector<unsigned char> bytes, Rng& rng) {
    const std::size_t edits = 1 + rng.Below(8);
    for (std::size_t i = 0; i < edits && !bytes.empty(); ++i) {
        const std::size_t limit = rng.Below(2) ? std::min<std::size_t>(bytes.size(), 256) : bytes.size();
        const std::size_t at = rng.Below(limit);
        switch (rng.Below(5)) {
        case 0: bytes[at] ^= static_cast<unsigned char>(1u << rng.Below(8)); break;
        case 1: bytes[at] = static_cast<unsigned char>(rng.Next()); break;
        case 2: bytes.resize(at); break;
        case 3: bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), static_cast<unsigned char>(rng.Next())); break;
        default: bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at)); break;
        }
    }
    return bytes;
}

std::vector<unsigned char> EmptyFont() { return BuildFont(1, {}); }

std::vector<unsigned char> KerningFontData() {
    return BuildFont(3, {
        {"cmap", Words({0, 1, 3, 1, 0, 12, 4, 40, 0, 6, 4, 1, 2, 'A', 'V', 0xFFFF, 0, 'A', 'V', 0xFFFF, 1 - 'A', 2 - 'V', 1, 0, 0, 0})},
        {"kern", Words({0, 1, 0, 20, 1, 1, 6, 0, 0, 1, 2, -200})},
    });
}

template <class F> void Guarded(F&& call) {
    try {
        call();
    } catch (const std::exception&) {
    }
}

}  // namespace


static void Put32(std::vector<unsigned char>& out, std::uint32_t value) {
    Put16(out, static_cast<int>(value >> 16));
    Put16(out, static_cast<int>(value & 0xFFFFu));
}

static void PutAll16(std::vector<unsigned char>& out, std::initializer_list<int> values) {
    for (const int value : values) Put16(out, static_cast<int>(static_cast<std::uint16_t>(value)));
}

// Fuente sintética con un único glifo cuadrado, igual que la de GuestSystemFont.
static std::vector<unsigned char> SquareGlyphFont(std::uint32_t codepoint, int width) {
    std::vector<unsigned char> head;
    Put32(head, 0x00010000u);
    Put32(head, 0x00010000u);
    Put32(head, 0);
    Put32(head, 0x5F0F3CF5u);
    PutAll16(head, {0x000B, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, width, 800, 0, 8, 2, 0, 0});
    std::vector<unsigned char> hhea;
    Put32(hhea, 0x00010000u);
    PutAll16(hhea, {800, -200, 0, width, 0, 0, width, 1, 0, 0, 0, 0, 0, 0, 0, 2});
    std::vector<unsigned char> maxp;
    Put32(maxp, 0x00010000u);
    PutAll16(maxp, {2, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0});
    std::vector<unsigned char> hmtx;
    PutAll16(hmtx, {width, 0, width, 100});
    std::vector<unsigned char> glyf;
    PutAll16(glyf, {1, 100, 0, width - 100, 700, 3, 0});
    glyf.insert(glyf.end(), {1, 1, 1, 1});
    PutAll16(glyf, {100, 0, width - 200, 0, 0, 700, 0, -700});
    std::vector<unsigned char> loca;
    PutAll16(loca, {0, 0, static_cast<int>(glyf.size() / 2)});
    std::vector<unsigned char> cmap;
    PutAll16(cmap, {0, 1, 3, 1});
    Put32(cmap, 12);
    PutAll16(cmap, {4, 32, 0, 4, 4, 1, 0, static_cast<int>(codepoint), 0xFFFF, 0, static_cast<int>(codepoint), 0xFFFF, 1 - static_cast<int>(codepoint), 1, 0, 0});
    const std::vector<std::pair<const char*, const std::vector<unsigned char>*>> tables = {
        {"cmap", &cmap}, {"glyf", &glyf}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}};
    std::vector<unsigned char> font;
    Put32(font, 0x00010000u);
    PutAll16(font, {static_cast<int>(tables.size()), 64, 2, static_cast<int>(tables.size()) * 16 - 64});
    std::uint32_t offset = 12 + static_cast<std::uint32_t>(tables.size()) * 16;
    for (const auto& [tag, data] : tables) {
        font.insert(font.end(), tag, tag + 4);
        Put32(font, 0);
        Put32(font, offset);
        Put32(font, static_cast<std::uint32_t>(data->size()));
        offset += (static_cast<std::uint32_t>(data->size()) + 3u) & ~3u;
    }
    for (const auto& [tag, data] : tables) {
        font.insert(font.end(), data->begin(), data->end());
        font.resize((font.size() + 3u) & ~std::size_t{3});
    }
    return font;
}

// Renderiza en superficies coherentes (el buffer cabe en ancho, alto y tamaño de píxel) y de tamaño exacto en el
// heap. Fuera del área de recorte no debe cambiar ningún byte.
static std::size_t RenderPhase(FontLibrary library, FontRenderer renderer, Rng& rng, std::size_t rounds) {
    constexpr unsigned char Canary = 0xA5;
    std::size_t rendered = 0;
    for (std::size_t round = 0; round < rounds; ++round) {
        const std::vector<unsigned char> fontData = SquareGlyphFont('A', 300 + static_cast<int>(rng.Below(700)));
        FontHandle font = nullptr;
        if (sceFontOpenFontMemory(library, fontData.data(), static_cast<std::uint32_t>(fontData.size()), nullptr, &font) != SCE_FONT_OK) continue;
        if (sceFontBindRenderer(font, renderer) != SCE_FONT_OK) {
            sceFontCloseFont(font);
            continue;
        }
        sceFontSetResolutionDpi(font, 72, 72);
        sceFontSetScalePixel(font, rng.Below(8) == 0 ? InterestingFloat(rng) : 4.0f + static_cast<float>(rng.Below(80)), 4.0f + static_cast<float>(rng.Below(80)));
        for (int k = 0; k < 20; ++k) {
            const int width = static_cast<int>(rng.Below(80));
            const int height = static_cast<int>(rng.Below(80));
            const int bytesPerPixel = rng.Below(6) == 0 ? static_cast<int>(rng.Below(9)) : (rng.Below(2) ? 1 : 4);
            const int padding = static_cast<int>(rng.Below(17));
            const int widthByte = width * std::max(bytesPerPixel, 1) + padding;
            const std::size_t size = static_cast<std::size_t>(widthByte) * static_cast<std::size_t>(height);
            auto* buffer = static_cast<unsigned char*>(std::malloc(size ? size : 1));
            std::memset(buffer, Canary, size ? size : 1);
            FontRenderSurface surface{};
            sceFontRenderSurfaceInit(&surface, buffer, widthByte, bytesPerPixel, width, height);
            if (rng.Below(2)) sceFontRenderSurfaceSetScissor(&surface, static_cast<int>(rng.Below(120)) - 20, static_cast<int>(rng.Below(120)) - 20,
                                                              static_cast<int>(rng.Below(120)) - 10, static_cast<int>(rng.Below(120)) - 10);
            const std::uint32_t sx0 = surface.sc_x0, sy0 = surface.sc_y0, sx1 = surface.sc_x1, sy1 = surface.sc_y1;
            float x = static_cast<float>(rng.Below(160)) - 40.0f + static_cast<float>(rng.Below(100)) / 100.0f;
            float y = static_cast<float>(rng.Below(160)) - 40.0f + static_cast<float>(rng.Below(100)) / 100.0f;
            if (rng.Below(20) == 0) x = static_cast<float>(rng.Below(2000001)) - 1000000.0f;
            if (rng.Below(20) == 0) y = static_cast<float>(rng.Below(2000001)) - 1000000.0f;
            FontGlyphMetrics metrics{};
            FontRenderOutput output{};
            int rc = SCE_FONT_ERROR_FATAL;
            Guarded([&] { rc = sceFontRenderCharGlyphImage(font, 'A', &surface, x, y, &metrics, &output); });
            if (rc == SCE_FONT_OK) {
                ++rendered;
                const int bpp = std::max(bytesPerPixel, 1);
                for (int row = 0; row < height; ++row) {
                    for (int column = 0; column < widthByte; ++column) {
                        const bool padding_byte = column >= width * bpp;
                        const int pixel = column / bpp;
                        const bool inside = !padding_byte && pixel >= static_cast<int>(sx0) && pixel < static_cast<int>(std::min<std::uint32_t>(sx1, width)) &&
                                            row >= static_cast<int>(sy0) && row < static_cast<int>(std::min<std::uint32_t>(sy1, height));
                        if (!inside && buffer[static_cast<std::size_t>(row) * widthByte + column] != Canary) {
                            std::fprintf(stderr, "render touched a byte outside the scissor: %dx%d bpp=%d scissor=[%u,%u)x[%u,%u) x=%g y=%g row=%d column=%d\n",
                                         width, height, bytesPerPixel, sx0, sx1, sy0, sy1, x, y, row, column);
                            std::abort();
                        }
                    }
                }
            }
            std::free(buffer);
        }
        sceFontUnbindRenderer(font);
        sceFontCloseFont(font);
    }
    return rendered;
}

int main() {
    const char* env = std::getenv("ANYPS5_FONT_FUZZ_CASES");
    const std::size_t cases = env ? std::strtoull(env, nullptr, 10) : 3000;
    Rng rng{0x666f6e7400000001ull};
    const FontMemoryInterface iface{Allocate, Release, nullptr, nullptr, nullptr, nullptr};
    FontMemory memory{};
    if (sceFontMemoryInit(&memory, nullptr, 0, &iface, nullptr, nullptr, nullptr) != SCE_FONT_OK) return 2;
    FontLibrary library = nullptr;
    if (sceFontCreateLibrary(&memory, sceFontSelectLibraryFt(0), &library) != SCE_FONT_OK) return 2;
    sceFontSupportExternalFonts(library, 4, 0x52);
    FontRenderer renderer = nullptr;
    if (sceFontCreateRenderer(&memory, sceFontSelectRendererFt(0), &renderer) != SCE_FONT_OK) return 2;

    const std::vector<unsigned char> seeds[] = {EmptyFont(), KerningFontData()};
    std::size_t opened = 0;
    for (std::size_t i = 0; i < cases; ++i) {
        std::vector<unsigned char> bytes = seeds[rng.Below(2)];
        if (rng.Below(4) != 0) bytes = Mutate(std::move(bytes), rng);
        // Copia de tamaño exacto en el heap: una lectura pasada del final la detecta ASan.
        auto* data = static_cast<unsigned char*>(std::malloc(bytes.empty() ? 1 : bytes.size()));
        if (!bytes.empty()) std::memcpy(data, bytes.data(), bytes.size());
        FontHandle font = nullptr;
        int status = SCE_FONT_ERROR_INVALID_PARAMETER;
        Guarded([&] { status = sceFontOpenFontMemory(library, data, static_cast<std::uint32_t>(bytes.size()), nullptr, &font); });
        if (status == SCE_FONT_OK && font != nullptr) {
            ++opened;
            Guarded([&] { sceFontSetResolutionDpi(font, InterestingU32(rng), InterestingU32(rng)); });
            Guarded([&] { sceFontSetScalePixel(font, InterestingFloat(rng), InterestingFloat(rng)); });
            std::uint32_t count = 0;
            Guarded([&] { sceFontGetFontGlyphsCount(font, &count); });
            std::uint32_t code = 0;
            Guarded([&] { sceFontGetCharGlyphCode(font, InterestingU32(rng), &code); });
            std::uint32_t resolution = 0;
            float scale = 0.0f;
            Guarded([&] { sceFontGetFontResolution(font, &resolution, &scale); });
            FontKerning kerning{};
            Guarded([&] { sceFontGetKerning(font, InterestingU32(rng), InterestingU32(rng), &kerning); });
            FontGlyph glyph = nullptr;
            Guarded([&] {
                if (sceFontGenerateCharGlyph(font, InterestingU32(rng), nullptr, &glyph) == SCE_FONT_OK && glyph != nullptr)
                    sceFontGlyphDefineAttribute(glyph, InterestingU32(rng) & 0xFF, rng.Next());
            });
            if (glyph != nullptr) Guarded([&] { sceFontDeleteGlyph(&memory, &glyph); });
            Guarded([&] {
                if (sceFontBindRenderer(font, renderer) != SCE_FONT_OK) return;
                if (rng.Below(2)) sceFontSetupRenderScalePixel(font, InterestingFloat(rng), InterestingFloat(rng));
                else sceFontSetupRenderScalePoint(font, InterestingFloat(rng), InterestingFloat(rng));
                sceFontGetRenderScaledKerning(font, InterestingU32(rng), InterestingU32(rng), &kerning);
                sceFontUnbindRenderer(font);
            });
            Guarded([&] { sceFontCloseFont(font); });
        }
        std::free(data);
    }
    const std::size_t rendered = RenderPhase(library, renderer, rng, std::max<std::size_t>(cases / 50, 1));
    sceFontDestroyRenderer(&renderer);
    sceFontDestroyLibrary(&library);
    if (allocations != 0) {
        std::fprintf(stderr, "font fuzz: %d allocations not released\n", allocations);
        return 1;
    }
    sceFontMemoryTerm(&memory);
    std::printf("font fuzz: %zu cases, %zu fonts opened, %zu glyphs rendered\n", cases, opened, rendered);
    return 0;
}
