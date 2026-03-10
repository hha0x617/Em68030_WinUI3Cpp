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
#include <tuple>
#include <stdexcept>

namespace Em68030::Core {
class Memory;
}

namespace Em68030::IO {

// ============================================================================
// LstLine
// ============================================================================

struct LstLine {
    std::string RawText;
    uint32_t Address = 0;
    bool HasAddress = false;
};

// ============================================================================
// ElfLoadResult
// ============================================================================

struct ElfLoadResult {
    uint32_t EntryPoint = 0;
    uint32_t StartAddress = 0;
    uint32_t EndAddress = 0;
    uint16_t Machine = 0;
    int SegmentsLoaded = 0;

    std::string MachineDescription() const {
        switch (Machine) {
            case 4: return "MC68000";
            default: return "Unknown (" + std::to_string(Machine) + ")";
        }
    }
};

// ============================================================================
// SRecordLoadResult
// ============================================================================

struct SRecordLoadResult {
    uint32_t StartAddress = 0;
    uint32_t EndAddress = 0;
    uint32_t EntryPoint = 0;
    bool HasEntryPoint = false;
};

// ============================================================================
// FileLoader (static utility class)
// ============================================================================

class FileLoader {
public:
    FileLoader() = delete;

    static uint32_t LoadBinary(Core::Memory& memory, const std::string& filePath, uint32_t loadAddress);

    static SRecordLoadResult LoadSRecord(Core::Memory& memory, const std::string& filePath);

    static ElfLoadResult LoadElf(Core::Memory& memory, const std::string& filePath);

    /// Check if a file looks like an ELF binary (by magic number).
    static bool IsElfFile(const std::string& filePath);

    /// Find corresponding .lst file for a given file path.
    static std::string FindLstFile(const std::string& filePath);

    /// Load a .lst file and parse addresses.
    static std::vector<LstLine> LoadLstFile(const std::string& lstPath);

private:
    static LstLine ParseLstLine(const std::string& line);

    static uint16_t ReadU16(const uint8_t* data, uint32_t offset, bool bigEndian);
    static uint32_t ReadU32(const uint8_t* data, uint32_t offset, bool bigEndian);
};

} // namespace Em68030::IO
