#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

#include "../ThirdParty/nlohmann/json.hpp"

namespace Em68030::Config {

// ============================================================================
// MemoryRegionConfig
// ============================================================================

struct MemoryRegionConfig {
    uint32_t BaseAddress = 0;
    int Size = 0;
    std::string Type = "Ram"; // "Ram" or "Rom"
};

void to_json(nlohmann::json& j, const MemoryRegionConfig& c);
void from_json(const nlohmann::json& j, MemoryRegionConfig& c);

// ============================================================================
// ScsiDiskConfig
// ============================================================================

struct ScsiDiskConfig {
    std::string Path;
    int ScsiId = 0;
};

void to_json(nlohmann::json& j, const ScsiDiskConfig& c);
void from_json(const nlohmann::json& j, ScsiDiskConfig& c);

// ============================================================================
// EmulatorConfig
// ============================================================================

class EmulatorConfig {
public:
    int MemorySize = 48 * 1024 * 1024; // 48MB default
    std::vector<MemoryRegionConfig> MemoryRegions;
    uint32_t ConsoleBaseAddress = 0x00FF0000;
    uint32_t HddBaseAddress = 0x00FF1000;
    bool ConsoleEnabled = true;
    bool HddEnabled = true;
    std::string HddImagePath;
    std::string FontFamily = "Consolas";
    double FontSize = 14.0;
    std::string LastOpenedFile;
    uint32_t LastLoadAddress = 0x1000;

    // Board type: "Generic" or "MVME147"
    std::string BoardType = "Generic";

    std::string Mvme147RomPath;
    std::vector<ScsiDiskConfig> Mvme147ScsiDisks;
    std::string Mvme147ScsiCdromPath;
    int Mvme147ScsiCdromId = 3;

    // Network mode: "Virtual" (internal echo server) or "NAT" (host network via user-mode NAT)
    std::string NetworkMode = "Virtual";

    // Console scrollback buffer size (lines). Range: 0..100000
    int ConsoleScrollbackLines = 2000;

    // Load configuration from appsettings.json next to the executable.
    // Returns default config on failure.
    static EmulatorConfig Load();

    // Save configuration to appsettings.json next to the executable.
    void Save() const;

    // Deep-copy via JSON round-trip.
    EmulatorConfig Clone() const;

private:
    static std::filesystem::path GetConfigPath();
};

void to_json(nlohmann::json& j, const EmulatorConfig& c);
void from_json(const nlohmann::json& j, EmulatorConfig& c);

} // namespace Em68030::Config
