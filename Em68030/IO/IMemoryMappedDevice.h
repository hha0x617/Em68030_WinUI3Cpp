#pragma once
#include <cstdint>

namespace Em68030::IO {

class IMemoryMappedDevice {
public:
    virtual ~IMemoryMappedDevice() = default;

    virtual uint8_t ReadByte(uint32_t address) = 0;
    virtual uint16_t ReadWord(uint32_t address) = 0;
    virtual uint32_t ReadLong(uint32_t address) = 0;
    virtual void WriteByte(uint32_t address, uint8_t value) = 0;
    virtual void WriteWord(uint32_t address, uint16_t value) = 0;
    virtual void WriteLong(uint32_t address, uint32_t value) = 0;
};

} // namespace Em68030::IO
