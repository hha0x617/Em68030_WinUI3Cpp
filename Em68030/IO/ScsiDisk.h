#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <functional>

namespace Em68030::IO {

// ============================================================================
// ScsiResult
// ============================================================================

struct ScsiResult {
    uint8_t StatusByte = 0;           // 0x00=GOOD, 0x02=CHECK_CONDITION
    std::vector<uint8_t> DataIn;      // DATA_IN phase data
    int DataInLength = 0;             // Actual data length
    std::vector<uint8_t> DataOut;     // DATA_OUT buffer
    int DataOutLength = 0;            // Expected DATA_OUT byte count
    bool HasDataIn = false;           // DATA_IN phase present
    bool HasDataOut = false;          // DATA_OUT phase present
};

// ============================================================================
// IScsiTarget
// ============================================================================

class IScsiTarget {
public:
    virtual ~IScsiTarget() = default;

    virtual bool IsReady() const = 0;
    virtual ScsiResult ProcessCommand(const uint8_t* cdb, int cdbLength, int lun = 0) = 0;
    virtual void CompleteWrite(uint32_t lba, const uint8_t* data, int length) = 0;
};

// ============================================================================
// ScsiDisk
// ============================================================================

/// SCSI disk target device. Processes SCSI CDBs against a file-backed disk image.
/// Sector size is fixed at 512 bytes.
class ScsiDisk : public IScsiTarget {
public:
    ScsiDisk();
    ~ScsiDisk();

    bool IsReady() const override;
    ScsiResult ProcessCommand(const uint8_t* cdb, int cdbLength, int lun = 0) override;
    void CompleteWrite(uint32_t lba, const uint8_t* data, int length) override;

    void MountImage(const std::string& path);
    void UnmountImage();

    /// Write a NetBSD/mvme68k cpu_disklabel to sector 0 of a disk image.
    static void WriteNetBsdDisklabel(const std::string& path);

    std::function<void(const std::string&)> DiagLog;

private:
    static constexpr int SectorSize = 512;

    ScsiResult CmdTestUnitReady();
    ScsiResult CmdRequestSense(const uint8_t* cdb);
    ScsiResult CmdInquiryNoDevice(const uint8_t* cdb);
    ScsiResult CmdInquiry(const uint8_t* cdb);
    ScsiResult CmdModeSelect6(const uint8_t* cdb);
    ScsiResult CmdModeSense6(const uint8_t* cdb);
    ScsiResult CmdModeSense10(const uint8_t* cdb);
    ScsiResult CmdModeSelect10(const uint8_t* cdb);
    ScsiResult CmdStartStopUnit();
    ScsiResult CmdPreventAllowRemoval();
    ScsiResult CmdSynchronizeCache();
    ScsiResult CmdReadCapacity();
    ScsiResult CmdRead6(const uint8_t* cdb);
    ScsiResult CmdRead10(const uint8_t* cdb);
    ScsiResult CmdWrite6(const uint8_t* cdb);
    ScsiResult CmdWrite10(const uint8_t* cdb);

    ScsiResult DoRead(uint32_t lba, int sectorCount);
    ScsiResult DoWrite(uint32_t lba, int sectorCount);
    ScsiResult MakeCheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq);
    void ClearSense();

    std::vector<uint8_t> BuildModePages(int pageCode);

    static void SetString(uint8_t* buf, int offset, int maxLen, const char* s);
    static void PutBE32(uint8_t* buf, int offset, uint32_t value);
    static void PutBE16(uint8_t* buf, int offset, uint16_t value);
    static void SetLabelString(uint8_t* buf, int offset, int maxLen, const char* s);

    std::fstream m_imageStream;
    int64_t m_totalSectors = 0;
    uint8_t m_senseData[18]{};
};

} // namespace Em68030::IO
