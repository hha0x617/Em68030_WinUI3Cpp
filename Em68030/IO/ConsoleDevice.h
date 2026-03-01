#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <string>

#include "IMemoryMappedDevice.h"

namespace Em68030::Core {
class MC68030;
}

namespace Em68030::IO {

class ConsoleDevice : public IMemoryMappedDevice {
public:
    explicit ConsoleDevice(uint32_t baseAddress = 0x00FF0000);

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    // TRAP #15 handler
    void HandleTrap(Core::MC68030& cpu);

    // Callbacks
    std::function<void(char)> CharOutput;
    std::function<void(const std::string&)> StringOutput;
    std::function<char()> CharInput;
    std::function<std::string()> StringInput;

    uint32_t GetBaseAddress() const { return m_baseAddress; }
    void SetBaseAddress(uint32_t addr) { m_baseAddress = addr; }

private:
    uint32_t m_baseAddress;
    std::array<uint8_t, 256> m_registers{};
};

} // namespace Em68030::IO
