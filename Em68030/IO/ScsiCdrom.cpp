#include "pch.h"

#include "ScsiCdrom.h"
#include <algorithm>
#include <cstring>

namespace Em68030::IO {

ScsiCdrom::ScsiCdrom()
{
    ClearSense();
}

ScsiCdrom::~ScsiCdrom()
{
    UnmountImage();
}

bool ScsiCdrom::IsReady() const
{
    return m_imageStream.is_open();
}

void ScsiCdrom::MountImage(const std::string& path)
{
    bool wasOpen = m_imageStream.is_open();
    UnmountImage();
    m_imageStream.open(path, std::ios::in | std::ios::binary);
    if (m_imageStream.is_open()) {
        m_imageStream.seekg(0, std::ios::end);
        int64_t fileSize = m_imageStream.tellg();
        m_totalSectors = fileSize / SectorSize;
        if (m_totalSectors == 0 && fileSize > 0)
            m_totalSectors = 1;
    }
    // Only report UNIT ATTENTION for media changes after initial mount.
    // At power-on, media is already present — no UNIT ATTENTION needed.
    m_mediaChanged = wasOpen;
    ClearSense();
}

void ScsiCdrom::UnmountImage()
{
    if (m_imageStream.is_open())
        m_mediaChanged = true;
    if (m_imageStream.is_open())
        m_imageStream.close();
    m_totalSectors = 0;
}

ScsiResult ScsiCdrom::ProcessCommand(const uint8_t* cdb, int cdbLength, int lun)
{
    uint8_t opcode = cdb[0];

    // INQUIRY and REQUEST SENSE must always work regardless of media state
    // (SCSI standard: these commands never return CHECK CONDITION for
    // UNIT ATTENTION or NOT READY).
    if (lun != 0) {
        if (opcode == 0x12) return CmdInquiryNoDevice(cdb);
        if (opcode == 0x03) return CmdRequestSense(cdb);
        return MakeCheckCondition(0x05, 0x25, 0x00); // LUN NOT SUPPORTED
    }
    if (opcode == 0x12) return CmdInquiry(cdb);
    if (opcode == 0x03) return CmdRequestSense(cdb);

    // Report media change via UNIT ATTENTION
    if (m_mediaChanged)
    {
        m_mediaChanged = false;
        return MakeCheckCondition(0x06, 0x28, 0x00); // UNIT ATTENTION, MEDIUM MAY HAVE CHANGED
    }

    if (!IsReady())
        return MakeCheckCondition(0x02, 0x3A, 0x00); // NOT READY, MEDIUM NOT PRESENT

    uint8_t op = cdb[0];
    switch (op) {
        case 0x00: return CmdTestUnitReady();
        case 0x03: return CmdRequestSense(cdb);
        case 0x08: return CmdRead6(cdb);
        case 0x12: return CmdInquiry(cdb);
        case 0x1A: return CmdModeSense6(cdb);
        case 0x1B: return CmdStartStopUnit();
        case 0x1E: return CmdPreventAllowRemoval();
        case 0x25: return CmdReadCapacity();
        case 0x28: return CmdRead10(cdb);
        case 0x43: return CmdReadToc(cdb);
        case 0x5A: return CmdModeSense10(cdb);
        default:   return MakeCheckCondition(0x05, 0x20, 0x00);
    }
}

void ScsiCdrom::CompleteWrite(uint32_t /*lba*/, const uint8_t* /*data*/, int /*length*/)
{
    // CD-ROM is read-only
}

ScsiResult ScsiCdrom::CmdTestUnitReady()
{
    ClearSense();
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiCdrom::CmdRequestSense(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 18;
    int len = std::min(allocLen, 18);
    std::vector<uint8_t> data(m_senseData, m_senseData + len);
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), len, {}, 0, true, false };
}

ScsiResult ScsiCdrom::CmdInquiryNoDevice(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 36;
    std::vector<uint8_t> data(36, 0);
    data[0] = 0x7F;
    data[4] = 0x1F;
    int len = std::min(allocLen, 36);
    return ScsiResult{ 0x00, TrimData(data, len), len, {}, 0, true, false };
}

ScsiResult ScsiCdrom::CmdInquiry(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 36;
    std::vector<uint8_t> data(36, 0);
    data[0] = 0x05; // CD-ROM device
    data[1] = 0x80; // Removable media
    data[2] = 0x02; // SCSI-2
    data[3] = 0x02; // Response format 2
    data[4] = 0x1F;

    SetString(data.data(), 8, 8, "EMULATED");
    SetString(data.data(), 16, 16, "SCSI CD-ROM");
    SetString(data.data(), 32, 4, "1.0 ");

    int len = std::min(allocLen, 36);
    ClearSense();
    return ScsiResult{ 0x00, TrimData(data, len), len, {}, 0, true, false };
}

ScsiResult ScsiCdrom::CmdModeSense6(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 4;

    std::vector<uint8_t> data(4, 0);
    data[0] = 0x03;
    data[1] = 0x01; // 120mm CD-ROM data only
    data[2] = 0x80; // Write-protected
    data[3] = 0x00;

    int len = std::min(allocLen, 4);
    ClearSense();
    return ScsiResult{ 0x00, TrimData(data, len), len, {}, 0, true, false };
}

ScsiResult ScsiCdrom::CmdModeSense10(const uint8_t* cdb)
{
    int allocLen = cdb[7] << 8 | cdb[8];
    if (allocLen == 0) allocLen = 8;

    std::vector<uint8_t> data(8, 0);
    data[0] = 0x00;
    data[1] = 0x06;
    data[2] = 0x01;
    data[3] = 0x80;
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = 0x00;
    data[7] = 0x00;

    int len = std::min(allocLen, 8);
    ClearSense();
    return ScsiResult{ 0x00, TrimData(data, len), len, {}, 0, true, false };
}

ScsiResult ScsiCdrom::CmdStartStopUnit()
{
    ClearSense();
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiCdrom::CmdPreventAllowRemoval()
{
    ClearSense();
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiCdrom::CmdReadCapacity()
{
    std::vector<uint8_t> data(8, 0);
    uint32_t lastLba = m_totalSectors > 0 ? static_cast<uint32_t>(m_totalSectors - 1) : 0;
    data[0] = static_cast<uint8_t>(lastLba >> 24);
    data[1] = static_cast<uint8_t>(lastLba >> 16);
    data[2] = static_cast<uint8_t>(lastLba >> 8);
    data[3] = static_cast<uint8_t>(lastLba);
    // Block size = 2048
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = 0x08;
    data[7] = 0x00;
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), 8, {}, 0, true, false };
}

ScsiResult ScsiCdrom::CmdRead6(const uint8_t* cdb)
{
    uint32_t lba = static_cast<uint32_t>((cdb[1] & 0x1F) << 16 | cdb[2] << 8 | cdb[3]);
    int count = cdb[4] == 0 ? 256 : cdb[4];
    return DoRead(lba, count);
}

ScsiResult ScsiCdrom::CmdRead10(const uint8_t* cdb)
{
    uint32_t lba = static_cast<uint32_t>(cdb[2] << 24 | cdb[3] << 16 | cdb[4] << 8 | cdb[5]);
    int count = cdb[7] << 8 | cdb[8];
    if (count == 0) {
        ClearSense();
        return ScsiResult{ 0x00 };
    }
    return DoRead(lba, count);
}

ScsiResult ScsiCdrom::CmdReadToc(const uint8_t* cdb)
{
    int allocLen = cdb[7] << 8 | cdb[8];
    if (allocLen == 0) allocLen = 12;
    bool msf = (cdb[1] & 0x02) != 0;

    // TOC header (4 bytes) + track 1 descriptor (8 bytes) + lead-out (8 bytes) = 20 bytes
    std::vector<uint8_t> toc(20, 0);

    int tocDataLen = 18;
    toc[0] = static_cast<uint8_t>(tocDataLen >> 8);
    toc[1] = static_cast<uint8_t>(tocDataLen & 0xFF);
    toc[2] = 0x01; // First track
    toc[3] = 0x01; // Last track

    // Track 1 descriptor
    toc[4] = 0x00;
    toc[5] = 0x14; // ADR=1, CONTROL=4 (data track)
    toc[6] = 0x01;
    toc[7] = 0x00;

    if (msf) {
        toc[8] = 0x00;
        toc[9] = 0x00;
        toc[10] = 0x02;
        toc[11] = 0x00;
    } else {
        toc[8] = 0x00;
        toc[9] = 0x00;
        toc[10] = 0x00;
        toc[11] = 0x00;
    }

    // Lead-out (track 0xAA)
    toc[12] = 0x00;
    toc[13] = 0x14;
    toc[14] = 0xAA;
    toc[15] = 0x00;

    uint32_t leadOutLba = static_cast<uint32_t>(m_totalSectors);
    if (msf) {
        uint8_t m, s, f;
        LbaToMsf(leadOutLba, m, s, f);
        toc[16] = 0x00;
        toc[17] = m;
        toc[18] = s;
        toc[19] = f;
    } else {
        toc[16] = static_cast<uint8_t>(leadOutLba >> 24);
        toc[17] = static_cast<uint8_t>(leadOutLba >> 16);
        toc[18] = static_cast<uint8_t>(leadOutLba >> 8);
        toc[19] = static_cast<uint8_t>(leadOutLba);
    }

    int len = std::min(allocLen, 20);
    ClearSense();
    return ScsiResult{ 0x00, TrimData(toc, len), len, {}, 0, true, false };
}

ScsiResult ScsiCdrom::DoRead(uint32_t lba, int sectorCount)
{
    if (static_cast<int64_t>(lba) + sectorCount > m_totalSectors)
        return MakeCheckCondition(0x05, 0x21, 0x00);

    int byteCount = sectorCount * SectorSize;
    std::vector<uint8_t> data(byteCount, 0);
    m_imageStream.seekg(static_cast<int64_t>(lba) * SectorSize, std::ios::beg);
    int totalRead = 0;
    while (totalRead < byteCount) {
        m_imageStream.read(reinterpret_cast<char*>(data.data() + totalRead), byteCount - totalRead);
        auto n = m_imageStream.gcount();
        if (n == 0) break;
        totalRead += static_cast<int>(n);
    }
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), byteCount, {}, 0, true, false };
}

ScsiResult ScsiCdrom::MakeCheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq)
{
    std::memset(m_senseData, 0, 18);
    m_senseData[0] = 0x70;
    m_senseData[2] = senseKey;
    m_senseData[7] = 0x0A;
    m_senseData[12] = asc;
    m_senseData[13] = ascq;
    return ScsiResult{ 0x02 };
}

void ScsiCdrom::ClearSense()
{
    std::memset(m_senseData, 0, 18);
    m_senseData[0] = 0x70;
    m_senseData[7] = 0x0A;
}

void ScsiCdrom::SetString(uint8_t* buf, int offset, int maxLen, const char* s)
{
    int sLen = static_cast<int>(std::strlen(s));
    for (int i = 0; i < maxLen; i++)
        buf[offset + i] = i < sLen ? static_cast<uint8_t>(s[i]) : static_cast<uint8_t>(' ');
}

std::vector<uint8_t> ScsiCdrom::TrimData(const std::vector<uint8_t>& data, int len)
{
    if (len >= static_cast<int>(data.size())) return data;
    return std::vector<uint8_t>(data.begin(), data.begin() + len);
}

void ScsiCdrom::LbaToMsf(uint32_t lba, uint8_t& m, uint8_t& s, uint8_t& f)
{
    uint32_t adjusted = lba + 150; // 150 frames = 2 seconds
    f = static_cast<uint8_t>(adjusted % 75);
    uint32_t seconds = adjusted / 75;
    s = static_cast<uint8_t>(seconds % 60);
    m = static_cast<uint8_t>(seconds / 60);
}

} // namespace Em68030::IO
