// Copyright 2026 hha0x617
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

    // Boot partition: 0='a', 1='b', etc. Used by boot stub to tell kernel which partition is root.
    int Mvme147BootPartition = 0;

    // Target OS: "NetBSD" or "Linux"
    std::string TargetOS = "NetBSD";

    // Linux kernel command line (used when TargetOS == "Linux")
    std::string LinuxCommandLine = "root=/dev/sda1 earlyprintk";

    // Network mode: "Virtual" (internal echo server) or "NAT" (host network via user-mode NAT)
    std::string NetworkMode = "Virtual";

    // NAT gateway address (shared by SlirpNetworkHandler and VirtualNetworkHandler)
    std::string NatGatewayIp = "10.0.2.2";
    std::string NatGatewayMac = "52:54:00:12:34:56";

    // TAP adapter GUID for bridge mode (e.g. "{A1B2C3D4-E5F6-...}")
    std::string TapAdapterGuid;

    // Console scrollback buffer size (lines). Range: 0..100000
    int ConsoleScrollbackLines = 2000;

    // Console terminal size (columns x rows). Minimum: 80x24
    int ConsoleColumns = 80;
    int ConsoleRows = 24;

    // Framebuffer (for X Window System)
    // VRAM is placed at the top of RAM (auto-calculated: MemorySize - VramSize, 1MB aligned).
    // The kernel is told RAM ends at the VRAM base, so it never touches VRAM.
    bool FramebufferEnabled = false;
    int FramebufferWidth = 640;
    int FramebufferHeight = 480;
    int FramebufferBpp = 16; // 8, 16, or 32

    /// Compute VRAM base address (top of RAM, 1MB aligned).
    uint32_t ComputeVramBase() const {
        uint32_t vramSize = static_cast<uint32_t>(FramebufferWidth) * FramebufferHeight * FramebufferBpp / 8;
        return (static_cast<uint32_t>(MemorySize) - vramSize) & ~0xFFFFFu;
    }

    // JIT compiler (experimental)
    bool JitEnabled = false;
    int JitMinBlockLength = 3;
    int JitCompileThreshold = 32;

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
