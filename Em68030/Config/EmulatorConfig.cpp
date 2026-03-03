#include "pch.h"

#include "EmulatorConfig.h"

#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Em68030::Config {

// ============================================================================
// MemoryRegionConfig JSON serialization
// ============================================================================

void to_json(nlohmann::json& j, const MemoryRegionConfig& c)
{
    j = nlohmann::json{
        {"BaseAddress", c.BaseAddress},
        {"Size", c.Size},
        {"Type", c.Type}
    };
}

void from_json(const nlohmann::json& j, MemoryRegionConfig& c)
{
    if (j.contains("BaseAddress")) j.at("BaseAddress").get_to(c.BaseAddress);
    if (j.contains("Size"))        j.at("Size").get_to(c.Size);
    if (j.contains("Type"))        j.at("Type").get_to(c.Type);
}

// ============================================================================
// ScsiDiskConfig JSON serialization
// ============================================================================

void to_json(nlohmann::json& j, const ScsiDiskConfig& c)
{
    j = nlohmann::json{
        {"Path", c.Path},
        {"ScsiId", c.ScsiId}
    };
}

void from_json(const nlohmann::json& j, ScsiDiskConfig& c)
{
    if (j.contains("Path"))   j.at("Path").get_to(c.Path);
    if (j.contains("ScsiId")) j.at("ScsiId").get_to(c.ScsiId);
}

// ============================================================================
// EmulatorConfig JSON serialization
// ============================================================================

void to_json(nlohmann::json& j, const EmulatorConfig& c)
{
    j = nlohmann::json{
        {"MemorySize",              c.MemorySize},
        {"MemoryRegions",           c.MemoryRegions},
        {"ConsoleBaseAddress",      c.ConsoleBaseAddress},
        {"HddBaseAddress",          c.HddBaseAddress},
        {"ConsoleEnabled",          c.ConsoleEnabled},
        {"HddEnabled",              c.HddEnabled},
        {"HddImagePath",            c.HddImagePath},
        {"FontFamily",              c.FontFamily},
        {"FontSize",                c.FontSize},
        {"LastOpenedFile",           c.LastOpenedFile},
        {"LastLoadAddress",          c.LastLoadAddress},
        {"BoardType",               c.BoardType},
        {"Mvme147RomPath",          c.Mvme147RomPath},
        {"Mvme147ScsiDisks",        c.Mvme147ScsiDisks},
        {"Mvme147ScsiCdromPath",    c.Mvme147ScsiCdromPath},
        {"Mvme147ScsiCdromId",      c.Mvme147ScsiCdromId},
        {"NetworkMode",             c.NetworkMode},
        {"ConsoleScrollbackLines",  c.ConsoleScrollbackLines}
    };
}

void from_json(const nlohmann::json& j, EmulatorConfig& c)
{
    if (j.contains("MemorySize"))             j.at("MemorySize").get_to(c.MemorySize);
    if (j.contains("MemoryRegions"))          j.at("MemoryRegions").get_to(c.MemoryRegions);
    if (j.contains("ConsoleBaseAddress"))     j.at("ConsoleBaseAddress").get_to(c.ConsoleBaseAddress);
    if (j.contains("HddBaseAddress"))         j.at("HddBaseAddress").get_to(c.HddBaseAddress);
    if (j.contains("ConsoleEnabled"))         j.at("ConsoleEnabled").get_to(c.ConsoleEnabled);
    if (j.contains("HddEnabled"))             j.at("HddEnabled").get_to(c.HddEnabled);
    if (j.contains("HddImagePath"))           j.at("HddImagePath").get_to(c.HddImagePath);
    if (j.contains("FontFamily"))             j.at("FontFamily").get_to(c.FontFamily);
    if (j.contains("FontSize"))               j.at("FontSize").get_to(c.FontSize);
    if (j.contains("LastOpenedFile"))          j.at("LastOpenedFile").get_to(c.LastOpenedFile);
    if (j.contains("LastLoadAddress"))         j.at("LastLoadAddress").get_to(c.LastLoadAddress);
    if (j.contains("BoardType"))              j.at("BoardType").get_to(c.BoardType);
    // Backward compat: old configs may have "Mvme147RamSize" instead of "MemorySize"
    if (j.contains("Mvme147RamSize") && !j.contains("MemorySize"))
        j.at("Mvme147RamSize").get_to(c.MemorySize);
    if (j.contains("Mvme147RomPath"))         j.at("Mvme147RomPath").get_to(c.Mvme147RomPath);
    if (j.contains("Mvme147ScsiDisks"))       j.at("Mvme147ScsiDisks").get_to(c.Mvme147ScsiDisks);
    if (j.contains("Mvme147ScsiCdromPath"))   j.at("Mvme147ScsiCdromPath").get_to(c.Mvme147ScsiCdromPath);
    if (j.contains("Mvme147ScsiCdromId"))     j.at("Mvme147ScsiCdromId").get_to(c.Mvme147ScsiCdromId);
    if (j.contains("NetworkMode"))             j.at("NetworkMode").get_to(c.NetworkMode);
    if (j.contains("ConsoleScrollbackLines")) j.at("ConsoleScrollbackLines").get_to(c.ConsoleScrollbackLines);

    // Migrate legacy per-disk fields to Mvme147ScsiDisks list
    if (c.Mvme147ScsiDisks.empty() && !j.contains("Mvme147ScsiDisks"))
    {
        if (j.contains("Mvme147ScsiDiskPath"))
        {
            std::string path = j.at("Mvme147ScsiDiskPath").get<std::string>();
            int id = 0;
            if (j.contains("Mvme147ScsiDiskId"))
                id = j.at("Mvme147ScsiDiskId").get<int>();
            if (!path.empty())
                c.Mvme147ScsiDisks.push_back({ path, id });
        }
        if (j.contains("Mvme147ScsiDisk2Path"))
        {
            std::string path2 = j.at("Mvme147ScsiDisk2Path").get<std::string>();
            int id2 = 1;
            if (j.contains("Mvme147ScsiDisk2Id"))
                id2 = j.at("Mvme147ScsiDisk2Id").get<int>();
            if (!path2.empty())
                c.Mvme147ScsiDisks.push_back({ path2, id2 });
        }
    }
}

// ============================================================================
// GetConfigPath -- equivalent to Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "appsettings.json")
// ============================================================================

std::filesystem::path EmulatorConfig::GetConfigPath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(buf).parent_path();
#else
    // Fallback: use current working directory
    std::filesystem::path exeDir = std::filesystem::current_path();
#endif
    return exeDir / "appsettings.json";
}

// ============================================================================
// Load
// ============================================================================

EmulatorConfig EmulatorConfig::Load()
{
    try
    {
        auto configPath = GetConfigPath();
        if (std::filesystem::exists(configPath))
        {
            std::ifstream ifs(configPath);
            if (ifs.is_open())
            {
                nlohmann::json j = nlohmann::json::parse(ifs);
                return j.get<EmulatorConfig>();
            }
        }
    }
    catch (...)
    {
        // If load fails, return defaults
    }
    return EmulatorConfig{};
}

// ============================================================================
// Save
// ============================================================================

void EmulatorConfig::Save() const
{
    try
    {
        auto configPath = GetConfigPath();
        nlohmann::json j = *this;
        std::ofstream ofs(configPath);
        if (ofs.is_open())
        {
            ofs << j.dump(4); // WriteIndented equivalent (4-space indent)
        }
    }
    catch (...)
    {
        // Silently fail
    }
}

// ============================================================================
// Clone -- deep copy via JSON round-trip
// ============================================================================

EmulatorConfig EmulatorConfig::Clone() const
{
    nlohmann::json j = *this;
    return j.get<EmulatorConfig>();
}

} // namespace Em68030::Config
