#include <SDL2/SDL.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>
#include <RmlUi/Core/SystemInterface.h>

#include "bitmap_font_engine.h"
#include "eden_app.h"
#include "frontend.h"
#include "assets_dir.h"
#include <cstring>
#include "diagnostics.h"
#include <chrono>
#include "radio_input.h"
#ifdef EDEN_DEV_ROM_ID
#include "development_input.h"
#include <fstream>
#endif

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <pthread.h>
#include <string>
#include <vector>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" void* mmap(void* address, std::size_t length, int protection,
    int flags, int descriptor, long offset);
extern "C" int munmap(void* address, std::size_t length);

namespace {

constexpr std::size_t kMappedAllocationThreshold = 64 * 1024;
constexpr std::uint64_t kAllocationMagic = UINT64_C(0x524144494F4D454D);
constexpr int kProtectionReadWrite = 3;
constexpr int kMapPrivateAnonymous = 0x1002;

struct alignas(std::max_align_t) AllocationHeader {
    std::uint64_t magic;
    std::size_t requested_size;
    std::size_t mapped_size;
};

void* AllocateTracked(std::size_t size) {
    if (size == 0) size = 1;
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader)) return nullptr;

    const std::size_t total = sizeof(AllocationHeader) + size;
    AllocationHeader* header = nullptr;
    std::size_t mapped_size = 0;
    if (size >= kMappedAllocationThreshold) {
        mapped_size = (total + 0x3fff) & ~std::size_t(0x3fff);
        void* mapping = mmap(nullptr, mapped_size, kProtectionReadWrite,
            kMapPrivateAnonymous, -1, 0);
        if (mapping != reinterpret_cast<void*>(-1)) {
            header = static_cast<AllocationHeader*>(mapping);
        }
    } else {
        header = static_cast<AllocationHeader*>(std::malloc(total));
    }
    if (!header) return nullptr;

    header->magic = kAllocationMagic;
    header->requested_size = size;
    header->mapped_size = mapped_size;
    return header + 1;
}

void FreeTracked(void* allocation) noexcept {
    if (!allocation) return;
    auto* header = static_cast<AllocationHeader*>(allocation) - 1;
    // SDL can retain small allocations made by its original allocator before
    // custom memory functions are installed. Those remain libc-owned.
    if (header->magic != kAllocationMagic) {
        std::free(allocation);
        return;
    }
    if (header->mapped_size != 0) {
        munmap(header, header->mapped_size);
    } else {
        std::free(header);
    }
}

void* CallocTracked(std::size_t count, std::size_t size) {
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return nullptr;
    const std::size_t total = count * size;
    void* allocation = AllocateTracked(total);
    if (allocation) std::memset(allocation, 0, total);
    return allocation;
}

void* ReallocTracked(void* allocation, std::size_t size) {
    if (!allocation) return AllocateTracked(size);
    if (size == 0) {
        FreeTracked(allocation);
        return nullptr;
    }

    auto* old_header = static_cast<AllocationHeader*>(allocation) - 1;
    if (old_header->magic != kAllocationMagic) std::abort();
    void* replacement = AllocateTracked(size);
    if (!replacement) return nullptr;
    std::memcpy(replacement, allocation,
        old_header->requested_size < size ? old_header->requested_size : size);
    FreeTracked(allocation);
    return replacement;
}

} // namespace

extern "C" int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
    constexpr int running = 2;
    int state = __atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE);
    if (state == PTHREAD_DONE_INIT) return 0;

    int expected = PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&once_control->state, &expected, running, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        init_routine();
        __atomic_store_n(&once_control->state, PTHREAD_DONE_INIT, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE) != PTHREAD_DONE_INIT) {
        sceKernelUsleep(100);
    }
    return 0;
}

extern "C" float strtof(const char* value, char** end) {
    return static_cast<float>(strtod(value, end));
}

extern "C" int fseek(std::FILE* file, long offset, int origin) {
    return fseeko(file, offset, origin);
}

extern "C" long ftell(std::FILE* file) {
    return static_cast<long>(ftello(file));
}

extern "C" char* strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; ++haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n) {
            const char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            const char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc) break;
            ++h;
            ++n;
        }
        if (!*n) return const_cast<char*>(haystack);
    }
    return nullptr;
}

namespace {

class AppSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return static_cast<double>(SDL_GetTicks64() - start_ticks_) / 1000.0;
    }

private:
    Uint64 start_ticks_ = SDL_GetTicks64();
};

class AppFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) {
            const Rml::String app_path = Eden::AppFile(path);
            file = std::fopen(app_path.c_str(), "rb");
        }
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    void Close(Rml::FileHandle file) override {
        if (file) std::fclose(reinterpret_cast<std::FILE*>(file));
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return std::fread(buffer, 1, size, reinterpret_cast<std::FILE*>(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return fseeko(reinterpret_cast<std::FILE*>(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override {
        return static_cast<size_t>(ftello(reinterpret_cast<std::FILE*>(file)));
    }
};

class SdlRenderInterface final : public Rml::RenderInterfaceCompatibility {
public:
    SdlRenderInterface(SDL_Renderer* renderer, SDL_Surface* surface) :
        renderer_(renderer), surface_(surface) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }

    void RenderGeometry(Rml::Vertex* rml_vertices, int num_vertices, int* indices,
        int num_indices, Rml::TextureHandle texture, const Rml::Vector2f& translation) override {
        AppTexture* app_texture = reinterpret_cast<AppTexture*>(texture);
        if (app_texture && RenderPixelAlignedQuads(rml_vertices, num_vertices, indices,
                num_indices, app_texture, translation)) {
            return;
        }

        std::vector<SDL_Vertex> vertices;
        vertices.reserve(static_cast<size_t>(num_vertices));
        for (int i = 0; i < num_vertices; ++i) {
            const Rml::Vertex& vertex = rml_vertices[i];
            SDL_Vertex sdl_vertex{};
            sdl_vertex.position = {vertex.position.x + translation.x, vertex.position.y + translation.y};
            sdl_vertex.color = {vertex.colour.red, vertex.colour.green,
                vertex.colour.blue, vertex.colour.alpha};
            sdl_vertex.tex_coord = {vertex.tex_coord.x, vertex.tex_coord.y};
            vertices.push_back(sdl_vertex);
        }
        SDL_RenderGeometry(renderer_, app_texture ? app_texture->texture : nullptr,
            vertices.data(), num_vertices, indices, num_indices);
    }

    bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions,
        const Rml::String& source) override {
        // A slow image load names itself in the log (the Library was reported slow to browse).
        const auto start = std::chrono::steady_clock::now();
        const bool loaded = LoadTextureFile(texture_handle, texture_dimensions, source);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (ms >= 20)
            Eden::Report("slow image", (source + ": " + std::to_string(ms) + " ms, " +
                std::to_string(texture_dimensions.x) + "x" + std::to_string(texture_dimensions.y)).c_str());
        return loaded;
    }

    bool LoadTextureFile(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions,
        const Rml::String& source) {
        texture_handle = {};
        texture_dimensions = {};

        std::FILE* file = std::fopen(source.c_str(), "rb");
        if (!file && !source.empty() && source[0] != '/') {
            const Rml::String app_path = Eden::AppFile(source);
            file = std::fopen(app_path.c_str(), "rb");
        }
        if (!file) {
            constexpr const char* marker = "prosperoeden/covers/";
            const std::size_t position = source.find(marker);
            if (position != Rml::String::npos) {
                const Rml::String generated_path = Eden::CoversDir() + "/" + source.substr(position + std::strlen(marker));
                file = std::fopen(generated_path.c_str(), "rb");
            }
        }
        if (!file) return false;

        unsigned char header[18]{};
        const bool header_read = std::fread(header, 1, sizeof(header), file) == sizeof(header);
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        bool decoded = false;

        if (header_read && std::memcmp(header, "RTA1", 4) == 0) {
            width = ReadLe16(header + 4);
            height = ReadLe16(header + 6);
            const std::uint32_t pixel_count = ReadLe32(header + 8);
            const std::uint32_t payload_length = ReadLe32(header + 12);
            const std::size_t expected_pixels = static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height);
            if (width > 0 && height > 0 && pixel_count == expected_pixels &&
                expected_pixels <= std::numeric_limits<std::size_t>::max() / 4 &&
                payload_length <= expected_pixels * 2 &&
                fseeko(file, 16, SEEK_SET) == 0) {
                std::vector<unsigned char> payload(payload_length);
                if (std::fread(payload.data(), 1, payload.size(), file) == payload.size())
                    decoded = DecodeRadioAtlas(payload, expected_pixels, pixels);
            }
        } else {
            width = header[12] | (header[13] << 8);
            height = header[14] | (header[15] << 8);
            const bool supported = header_read && header[0] == 0 && header[1] == 0 &&
                header[2] == 2 && width > 0 && height > 0 && header[16] == 32 &&
                (header[17] & 0x0f) == 8 && (header[17] & 0x30) == 0x20;
            if (supported && static_cast<std::size_t>(width) <=
                    std::numeric_limits<std::size_t>::max() /
                        (static_cast<std::size_t>(height) * 4)) {
                pixels.resize(static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4);
                decoded = std::fread(pixels.data(), 1, pixels.size(), file) == pixels.size();
            }
        }
        std::fclose(file);
        if (!decoded) return false;

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), width, height,
            32, width * 4, SDL_PIXELFORMAT_BGRA32);
        if (!surface) return false;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture) return false;

        const bool art = source.find("/covers/") != Rml::String::npos ||
            source.find("background-menu.tga") != Rml::String::npos ||
            source.find("brand-72.tga") != Rml::String::npos;
        if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) != 0 ||
            SDL_SetTextureScaleMode(texture, art ? SDL_ScaleModeLinear : SDL_ScaleModeNearest) != 0) {
            SDL_DestroyTexture(texture);
            return false;
        }

        auto* app_texture = new AppTexture;
        app_texture->texture = texture;
        app_texture->width = width;
        app_texture->height = height;
        app_texture->exact_pixels = source.find("lvgl-bitmap") != Rml::String::npos;
        app_texture->rgba.resize(pixels.size());
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            app_texture->rgba[i + 0] = pixels[i + 2];
            app_texture->rgba[i + 1] = pixels[i + 1];
            app_texture->rgba[i + 2] = pixels[i + 0];
            app_texture->rgba[i + 3] = pixels[i + 3];
        }
        if (app_texture->exact_pixels) {
            app_texture->surface = SDL_CreateRGBSurfaceWithFormatFrom(
                app_texture->rgba.data(), width, height, 32, width * 4,
                SDL_PIXELFORMAT_RGBA32);
            if (!app_texture->surface ||
                SDL_SetSurfaceBlendMode(app_texture->surface, SDL_BLENDMODE_BLEND) != 0) {
                SDL_FreeSurface(app_texture->surface);
                SDL_DestroyTexture(texture);
                delete app_texture;
                return false;
            }
        }

        texture_dimensions = {width, height};
        texture_handle = reinterpret_cast<Rml::TextureHandle>(app_texture);
        return true;
    }

    bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source,
        const Rml::Vector2i& dimensions) override {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
            const_cast<Rml::byte*>(source), dimensions.x, dimensions.y, 32,
            dimensions.x * 4, SDL_PIXELFORMAT_RGBA32);
        if (!surface) return false;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (texture) {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        }
        if (!texture) return false;

        auto* app_texture = new AppTexture;
        app_texture->texture = texture;
        app_texture->width = dimensions.x;
        app_texture->height = dimensions.y;
        app_texture->rgba.assign(source, source +
            static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4);
        texture_handle = reinterpret_cast<Rml::TextureHandle>(app_texture);
        return true;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        auto* app_texture = reinterpret_cast<AppTexture*>(texture);
        if (!app_texture) return;
        SDL_FreeSurface(app_texture->surface);
        SDL_DestroyTexture(app_texture->texture);
        delete app_texture;
    }

    void EnableScissorRegion(bool enable) override {
        scissor_enabled_ = enable;
        SDL_RenderSetClipRect(renderer_, enable ? &scissor_ : nullptr);
    }

    void SetScissorRegion(int x, int y, int width, int height) override {
        scissor_ = {x, y, width, height};
        if (scissor_enabled_) SDL_RenderSetClipRect(renderer_, &scissor_);
    }

private:
    struct AppTexture {
        SDL_Texture* texture = nullptr;
        SDL_Surface* surface = nullptr;
        int width = 0;
        int height = 0;
        bool exact_pixels = false;
        std::vector<Rml::byte> rgba;
    };

    static std::uint16_t ReadLe16(const unsigned char* value) {
        return static_cast<std::uint16_t>(value[0]) |
            (static_cast<std::uint16_t>(value[1]) << 8);
    }

    static std::uint32_t ReadLe32(const unsigned char* value) {
        return static_cast<std::uint32_t>(value[0]) |
            (static_cast<std::uint32_t>(value[1]) << 8) |
            (static_cast<std::uint32_t>(value[2]) << 16) |
            (static_cast<std::uint32_t>(value[3]) << 24);
    }

    static bool DecodeRadioAtlas(const std::vector<unsigned char>& payload,
        std::size_t pixel_count, std::vector<unsigned char>& pixels) {
        pixels.assign(pixel_count * 4, 255);
        std::size_t input = 0;
        std::size_t output = 0;
        while (input < payload.size() && output < pixel_count) {
            const unsigned char token = payload[input++];
            const std::size_t length = static_cast<std::size_t>(token & 0x7f) + 1;
            if (length > pixel_count - output) return false;
            if ((token & 0x80) == 0) {
                for (std::size_t index = 0; index < length; ++index)
                    pixels[(output + index) * 4 + 3] = 0;
            } else {
                const std::size_t bytes = (length + 1) / 2;
                if (bytes > payload.size() - input) return false;
                for (std::size_t index = 0; index < length; ++index) {
                    const unsigned char packed = payload[input + index / 2];
                    const unsigned char alpha = (index & 1) != 0
                        ? static_cast<unsigned char>((packed & 0x0f) * 17)
                        : static_cast<unsigned char>((packed >> 4) * 17);
                    pixels[(output + index) * 4 + 3] = alpha;
                }
                input += bytes;
            }
            output += length;
        }
        return input == payload.size() && output == pixel_count;
    }

    struct PixelCopy {
        SDL_Rect source;
        SDL_Rect destination;
        Rml::ColourbPremultiplied colour;
    };

    static int RoundPixel(float value) {
        return static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f));
    }

    static bool SameColor(const Rml::Vertex& lhs, const Rml::Vertex& rhs) {
        return lhs.colour.red == rhs.colour.red && lhs.colour.green == rhs.colour.green &&
            lhs.colour.blue == rhs.colour.blue && lhs.colour.alpha == rhs.colour.alpha;
    }

    bool RenderPixelAlignedQuads(Rml::Vertex* vertices, int num_vertices, int* indices,
        int num_indices, AppTexture* texture, const Rml::Vector2f& translation) {
        if (num_vertices <= 0 || num_indices <= 0 || num_indices % 6 != 0) return false;

        const int texture_width = texture->width;
        const int texture_height = texture->height;
        if (texture_width <= 0 || texture_height <= 0) return false;

        const int num_quads = num_indices / 6;
        std::vector<PixelCopy> copies;
        copies.reserve(static_cast<size_t>(num_quads));
        for (int quad = 0; quad < num_quads; ++quad) {
            const int index = quad * 6;
            const int i0 = indices[index + 0];
            const int i3 = indices[index + 1];
            const int i1 = indices[index + 2];
            const int i2 = indices[index + 5];
            if (i0 < 0 || i0 >= num_vertices || i1 < 0 || i1 >= num_vertices ||
                i2 < 0 || i2 >= num_vertices || i3 < 0 || i3 >= num_vertices ||
                indices[index + 3] != i1 || indices[index + 4] != i3 ||
                i0 == i1 || i0 == i2 || i0 == i3 || i1 == i2 || i1 == i3 || i2 == i3)
                return false;

            const Rml::Vertex& v0 = vertices[i0];
            const Rml::Vertex& v1 = vertices[i1];
            const Rml::Vertex& v2 = vertices[i2];
            const Rml::Vertex& v3 = vertices[i3];
            if (v0.position.y != v1.position.y || v1.position.x != v2.position.x ||
                v2.position.y != v3.position.y || v3.position.x != v0.position.x ||
                v0.tex_coord.y != v1.tex_coord.y || v1.tex_coord.x != v2.tex_coord.x ||
                v2.tex_coord.y != v3.tex_coord.y || v3.tex_coord.x != v0.tex_coord.x ||
                !SameColor(v0, v1) || !SameColor(v0, v2) || !SameColor(v0, v3))
                return false;

            const int source_width = RoundPixel((v1.tex_coord.x - v0.tex_coord.x) * texture_width);
            const int source_height = RoundPixel((v3.tex_coord.y - v0.tex_coord.y) * texture_height);
            const int destination_width = RoundPixel(v1.position.x - v0.position.x);
            const int destination_height = RoundPixel(v3.position.y - v0.position.y);
            if (source_width <= 0 || source_height <= 0 ||
                destination_width <= 0 || destination_height <= 0)
                return false;

            SDL_Rect source{
                RoundPixel(v0.tex_coord.x * texture_width),
                RoundPixel(v0.tex_coord.y * texture_height), source_width, source_height};
            if (source.x < 0 || source.y < 0 || source.x + source.w > texture_width ||
                source.y + source.h > texture_height)
                return false;
            copies.push_back({
                source,
                {RoundPixel(v0.position.x + translation.x),
                    RoundPixel(v0.position.y + translation.y), destination_width, destination_height},
                v0.colour});
        }

        if (texture->exact_pixels) return CompositeExactPixels(*texture, copies);

        for (const PixelCopy& copy : copies) {
            SDL_SetTextureColorMod(texture->texture,
                copy.colour.red, copy.colour.green, copy.colour.blue);
            SDL_SetTextureAlphaMod(texture->texture, copy.colour.alpha);
            SDL_RenderCopy(renderer_, texture->texture, &copy.source, &copy.destination);
        }

        SDL_SetTextureColorMod(texture->texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture->texture, 255);
        return true;
    }

    bool CompositeExactPixels(const AppTexture& texture,
        const std::vector<PixelCopy>& copies) {
        if (!surface_ || !texture.surface) return false;
        SDL_RenderFlush(renderer_);

        SDL_Rect old_clip{};
        SDL_GetClipRect(surface_, &old_clip);
        SDL_SetClipRect(surface_, scissor_enabled_ ? &scissor_ : nullptr);
        for (const PixelCopy& copy : copies) {
            SDL_SetSurfaceColorMod(texture.surface,
                copy.colour.red, copy.colour.green, copy.colour.blue);
            SDL_SetSurfaceAlphaMod(texture.surface, copy.colour.alpha);
            SDL_Rect destination = copy.destination;
            SDL_BlitSurface(texture.surface, &copy.source, surface_, &destination);
        }
        SDL_SetSurfaceColorMod(texture.surface, 255, 255, 255);
        SDL_SetSurfaceAlphaMod(texture.surface, 255);
        SDL_SetClipRect(surface_, &old_clip);
        return true;
    }

    SDL_Renderer* renderer_;
    SDL_Surface* surface_;
    SDL_Rect scissor_{};
    bool scissor_enabled_ = false;
};

bool LoadFonts() {
    static constexpr const char* kBitmapFonts[] = {
        "ui/fonts/lvgl-bitmap/Montserrat-20.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-24.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-28.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-32.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-36.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-40.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-48.fnt",
    };
    for (const char* font : kBitmapFonts) {
        if (!Rml::LoadFontFace(font)) return false;
    }
    return true;
}

void PresentColor(SDL_Renderer* renderer, SDL_Window* window, Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    SDL_UpdateWindowSurface(window);
}

std::string RunApp(const std::string& launch_error) {
    if (SDL_SetMemoryFunctions(AllocateTracked, CallocTracked, ReallocTracked, FreeTracked) != 0) {
        return {};
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        Eden::Report("menu video failure", SDL_GetError());
        return {};
    }
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Window* window = SDL_CreateWindow("ProsperoEden", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!window || !renderer) {
        Eden::Report("menu renderer failure", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return {};
    }

    PresentColor(renderer, window, 10, 22, 5);

    AppSystemInterface system_interface;
    AppFileInterface file_interface;
    SdlRenderInterface render_interface(renderer, surface);
    BitmapFontEngine font_engine;
    Rml::RenderInterface* adapted_render_interface = render_interface.GetAdaptedInterface();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetRenderInterface(adapted_render_interface);
    Rml::SetFontEngineInterface(&font_engine);

    bool running = Rml::Initialise();
    if (running) running = LoadFonts();
    Rml::Context* context = running ? Rml::CreateContext("prosperoeden", {1920, 1080}, adapted_render_interface) : nullptr;
    Rml::ElementDocument* document = context ? context->LoadDocument("ui/main.rml") : nullptr;
    EdenApp app;
    bool input_ready = false;
    if (document) {
        document->Show();
        document->GetElementById("startup-status")->SetInnerRML("Checking keys and firmware...");
        context->Update();
        context->Render();
        SDL_RenderFlush(renderer);
        SDL_UpdateWindowSurface(window);
        sceSystemServiceHideSplashScreen();
        Eden::Report("setup", "Checking supplied keys and firmware");
        input_ready = radio_input_init();
        running = input_ready && app.Initialize(document, launch_error);
        if (running) sceSystemServiceHideSplashScreen();
    } else {
        Eden::Report("menu failure", "UI document or fonts could not load; check app UI files");
        PresentColor(renderer, window, 180, 20, 40);
        running = false;
    }

#ifdef EDEN_DEV_ROM_ID
    static Eden::DevelopmentInput development_input;
    unsigned development_poll = 0;
    bool capture_ui = true;
    std::fprintf(stderr, "EDEN_DEV_LAUNCHER_READY ready=%d\n", running);
#endif
    while (running && app.SelectedGame().empty()) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }
#ifdef EDEN_DEV_ROM_ID
        const auto now = SDL_GetTicks64();
        if (++development_poll >= 10) {
            development_poll = 0;
            std::ifstream command(Eden::AppFile("compat-input.txt"));
            if (development_input.Read(command, now)) {
                capture_ui = true;
                std::fprintf(stderr, "EDEN_DEV_UI_INPUT sequence=%llu buttons=%x\n",
                    static_cast<unsigned long long>(development_input.sequence), development_input.buttons);
            }
        }
        if (!development_input.active) radio_input_poll();
        if (const auto sample = development_input.Sample(now)) {
            static_assert(sizeof(*sample) == 120);
            radio_input_development_sample(&*sample);
        }
#else
        radio_input_poll();
#endif
        radio_input_event_t input{};
        while (radio_input_next(&input)) app.HandleInput(input);
        app.Poll();
        const auto frame_start = std::chrono::steady_clock::now();
        context->Update();
        const auto updated = std::chrono::steady_clock::now();
        SDL_SetRenderDrawColor(renderer, 10, 22, 5, 255);
        SDL_RenderClear(renderer);
        context->Render();
        const auto rendered = std::chrono::steady_clock::now();
        SDL_RenderFlush(renderer);
        SDL_UpdateWindowSurface(window);
        {
            // A slow launcher frame names its stage in the log, at most once a second.
            using std::chrono::duration_cast, std::chrono::milliseconds;
            const auto done = std::chrono::steady_clock::now();
            static auto last_report = done - std::chrono::seconds(2);
            if (done - frame_start >= milliseconds(100) && done - last_report >= std::chrono::seconds(1)) {
                last_report = done;
                const std::string detail = "update " + std::to_string(duration_cast<milliseconds>(updated - frame_start).count()) +
                    " ms, render " + std::to_string(duration_cast<milliseconds>(rendered - updated).count()) +
                    " ms, present " + std::to_string(duration_cast<milliseconds>(done - rendered).count()) + " ms";
                Eden::Report("slow frame", detail.c_str());
            }
        }
#ifdef EDEN_DEV_ROM_ID
        if (capture_ui && std::ifstream(Eden::AppFile("ui-capture.txt")).good()) {
            capture_ui = false;
            const int rc = SDL_SaveBMP(surface, Eden::LogFile("launcher.bmp").c_str());
            std::fprintf(stderr, "EDEN_DEV_UI_CAPTURE rc=%d\n", rc);
        }
#endif
        sceKernelUsleep(16667);
    }

    std::string selected_game = app.SelectedGame();
    app.Shutdown();
    if (input_ready) radio_input_shutdown();
    if (document) document->Close();
    if (context) Rml::RemoveContext("prosperoeden");
    Rml::Shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return selected_game;
}

} // namespace

std::string SelectProsperoEdenGame(const std::string& launch_error) {
    return RunApp(launch_error);
}
