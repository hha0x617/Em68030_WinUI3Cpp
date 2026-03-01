#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

#include "ScsiDisk.h" // For IScsiTarget and ScsiResult

namespace Em68030::IO {

/// SCSI CD-ROM target device. Processes SCSI CDBs against an ISO 9660 image file.
/// Sector size is fixed at 2048 bytes (ISO 9660 standard).
/// Read-only device -- no write commands are supported.
class ScsiCdrom : public IScsiTarget {
public:
    ScsiCdrom();
    ~ScsiCdrom();

    bool IsReady() const override;
    ScsiResult ProcessCommand(const uint8_t* cdb, int cdbLength, int lun = 0) override;
    void CompleteWrite(uint32_t lba, const uint8_t* data, int length) override;

    void MountImage(const std::string& path);
    void UnmountImage();

private:
    static constexpr int SectorSize = 2048;

    ScsiResult CmdTestUnitReady();
    ScsiResult CmdRequestSense(const uint8_t* cdb);
    ScsiResult CmdInquiryNoDevice(const uint8_t* cdb);
    ScsiResult CmdInquiry(const uint8_t* cdb);
    ScsiResult CmdModeSense6(const uint8_t* cdb);
    ScsiResult CmdModeSense10(const uint8_t* cdb);
    ScsiResult CmdStartStopUnit();
    ScsiResult CmdPreventAllowRemoval();
    ScsiResult CmdReadCapacity();
    ScsiResult CmdRead6(const uint8_t* cdb);
    ScsiResult CmdRead10(const uint8_t* cdb);
    ScsiResult CmdReadToc(const uint8_t* cdb);

    ScsiResult DoRead(uint32_t lba, int sectorCount);
    ScsiResult MakeCheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq);
    void ClearSense();

    static void SetString(uint8_t* buf, int offset, int maxLen, const char* s);
    static std::vector<uint8_t> TrimData(const std::vector<uint8_t>& data, int len);
    static void LbaToMsf(uint32_t lba, uint8_t& m, uint8_t& s, uint8_t& f);

    std::ifstream m_imageStream;
    int64_t m_totalSectors = 0;
    uint8_t m_senseData[18]{};
    bool m_mediaChanged = false; // Set on mount/unmount to trigger UNIT ATTENTION
};

} // namespace Em68030::IO
