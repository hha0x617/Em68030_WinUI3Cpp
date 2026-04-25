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

#include "pch.h"

#include "Memory.h"
#include "MC68030.h"
#include "BusErrorException.h"

namespace Em68030::Core {

// ============================================================================
// MemoryRegion
// ============================================================================

MemoryRegion::MemoryRegion(uint32_t baseAddress, int size, RegionType type)
    : m_baseAddress(baseAddress), m_size(size), m_type(type), m_data(static_cast<size_t>(size), 0)
{
}

// ============================================================================
// Memory -- construction
// ============================================================================

Memory::Memory()
{
}

Memory::Memory(int sizeBytes)
{
    AddRegion(0, sizeBytes, RegionType::Ram);
}

// ============================================================================
// Memory -- properties
// ============================================================================

int Memory::GetSize() const
{
    if (m_regions.empty()) return 0;
    uint32_t max = 0;
    for (const auto& r : m_regions)
    {
        uint32_t end = r.GetBaseAddress() + static_cast<uint32_t>(r.GetSize());
        if (end > max) max = end;
    }
    return static_cast<int>(max);
}

// ============================================================================
// Memory -- region / device management
// ============================================================================

void Memory::AddRegion(uint32_t baseAddress, int size, RegionType type)
{
    m_regions.emplace_back(baseAddress, size, type);
    // Cache base-0 RAM for fast path
    if (baseAddress == 0 && type == RegionType::Ram)
    {
        m_fastRam = m_regions.back().GetData().data();
        m_fastRamSize = static_cast<uint32_t>(size);
        UpdateFastRamLimit();
    }
}

void Memory::RegisterDevice(uint32_t baseAddress, uint32_t size, IO::IMemoryMappedDevice* device)
{
    for (uint32_t i = 0; i < size; i += 4)
    {
        m_deviceMap[baseAddress + i] = device;
    }
    if (baseAddress < m_deviceMinAddr)
    {
        m_deviceMinAddr = baseAddress;
        UpdateFastRamLimit();
    }
}

void Memory::UnregisterDevice(uint32_t baseAddress, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 4)
    {
        m_deviceMap.erase(baseAddress + i);
    }
}

inline MemoryRegion* Memory::FindRegion(uint32_t address)
{
    // Check last-hit cache first (covers repeated ROM accesses)
    auto cached = m_lastRegion;
    if (cached != nullptr && cached->Contains(address))
        return cached;
    for (size_t i = 0; i < m_regions.size(); i++)
    {
        if (m_regions[i].Contains(address))
        {
            m_lastRegion = &m_regions[i];
            return &m_regions[i];
        }
    }
    return nullptr;
}

IO::IMemoryMappedDevice* Memory::FindDevice(uint32_t address)
{
    uint32_t aligned = address & 0xFFFFFFFC;
    auto it = m_deviceMap.find(aligned);
    if (it != m_deviceMap.end())
        return it->second;
    return nullptr;
}

// ============================================================================
// Big-endian helpers
// ============================================================================

namespace {

inline uint16_t ReadUInt16BigEndian(const uint8_t* ptr)
{
    return static_cast<uint16_t>((ptr[0] << 8) | ptr[1]);
}

inline uint32_t ReadUInt32BigEndian(const uint8_t* ptr)
{
    return (static_cast<uint32_t>(ptr[0]) << 24) |
           (static_cast<uint32_t>(ptr[1]) << 16) |
           (static_cast<uint32_t>(ptr[2]) << 8)  |
            static_cast<uint32_t>(ptr[3]);
}

inline void WriteUInt16BigEndian(uint8_t* ptr, uint16_t value)
{
    ptr[0] = static_cast<uint8_t>(value >> 8);
    ptr[1] = static_cast<uint8_t>(value & 0xFF);
}

inline void WriteUInt32BigEndian(uint8_t* ptr, uint32_t value)
{
    ptr[0] = static_cast<uint8_t>(value >> 24);
    ptr[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    ptr[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    ptr[3] = static_cast<uint8_t>(value & 0xFF);
}

} // anonymous namespace

// ============================================================================
// Read/Write -- CPU execution (bus error on unmapped access)
// ============================================================================

// Helper: report an unmapped-physical-address fault on the attached CPU and
// return a benign default. If no CPU is attached (e.g., unit tests driving
// Memory directly), the LastFault* fields still record it so tests can
// EXPECT it without catching an exception.
void Memory::RaisePhysicalBusError(uint32_t address, bool isWrite)
{
    LastFaultAddress = address;
    LastFaultIsWrite = isWrite;
    LastFaultRaised  = true;
    if (m_cpu) m_cpu->SetBusError(address, isWrite, 0, 0);
}

uint8_t Memory::ReadByte(uint32_t address)
{
    // Fast path: address in base-0 RAM and below any device range
    if (address < m_fastRamLimit)
        return m_fastRam[address];

    auto device = FindDevice(address);
    if (device != nullptr)
        return device->ReadByte(address);

    auto region = FindRegion(address);
    if (region != nullptr)
        return region->GetData()[address - region->GetBaseAddress()];

    RaisePhysicalBusError(address, false);
    return 0;
}

uint16_t Memory::ReadWord(uint32_t address)
{
    if (address + 1 < m_fastRamLimit)
        return ReadUInt16BigEndian(m_fastRam + address);

    auto device = FindDevice(address);
    if (device != nullptr)
        return device->ReadWord(address);

    auto region = FindRegion(address);
    if (region != nullptr)
    {
        uint32_t offset = address - region->GetBaseAddress();
        if (offset + 1 < static_cast<uint32_t>(region->GetSize()))
            return static_cast<uint16_t>((region->GetData()[offset] << 8) | region->GetData()[offset + 1]);
    }

    RaisePhysicalBusError(address, false);
    return 0;
}

uint32_t Memory::ReadLong(uint32_t address)
{
    if (address + 3 < m_fastRamLimit)
        return ReadUInt32BigEndian(m_fastRam + address);

    auto device = FindDevice(address);
    if (device != nullptr)
        return device->ReadLong(address);

    auto region = FindRegion(address);
    if (region != nullptr)
    {
        uint32_t offset = address - region->GetBaseAddress();
        if (offset + 3 < static_cast<uint32_t>(region->GetSize()))
        {
            auto& data = region->GetData();
            return (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                    static_cast<uint32_t>(data[offset + 3]);
        }
    }

    RaisePhysicalBusError(address, false);
    return 0;
}

void Memory::WriteByte(uint32_t address, uint8_t value)
{
    if (address < m_fastRamLimit)
    {
        m_fastRam[address] = value;
        return;
    }

    auto device = FindDevice(address);
    if (device != nullptr)
    {
        device->WriteByte(address, value);
        return;
    }

    auto region = FindRegion(address);
    if (region == nullptr)
    {
        RaisePhysicalBusError(address, true);
        return;
    }

    if (region->GetType() == RegionType::Rom)
        return;

    region->GetData()[address - region->GetBaseAddress()] = value;
}

void Memory::WriteWord(uint32_t address, uint16_t value)
{
    if (address + 1 < m_fastRamLimit)
    {
        WriteUInt16BigEndian(m_fastRam + address, value);
        return;
    }

    auto device = FindDevice(address);
    if (device != nullptr)
    {
        device->WriteWord(address, value);
        return;
    }

    auto region = FindRegion(address);
    if (region == nullptr)
    {
        RaisePhysicalBusError(address, true);
        return;
    }

    if (region->GetType() == RegionType::Rom)
        return;

    uint32_t offset = address - region->GetBaseAddress();
    if (offset + 1 < static_cast<uint32_t>(region->GetSize()))
    {
        region->GetData()[offset] = static_cast<uint8_t>(value >> 8);
        region->GetData()[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    }
}

void Memory::WriteLong(uint32_t address, uint32_t value)
{
    if (address + 3 < m_fastRamLimit)
    {
        WriteUInt32BigEndian(m_fastRam + address, value);
        return;
    }

    auto device = FindDevice(address);
    if (device != nullptr)
    {
        device->WriteLong(address, value);
        return;
    }

    auto region = FindRegion(address);
    if (region == nullptr)
    {
        RaisePhysicalBusError(address, true);
        return;
    }

    if (region->GetType() == RegionType::Rom)
        return;

    uint32_t offset = address - region->GetBaseAddress();
    if (offset + 3 < static_cast<uint32_t>(region->GetSize()))
    {
        auto& data = region->GetData();
        data[offset]     = static_cast<uint8_t>(value >> 24);
        data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data[offset + 3] = static_cast<uint8_t>(value & 0xFF);
    }
}

// ============================================================================
// Peek/Poke -- Debugger/loader (no exceptions, ROM writable)
// ============================================================================

uint8_t Memory::PeekByte(uint32_t address)
{
    auto device = FindDevice(address);
    if (device != nullptr)
        return device->ReadByte(address);

    auto region = FindRegion(address);
    if (region != nullptr)
        return region->GetData()[address - region->GetBaseAddress()];

    return 0xFF;
}

uint16_t Memory::PeekWord(uint32_t address)
{
    auto device = FindDevice(address);
    if (device != nullptr)
        return device->ReadWord(address);

    auto region = FindRegion(address);
    if (region != nullptr)
    {
        uint32_t offset = address - region->GetBaseAddress();
        if (offset + 1 < static_cast<uint32_t>(region->GetSize()))
            return static_cast<uint16_t>((region->GetData()[offset] << 8) | region->GetData()[offset + 1]);
    }

    return 0xFFFF;
}

uint32_t Memory::PeekLong(uint32_t address)
{
    auto device = FindDevice(address);
    if (device != nullptr)
        return device->ReadLong(address);

    auto region = FindRegion(address);
    if (region != nullptr)
    {
        uint32_t offset = address - region->GetBaseAddress();
        if (offset + 3 < static_cast<uint32_t>(region->GetSize()))
        {
            auto& data = region->GetData();
            return (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                    static_cast<uint32_t>(data[offset + 3]);
        }
    }

    return 0xFFFFFFFF;
}

void Memory::PokeByte(uint32_t address, uint8_t value)
{
    auto region = FindRegion(address);
    if (region != nullptr)
        region->GetData()[address - region->GetBaseAddress()] = value;
}

void Memory::PokeWord(uint32_t address, uint16_t value)
{
    auto region = FindRegion(address);
    if (region != nullptr)
    {
        uint32_t offset = address - region->GetBaseAddress();
        if (offset + 1 < static_cast<uint32_t>(region->GetSize()))
        {
            region->GetData()[offset] = static_cast<uint8_t>(value >> 8);
            region->GetData()[offset + 1] = static_cast<uint8_t>(value & 0xFF);
        }
    }
}

void Memory::PokeLong(uint32_t address, uint32_t value)
{
    auto region = FindRegion(address);
    if (region != nullptr)
    {
        uint32_t offset = address - region->GetBaseAddress();
        if (offset + 3 < static_cast<uint32_t>(region->GetSize()))
        {
            auto& data = region->GetData();
            data[offset]     = static_cast<uint8_t>(value >> 24);
            data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
            data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
            data[offset + 3] = static_cast<uint8_t>(value & 0xFF);
        }
    }
}

// ============================================================================
// Bulk operations (debugger/loader)
// ============================================================================

void Memory::LoadData(uint32_t address, const std::vector<uint8_t>& data)
{
    for (size_t i = 0; i < data.size(); i++)
    {
        PokeByte(address + static_cast<uint32_t>(i), data[i]);
    }
}

void Memory::LoadData(uint32_t address, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        PokeByte(address + static_cast<uint32_t>(i), data[i]);
    }
}

std::vector<uint8_t> Memory::GetRange(uint32_t address, int length)
{
    std::vector<uint8_t> result(static_cast<size_t>(length));
    for (int i = 0; i < length; i++)
    {
        result[i] = PeekByte(address + static_cast<uint32_t>(i));
    }
    return result;
}

} // namespace Em68030::Core
