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
#include <vector>
#include <unordered_map>
#include <limits>

#include "BusErrorException.h"
#include "../IO/IMemoryMappedDevice.h"

namespace Em68030::Core {

// ============================================================================
// RegionType
// ============================================================================

enum class RegionType { Ram, Rom };

// ============================================================================
// MemoryRegion
// ============================================================================

class MemoryRegion {
public:
    MemoryRegion(uint32_t baseAddress, int size, RegionType type);

    uint32_t GetBaseAddress() const { return m_baseAddress; }
    int GetSize() const { return m_size; }
    RegionType GetType() const { return m_type; }
    std::vector<uint8_t>& GetData() { return m_data; }
    const std::vector<uint8_t>& GetData() const { return m_data; }

    bool Contains(uint32_t address) const {
        return address >= m_baseAddress && address < m_baseAddress + static_cast<uint32_t>(m_size);
    }

private:
    uint32_t m_baseAddress;
    int m_size;
    RegionType m_type;
    std::vector<uint8_t> m_data;
};

// ============================================================================
// Memory
// ============================================================================

class MC68030;  // forward — for SetBusError back-ref

class Memory {
public:
    Memory();
    explicit Memory(int sizeBytes);

    int GetSize() const;

    void AddRegion(uint32_t baseAddress, int size, RegionType type);

    void RegisterDevice(uint32_t baseAddress, uint32_t size, IO::IMemoryMappedDevice* device);
    void UnregisterDevice(uint32_t baseAddress, uint32_t size);

    // Owning CPU — set by MC68030's ctor. Used to report unmapped-address
    // faults via MC68030::SetBusError instead of throwing.
    void SetCpu(MC68030* cpu) { m_cpu = cpu; }

    // Last fault info — mirrors what was sent to the CPU's SetBusError.
    // Set unconditionally (even when no CPU is attached) so unit tests that
    // drive Memory directly can assert "the access raised a fault" without
    // catching an exception.
    uint32_t LastFaultAddress = 0;
    bool     LastFaultIsWrite = false;
    bool     LastFaultRaised  = false;  // cleared only by tests; set on every fault

    // ========================================================================
    // Read/Write -- CPU execution (sets CPU bus error flag on unmapped access)
    // ========================================================================

    uint8_t ReadByte(uint32_t address);
    uint16_t ReadWord(uint32_t address);
    uint32_t ReadLong(uint32_t address);

    void WriteByte(uint32_t address, uint8_t value);
    void WriteWord(uint32_t address, uint16_t value);
    void WriteLong(uint32_t address, uint32_t value);

    // ========================================================================
    // Peek/Poke -- Debugger/loader (no exceptions, ROM writable)
    // ========================================================================

    uint8_t PeekByte(uint32_t address);
    uint16_t PeekWord(uint32_t address);
    uint32_t PeekLong(uint32_t address);

    void PokeByte(uint32_t address, uint8_t value);
    void PokeWord(uint32_t address, uint16_t value);
    void PokeLong(uint32_t address, uint32_t value);

    // ========================================================================
    // Bulk operations (debugger/loader)
    // ========================================================================

    void LoadData(uint32_t address, const std::vector<uint8_t>& data);
    void LoadData(uint32_t address, const uint8_t* data, size_t length);
    std::vector<uint8_t> GetRange(uint32_t address, int length);

private:
    MemoryRegion* FindRegion(uint32_t address);
    IO::IMemoryMappedDevice* FindDevice(uint32_t address);

    void RaisePhysicalBusError(uint32_t address, bool isWrite);

    std::vector<MemoryRegion> m_regions;
    std::unordered_map<uint32_t, IO::IMemoryMappedDevice*> m_deviceMap;

    // Fast path: direct pointer to base-0 RAM region (avoids FindRegion/FindDevice per access)
    uint8_t* m_fastRam = nullptr;
    uint32_t m_fastRamSize = 0;
    uint32_t m_deviceMinAddr = std::numeric_limits<uint32_t>::max(); // Lowest device-mapped address
    uint32_t m_fastRamLimit = 0; // min(m_fastRamSize, m_deviceMinAddr) — single-compare fast path

    void UpdateFastRamLimit() {
        m_fastRamLimit = (m_fastRamSize < m_deviceMinAddr) ? m_fastRamSize : m_deviceMinAddr;
    }

    // Last-hit region cache for non-fastRAM accesses (e.g., ROM at $FF800000)
    MemoryRegion* m_lastRegion = nullptr;

    MC68030* m_cpu = nullptr;  // for SetBusError on unmapped-address fault

public:
    /// Direct access to the base-0 RAM array for the framebuffer renderer.
    /// The renderer reads VRAM data on the UI thread — no locking needed.
    const uint8_t* GetFastRamPointer() const { return m_fastRam; }
    uint32_t GetFastRamSize() const { return m_fastRamSize; }
};

} // namespace Em68030::Core
