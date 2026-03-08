#include "pch.h"

#include "Mk48t02Device.h"
#include <chrono>
#include <ctime>

namespace Em68030::IO {

Mk48t02Device::Mk48t02Device()
{
    m_nvram.fill(0);
}

uint8_t Mk48t02Device::ReadByte(uint32_t address)
{
    uint32_t offset = address - BaseAddress;
    if (offset >= 2048) return 0xFF;

    if (offset >= 0x7F8)
        return ReadClockRegister(offset - 0x7F8);

    return m_nvram[offset];
}

uint16_t Mk48t02Device::ReadWord(uint32_t address)
{
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t Mk48t02Device::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void Mk48t02Device::WriteByte(uint32_t address, uint8_t value)
{
    uint32_t offset = address - BaseAddress;
    if (offset >= 2048) return;

    m_nvram[offset] = value;
}

void Mk48t02Device::WriteWord(uint32_t address, uint16_t value)
{
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void Mk48t02Device::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

uint8_t Mk48t02Device::ReadClockRegister(uint32_t reg)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    gmtime_s(&local_tm, &t);
#else
    gmtime_r(&t, &local_tm);
#endif

    switch (reg) {
        case 0: return m_nvram[0x7F8]; // Control register
        case 1: return ToBcd(local_tm.tm_sec);
        case 2: return ToBcd(local_tm.tm_min);
        case 3: return ToBcd(local_tm.tm_hour);
        case 4: return static_cast<uint8_t>(local_tm.tm_wday + 1); // Sunday=1
        case 5: return ToBcd(local_tm.tm_mday);
        case 6: return ToBcd(local_tm.tm_mon + 1); // tm_mon is 0-based
        case 7: return ToBcd((local_tm.tm_year - m_yearOffset) % 100);
        default: return 0;
    }
}

uint8_t Mk48t02Device::ToBcd(int val)
{
    return static_cast<uint8_t>(((val / 10) << 4) | (val % 10));
}

void Mk48t02Device::SetMvme147Config(uint32_t onboardRamEnd, const uint8_t* ethernetAddr, size_t ethernetAddrLen)
{
    // Offset $0774: End+1 of onboard memory (32-bit, big-endian)
    m_nvram[0x0774] = static_cast<uint8_t>(onboardRamEnd >> 24);
    m_nvram[0x0775] = static_cast<uint8_t>(onboardRamEnd >> 16);
    m_nvram[0x0776] = static_cast<uint8_t>(onboardRamEnd >> 8);
    m_nvram[0x0777] = static_cast<uint8_t>(onboardRamEnd);

    // Offset $0764: Start of offboard RAM (0 = none)
    m_nvram[0x0764] = 0;
    m_nvram[0x0765] = 0;
    m_nvram[0x0766] = 0;
    m_nvram[0x0767] = 0;

    // Offset $0768: End of offboard RAM (0 = none)
    m_nvram[0x0768] = 0;
    m_nvram[0x0769] = 0;
    m_nvram[0x076A] = 0;
    m_nvram[0x076B] = 0;

    // Offset $0778: Ethernet address bytes
    if (ethernetAddrLen >= 3) {
        m_nvram[0x0778] = ethernetAddr[0];
        m_nvram[0x0779] = ethernetAddr[1];
        m_nvram[0x077A] = ethernetAddr[2];
    }
}

} // namespace Em68030::IO
