#include "pch.h"

#include "HddDevice.h"
#include "../Core/Memory.h"
#include <algorithm>

namespace Em68030::IO {

HddDevice::HddDevice(uint32_t baseAddress)
    : m_baseAddress(baseAddress)
{
}

HddDevice::~HddDevice()
{
    if (m_imageStream.is_open())
        m_imageStream.close();
}

void HddDevice::AttachMemory(Core::Memory* memory)
{
    m_memory = memory;
}

void HddDevice::MountImage(const std::string& path)
{
    if (m_imageStream.is_open())
        m_imageStream.close();

    m_imagePath = path;
    m_imageStream.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!m_imageStream.is_open()) {
        // Try creating the file first
        std::ofstream create(path, std::ios::binary);
        create.close();
        m_imageStream.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    m_status = StatusReady;
}

void HddDevice::UnmountImage()
{
    if (m_imageStream.is_open())
        m_imageStream.close();
    m_imagePath.clear();
    m_status = StatusError;
}

void HddDevice::CreateImage(const std::string& path, int64_t sizeBytes)
{
    std::ofstream fs(path, std::ios::binary);
    if (fs.is_open() && sizeBytes > 0) {
        fs.seekp(sizeBytes - 1);
        fs.put(0);
    }
}

uint8_t HddDevice::ReadByte(uint32_t address)
{
    uint32_t offset = address - m_baseAddress;
    uint32_t regValue = 0;
    switch (offset & 0xFC) {
        case 0x00: regValue = m_command; break;
        case 0x04: regValue = m_lba; break;
        case 0x08: regValue = m_status; break;
        case 0x0C: regValue = m_dmaAddr; break;
        default: return 0;
    }
    int byteOffset = static_cast<int>(3 - (offset & 3));
    return static_cast<uint8_t>(regValue >> (byteOffset * 8));
}

uint16_t HddDevice::ReadWord(uint32_t address)
{
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t HddDevice::ReadLong(uint32_t address)
{
    uint32_t offset = address - m_baseAddress;
    switch (offset) {
        case 0x00: return m_command;
        case 0x04: return m_lba;
        case 0x08: return m_status;
        case 0x0C: return m_dmaAddr;
        default: return 0;
    }
}

void HddDevice::WriteByte(uint32_t address, uint8_t value)
{
    uint32_t offset = address - m_baseAddress;
    int byteOffset = static_cast<int>(3 - (offset & 3));
    uint32_t aligned = offset & 0xFFFC;
    uint32_t mask = ~(0xFFu << (byteOffset * 8));
    uint32_t val = static_cast<uint32_t>(value) << (byteOffset * 8);

    switch (aligned) {
        case 0x00: m_command = (m_command & mask) | val; break;
        case 0x04: m_lba = (m_lba & mask) | val; break;
        case 0x0C: m_dmaAddr = (m_dmaAddr & mask) | val; break;
    }

    // Execute on command register byte 3 write (last byte)
    if (offset == 0x03)
        ExecuteCommand();
}

void HddDevice::WriteWord(uint32_t address, uint16_t value)
{
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void HddDevice::WriteLong(uint32_t address, uint32_t value)
{
    uint32_t offset = address - m_baseAddress;
    switch (offset) {
        case 0x00:
            m_command = value;
            ExecuteCommand();
            break;
        case 0x04: m_lba = value; break;
        case 0x0C: m_dmaAddr = value; break;
    }
}

void HddDevice::ExecuteCommand()
{
    if (!m_memory || !m_imageStream.is_open()) {
        m_status = StatusError;
        return;
    }

    switch (m_command) {
        case CmdNop:
            break;
        case CmdRead:
            ReadSector();
            break;
        case CmdWrite:
            WriteSector();
            break;
        case CmdStatus:
            // Status already set
            break;
    }

    m_command = CmdNop;
}

void HddDevice::ReadSector()
{
    if (!m_imageStream.is_open() || !m_memory) {
        m_status = StatusError;
        return;
    }

    int64_t offset = static_cast<int64_t>(m_lba) * SectorSize;
    m_imageStream.seekg(0, std::ios::end);
    int64_t fileSize = m_imageStream.tellg();

    if (offset + SectorSize > fileSize) {
        m_status = StatusError;
        return;
    }

    uint8_t buffer[SectorSize];
    m_imageStream.seekg(offset, std::ios::beg);
    m_imageStream.read(reinterpret_cast<char*>(buffer), SectorSize);
    auto bytesRead = m_imageStream.gcount();

    // DMA transfer to memory
    for (int i = 0; i < bytesRead; i++) {
        m_memory->PokeByte(m_dmaAddr + static_cast<uint32_t>(i), buffer[i]);
    }

    m_status = StatusReady;
}

void HddDevice::WriteSector()
{
    if (!m_imageStream.is_open() || !m_memory) {
        m_status = StatusError;
        return;
    }

    int64_t offset = static_cast<int64_t>(m_lba) * SectorSize;

    // Extend file if needed
    m_imageStream.seekg(0, std::ios::end);
    int64_t fileSize = m_imageStream.tellg();
    if (offset + SectorSize > fileSize) {
        // Extend file by seeking and writing
        m_imageStream.seekp(offset + SectorSize - 1, std::ios::beg);
        m_imageStream.put(0);
    }

    uint8_t buffer[SectorSize];
    for (int i = 0; i < SectorSize; i++) {
        buffer[i] = m_memory->PeekByte(m_dmaAddr + static_cast<uint32_t>(i));
    }

    m_imageStream.seekp(offset, std::ios::beg);
    m_imageStream.write(reinterpret_cast<const char*>(buffer), SectorSize);
    m_imageStream.flush();

    m_status = StatusReady;
}

} // namespace Em68030::IO
