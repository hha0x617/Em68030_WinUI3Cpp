#include "pch.h"

#include "ConsoleDevice.h"
#include "../Core/MC68030.h"
#include <string>

namespace Em68030::IO {

ConsoleDevice::ConsoleDevice(uint32_t baseAddress)
    : m_baseAddress(baseAddress)
{
    m_registers.fill(0);
}

// Console I/O register offsets:
// 0x00: Data register (R/W) - character data
// 0x04: Status register (R) - bit 0: data available, bit 1: ready to send
// 0x08: Command register (W) - write to trigger I/O operation

uint8_t ConsoleDevice::ReadByte(uint32_t address)
{
    uint32_t offset = address - m_baseAddress;
    if (offset < m_registers.size())
        return m_registers[offset];
    return 0;
}

uint16_t ConsoleDevice::ReadWord(uint32_t address)
{
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t ConsoleDevice::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void ConsoleDevice::WriteByte(uint32_t address, uint8_t value)
{
    uint32_t offset = address - m_baseAddress;
    if (offset < m_registers.size())
        m_registers[offset] = value;

    if (offset == 0) // Data register - output character
    {
        if (CharOutput)
            CharOutput(static_cast<char>(value));
    }
}

void ConsoleDevice::WriteWord(uint32_t address, uint16_t value)
{
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void ConsoleDevice::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

void ConsoleDevice::HandleTrap(Core::MC68030& cpu)
{
    uint32_t function = cpu.D[0];

    switch (function)
    {
        case 0: // Display null-terminated string at (A1)
        {
            std::string sb;
            uint32_t addr = cpu.A[1];
            uint8_t ch;
            while ((ch = cpu.ReadByte(addr)) != 0) {
                sb += static_cast<char>(ch);
                addr++;
            }
            if (StringOutput)
                StringOutput(sb);
            break;
        }

        case 1: // Read one character -> D1.B
        {
            char ch = CharInput ? CharInput() : '\0';
            cpu.D[1] = (cpu.D[1] & 0xFFFFFF00) | static_cast<uint8_t>(ch);
            break;
        }

        case 2: // Display number in D1.L
            if (StringOutput)
                StringOutput(std::to_string(static_cast<int32_t>(cpu.D[1])));
            break;

        case 3: // Read string to buffer at (A1)
        {
            if (StringInput) {
                std::string input = StringInput();
                uint32_t addr = cpu.A[1];
                for (char c : input) {
                    cpu.WriteByte(addr, static_cast<uint8_t>(c));
                    addr++;
                }
                cpu.WriteByte(addr, 0); // null terminate
            }
            break;
        }

        case 4: // Read number -> D1.L
        {
            if (StringInput) {
                std::string input = StringInput();
                try {
                    int val = std::stoi(input);
                    cpu.D[1] = static_cast<uint32_t>(val);
                } catch (...) {}
            }
            break;
        }

        case 5: // Display character in D1.B
            if (CharOutput)
                CharOutput(static_cast<char>(cpu.D[1] & 0xFF));
            break;

        case 9: // Program termination
            cpu.Halted = true;
            cpu.StopReason = "Program terminated (TRAP #15, D0=9)";
            break;
    }
}

} // namespace Em68030::IO
