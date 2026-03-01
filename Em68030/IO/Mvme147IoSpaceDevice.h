#pragma once

#include <cstdint>

#include "IMemoryMappedDevice.h"

namespace Em68030::IO {

/// Catch-all device for unmapped MVME147 I/O space.
/// Prevents bus errors when the kernel probes addresses that don't
/// correspond to specific emulated devices.
/// Returns 0 for reads, ignores writes.
class Mvme147IoSpaceDevice : public IMemoryMappedDevice {
public:
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;
};

} // namespace Em68030::IO
