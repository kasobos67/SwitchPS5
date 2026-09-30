#include "metadata_bridge.h"
#include "assets_dir.h"
#if defined(__PROSPERO__)
#include "native_directory.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <stb_image.h>

#include "common/fs/path_util.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/loader/loader.h"

namespace {
constexpr std::array<const char*, 18> language_names{
    "AmericanEnglish", "BritishEnglish", "Japanese", "French", "German",
    "LatinAmericanSpanish", "Spanish", "Italian", "Dutch", "CanadianFrench",
    "Portuguese", "Russian", "Korean", "TraditionalChinese", "SimplifiedChinese",
    "BrazilianPortuguese", "Polish", "Thai",
};

FileSys::VirtualDir OpenControlRomFs(const FileSys::VirtualFile& file, bool xci) {
    std::shared_ptr<FileSys::NCA> control;
    if (xci) {
        FileSys::XCI image(file);
        if (image.GetStatus() != Loader::ResultStatus::Success) return {};
        control = image.GetNCAByType(FileSys::NCAContentType::Control);
    } else {
        FileSys::NSP package(file);
        if (package.GetStatus() != Loader::ResultStatus::Success) return {};
        control = package.GetNCA(FileSys::GetBaseTitleID(package.GetProgramTitleID()),
                                 FileSys::ContentRecordType::Control);
    }
    if (!control || control->GetStatus() != Loader::ResultStatus::Success) return {};
    const auto romfs = control->GetRomFS();
    return romfs ? FileSys::ExtractRomFS(romfs) : FileSys::VirtualDir{};
}

std::string ReadTitle(const FileSys::VirtualDir& romfs) {
    auto nacp = romfs->GetFile("control.nacp");
    if (!nacp) nacp = romfs->GetFile("Control.nacp");
    if (!nacp || nacp->GetSize() < 0x3000) return {};
    std::array<char, 0x200> name{};
    for (std::size_t language = 0; language < 16; ++language) {
        if (nacp->Read(reinterpret_cast<unsigned char*>(name.data()), name.size(),
                       language * 0x300) != name.size()) return {};
        name.back() = '\0';
        if (name.front()) return name.data();
    }
    return {};
}

FileSys::VirtualFile FindIcon(const FileSys::VirtualDir& romfs) {
    for (const char* language : language_names) {
        if (auto icon = romfs->GetFile(std::string{"icon_"} + language + ".dat")) return icon;
    }
    return {};
}

bool WriteTga(const FileSys::VirtualFile& icon, const char* output) {
    const auto encoded = icon->ReadAllBytes();
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* rgba = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                                 &width, &height, &channels, 4);
    if (!rgba || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        stbi_image_free(rgba);
        return false;
    }
    std::FILE* file = std::fopen(output, "wb");
    if (!file) {
        stbi_image_free(rgba);
        return false;
    }
    unsigned char header[18]{};
    header[2] = 2;
    header[12] = static_cast<unsigned char>(width);
    header[13] = static_cast<unsigned char>(width >> 8);
    header[14] = static_cast<unsigned char>(height);
    header[15] = static_cast<unsigned char>(height >> 8);
    header[16] = 32;
    header[17] = 0x28;
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * 4);
    for (int y = 0; ok && y < height; ++y) {
        const unsigned char* source = rgba + static_cast<std::size_t>(y) * row.size();
        for (int x = 0; x < width; ++x) {
            row[4 * x + 0] = source[4 * x + 2];
            row[4 * x + 1] = source[4 * x + 1];
            row[4 * x + 2] = source[4 * x + 0];
            row[4 * x + 3] = source[4 * x + 3];
        }
        ok = std::fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    ok = std::fclose(file) == 0 && ok;
    stbi_image_free(rgba);
    return ok;
}
}

uint64_t eden_game_title_id(const char* rom_path) {
    if (!rom_path) return 0;
    Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, Eden::AssetsPath("keys"));
    FileSys::RealVfsFilesystem vfs;
    const auto file = vfs.OpenFile(rom_path, FileSys::OpenMode::Read);
    if (!file) return 0;
    std::string path = rom_path;
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (path.ends_with(".xci")) {
        FileSys::XCI image(file);
        return image.GetStatus() == Loader::ResultStatus::Success ? image.GetProgramTitleID() : 0;
    }
    if (path.ends_with(".nsp")) {
        FileSys::NSP package(file);
        return package.GetStatus() == Loader::ResultStatus::Success ? package.GetProgramTitleID() : 0;
    }
    return 0;
}

int eden_extract_game_metadata(const char* rom_path, const char* keys_dir,
                               const char* cover_tga_path, char* title,
                               size_t title_capacity) {
    if (!rom_path || !keys_dir || !cover_tga_path || !title || title_capacity == 0) return 0;
    title[0] = '\0';
    Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, keys_dir);
    FileSys::RealVfsFilesystem vfs;
    const auto file = vfs.OpenFile(rom_path, FileSys::OpenMode::Read);
    if (!file) return 0;
    std::string path = rom_path;
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool xci = path.size() >= 4 && path.substr(path.size() - 4) == ".xci";
    const auto romfs = OpenControlRomFs(file, xci);
    if (!romfs) return 0;

    int result = 0;
    const auto extracted_title = ReadTitle(romfs);
    if (!extracted_title.empty()) {
        std::snprintf(title, title_capacity, "%s", extracted_title.c_str());
        result |= EDEN_METADATA_TITLE;
    }
    if (const auto icon = FindIcon(romfs); icon && WriteTga(icon, cover_tga_path))
        result |= EDEN_METADATA_COVER;
    return result;
}

const char* eden_startup_error() {
    static const std::string error = []() -> std::string {
        try {
            const std::string keys = Eden::AssetsPath("keys");
            const std::string firmware = Eden::AssetsPath("firmware");
            Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, keys);
            FileSys::RealVfsFilesystem vfs;
            const auto prod = vfs.OpenFile(std::string{keys} + "/prod.keys", FileSys::OpenMode::Read);
            if (!prod || !prod->GetSize())
                return "Missing or empty keys/prod.keys in " + Eden::AssetsDir() + ".";
            auto& manager = Core::Crypto::KeyManager::Instance();
            if (!manager.HasKey(Core::Crypto::S256KeyType::Header))
                return "prod.keys could not supply an NCA header key. Replace it with a valid key dump.";
            std::error_code ec;
#if defined(__PROSPERO__)
            const auto entries = Eden::ReadNativeDirectory(firmware, ec);
#else
            std::vector<std::filesystem::directory_entry> entries;
            for (std::filesystem::directory_iterator it{firmware, ec}, end; !ec && it != end; it.increment(ec))
                entries.push_back(*it);
#endif
            if (ec) return "Cannot read firmware/ in " + Eden::AssetsDir() + ". Install extracted firmware NCAs.";
            unsigned count = 0;
            bool version = false;
            for (const auto& entry : entries) {
                if (entry.path().extension() != ".nca") continue;
                ++count;
                const auto file = vfs.OpenFile(entry.path().string(), FileSys::OpenMode::Read);
                if (!file) return "A firmware NCA cannot be read. Reinstall the firmware dump.";
                FileSys::NCA nca(file);
                if (nca.GetStatus() != Loader::ResultStatus::Success)
                    return "Firmware NCA validation failed (code " +
                        std::to_string(static_cast<unsigned>(nca.GetStatus())) +
                        "). Check that firmware and prod.keys are compatible.";
                if (nca.GetTitleId() == 0x0100000000000809ULL && nca.GetRomFS()) {
                    const auto romfs = FileSys::ExtractRomFS(nca.GetRomFS());
                    const auto file = romfs ? romfs->GetFile("file") : FileSys::VirtualFile{};
                    version |= file && file->GetSize() >= 0x100 && file->ReadBytes(0x100).size() == 0x100;
                }
            }
            if (!count) return "No firmware NCAs found in " + Eden::AssetsPath("firmware") + ".";
            if (!version) return "Firmware SystemVersion data is missing or unreadable. Install a complete firmware dump.";
            return {};
        } catch (...) {
            return "Setup validation failed. Check that firmware and key files are readable and valid.";
        }
    }();
    return error.c_str();
}
