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
#include <array>
#include <string>
#include <vector>

#include "IMemoryMappedDevice.h"

namespace Em68030::IO {

/// MK48T02 NVRAM/RTC for MVME147.
/// 2KB SRAM with clock registers at the last 8 bytes ($7F8-$7FF).
/// Mapped at $FFFE0000, 2048 bytes.
///
/// Clock registers (offset from base):
///   $7F8 = Control (bit 7: Write, bit 6: Read)
///   $7F9 = Seconds (BCD, 0-59)
///   $7FA = Minutes (BCD, 0-59)
///   $7FB = Hours   (BCD, 0-23)
///   $7FC = Day of week (1-7)
///   $7FD = Date    (BCD, 1-31)
///   $7FE = Month   (BCD, 1-12)
///   $7FF = Year    (BCD, 0-99)
class Mk48t02Device : public IMemoryMappedDevice {
public:
    Mk48t02Device();

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    /// Pre-populate NVRAM with MVME147 hardware configuration values.
    void SetMvme147Config(uint32_t onboardRamEnd, const uint8_t* ethernetAddr, size_t ethernetAddrLen);

    /// Set the year base offset for RTC year register.
    /// NetBSD uses YEAR0=1968: year stored as (year - 1968) % 100.
    /// Linux uses raw 2-digit year: year stored as year % 100.
    /// Default is 0 (Linux/standard).
    void SetYearOffset(int offset) { m_yearOffset = offset; }

    /// Load NVRAM contents from a binary file. Returns true on success.
    bool LoadFromFile(const std::string& path);

    /// Save NVRAM contents to a binary file. Returns true on success.
    bool SaveToFile(const std::string& path) const;

private:
    static constexpr uint32_t BaseAddress = 0xFFFE0000;

    uint8_t ReadClockRegister(uint32_t reg);
    static uint8_t ToBcd(int val);

    std::array<uint8_t, 2048> m_nvram{};
    int m_yearOffset = 0; // 68 for NetBSD (YEAR0=1968), 0 for Linux
};

} // namespace Em68030::IO
