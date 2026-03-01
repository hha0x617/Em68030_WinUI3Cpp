#pragma once

#include <cstdint>
#include <string>
#include <fstream>

#include "IMemoryMappedDevice.h"

namespace Em68030::Core {
class Memory;
}

namespace Em68030::IO {

class HddDevice : public IMemoryMappedDevice {
public:
    static constexpr int SectorSize = 512;

    explicit HddDevice(uint32_t baseAddress = 0x00FF1000);
    ~HddDevice();

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    void AttachMemory(Core::Memory* memory);
    void MountImage(const std::string& path);
    void UnmountImage();
    static void CreateImage(const std::string& path, int64_t sizeBytes);

    bool IsImageLoaded() const { return m_imageStream.is_open(); }
    const std::string& GetImagePath() const { return m_imagePath; }

    uint32_t GetBaseAddress() const { return m_baseAddress; }
    void SetBaseAddress(uint32_t addr) { m_baseAddress = addr; }

private:
    // Register offsets
    static constexpr int RegCommand = 0x00;
    static constexpr int RegLBA     = 0x04;
    static constexpr int RegStatus  = 0x08;
    static constexpr int RegDmaAddr = 0x0C;

    // Commands
    static constexpr uint32_t CmdNop    = 0;
    static constexpr uint32_t CmdRead   = 1;
    static constexpr uint32_t CmdWrite  = 2;
    static constexpr uint32_t CmdStatus = 3;

    // Status bits
    static constexpr uint32_t StatusReady = 0x01;
    static constexpr uint32_t StatusError = 0x02;

    void ExecuteCommand();
    void ReadSector();
    void WriteSector();

    uint32_t m_baseAddress;
    std::fstream m_imageStream;
    std::string m_imagePath;
    Core::Memory* m_memory = nullptr;

    // Internal registers
    uint32_t m_command = 0;
    uint32_t m_lba = 0;
    uint32_t m_status = StatusReady;
    uint32_t m_dmaAddr = 0;
};

} // namespace Em68030::IO
