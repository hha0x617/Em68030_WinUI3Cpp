#include "pch.h"

#include "ScsiDisk.h"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace Em68030::IO {

ScsiDisk::ScsiDisk()
{
    ClearSense();
}

ScsiDisk::~ScsiDisk()
{
    UnmountImage();
}

bool ScsiDisk::IsReady() const
{
    return m_imageStream.is_open();
}

void ScsiDisk::MountImage(const std::string& path)
{
    UnmountImage();
    m_imageStream.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!m_imageStream.is_open()) {
        // Try creating the file first
        std::ofstream create(path, std::ios::binary);
        create.close();
        m_imageStream.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (m_imageStream.is_open()) {
        m_imageStream.seekg(0, std::ios::end);
        int64_t fileSize = m_imageStream.tellg();
        m_totalSectors = fileSize / SectorSize;
        if (m_totalSectors == 0 && fileSize > 0)
            m_totalSectors = 1;
    }
    ClearSense();
}

void ScsiDisk::UnmountImage()
{
    if (m_imageStream.is_open())
        m_imageStream.close();
    m_totalSectors = 0;
}

ScsiResult ScsiDisk::ProcessCommand(const uint8_t* cdb, int cdbLength, int lun)
{
    if (!IsReady())
        return MakeCheckCondition(0x02, 0x3A, 0x00); // NOT READY, MEDIUM NOT PRESENT

    if (lun != 0) {
        uint8_t opcode = cdb[0];
        if (opcode == 0x12) return CmdInquiryNoDevice(cdb);
        if (opcode == 0x03) return CmdRequestSense(cdb);
        return MakeCheckCondition(0x05, 0x25, 0x00); // ILLEGAL REQUEST, LUN NOT SUPPORTED
    }

    uint8_t op = cdb[0];
    ScsiResult result;
    switch (op) {
        case 0x00: result = CmdTestUnitReady(); break;
        case 0x03: result = CmdRequestSense(cdb); break;
        case 0x08: result = CmdRead6(cdb); break;
        case 0x0A: result = CmdWrite6(cdb); break;
        case 0x12: result = CmdInquiry(cdb); break;
        case 0x15: result = CmdModeSelect6(cdb); break;
        case 0x1A: result = CmdModeSense6(cdb); break;
        case 0x1B: result = CmdStartStopUnit(); break;
        case 0x1E: result = CmdPreventAllowRemoval(); break;
        case 0x25: result = CmdReadCapacity(); break;
        case 0x28: result = CmdRead10(cdb); break;
        case 0x2A: result = CmdWrite10(cdb); break;
        case 0x35: result = CmdSynchronizeCache(); break;
        case 0x55: result = CmdModeSelect10(cdb); break;
        case 0x5A: result = CmdModeSense10(cdb); break;
        default:   result = MakeCheckCondition(0x05, 0x20, 0x00); break;
    }

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SD] op=$" << std::hex << static_cast<int>(op)
           << " lun=" << std::dec << lun
           << " status=$" << std::hex << static_cast<int>(result.StatusByte)
           << " din=" << std::dec << result.DataInLength
           << " dout=" << result.DataOutLength
           << " CDB=[";
        for (int i = 0; i < cdbLength; i++) {
            if (i > 0) ss << " ";
            ss << std::hex << static_cast<int>(cdb[i]);
        }
        ss << "]";
        DiagLog(ss.str());
    }
    return result;
}

ScsiResult ScsiDisk::CmdTestUnitReady()
{
    ClearSense();
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiDisk::CmdRequestSense(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 18;
    // Return allocLen bytes (zero-padded beyond 18) to match the transfer count
    // the driver sets up. Returning fewer bytes leaves a non-zero residual TC,
    // which causes the NetBSD wdsc driver to report EINVAL (error 22).
    std::vector<uint8_t> data(allocLen, 0);
    int copyLen = std::min(allocLen, 18);
    std::memcpy(data.data(), m_senseData, copyLen);
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), allocLen, {}, 0, true, false };
}

ScsiResult ScsiDisk::CmdInquiryNoDevice(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 36;
    std::vector<uint8_t> data(36, 0);
    data[0] = 0x7F; // No device at this LUN
    data[4] = 0x1F;
    int len = std::min(allocLen, 36);
    data.resize(len);
    return ScsiResult{ 0x00, std::move(data), len, {}, 0, true, false };
}

ScsiResult ScsiDisk::CmdInquiry(const uint8_t* cdb)
{
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 36;
    std::vector<uint8_t> data(36, 0);
    data[0] = 0x00; // Direct access device
    data[1] = 0x00; // Not removable
    data[2] = 0x02; // SCSI-2
    data[3] = 0x02; // Response format 2
    data[4] = 0x1F; // Additional length = 31

    SetString(data.data(), 8, 8, "EMULATED");
    SetString(data.data(), 16, 16, "SCSI DISK");
    SetString(data.data(), 32, 4, "1.0 ");

    int len = std::min(allocLen, 36);
    data.resize(len);
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), len, {}, 0, true, false };
}

ScsiResult ScsiDisk::CmdModeSelect6(const uint8_t* cdb)
{
    int paramLen = cdb[4];
    ClearSense();
    if (paramLen > 0) {
        return ScsiResult{ 0x00, {}, 0, std::vector<uint8_t>(paramLen, 0), paramLen, false, true };
    }
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiDisk::CmdModeSense6(const uint8_t* cdb)
{
    bool dbd = (cdb[1] & 0x08) != 0;
    int pageCode = cdb[2] & 0x3F;
    int allocLen = cdb[4];
    if (allocLen == 0) allocLen = 4;

    auto pages = BuildModePages(pageCode);

    int bdLen = dbd ? 0 : 8;
    int totalLen = 4 + bdLen + static_cast<int>(pages.size());
    std::vector<uint8_t> data(totalLen, 0);
    data[0] = static_cast<uint8_t>(totalLen - 1);
    data[1] = 0x00;
    data[2] = 0x00;
    data[3] = static_cast<uint8_t>(bdLen);

    if (!dbd) {
        uint32_t numBlocks = static_cast<uint32_t>(m_totalSectors);
        data[4] = 0x00;
        data[5] = static_cast<uint8_t>(numBlocks >> 16);
        data[6] = static_cast<uint8_t>(numBlocks >> 8);
        data[7] = static_cast<uint8_t>(numBlocks);
        data[8] = 0x00;
        data[9] = 0x00;
        data[10] = 0x02;
        data[11] = 0x00;
    }

    std::copy(pages.begin(), pages.end(), data.begin() + 4 + bdLen);

    int len = std::min(allocLen, totalLen);
    data.resize(len);
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), len, {}, 0, true, false };
}

ScsiResult ScsiDisk::CmdModeSense10(const uint8_t* cdb)
{
    bool dbd = (cdb[1] & 0x08) != 0;
    int pageCode = cdb[2] & 0x3F;
    int allocLen = (cdb[7] << 8) | cdb[8];
    if (allocLen == 0) allocLen = 8;

    auto pages = BuildModePages(pageCode);

    int bdLen = dbd ? 0 : 8;
    int totalLen = 8 + bdLen + static_cast<int>(pages.size());
    std::vector<uint8_t> data(totalLen, 0);
    int mdl = totalLen - 2;
    data[0] = static_cast<uint8_t>(mdl >> 8);
    data[1] = static_cast<uint8_t>(mdl);
    data[2] = 0x00;
    data[3] = 0x00;
    data[6] = static_cast<uint8_t>(bdLen >> 8);
    data[7] = static_cast<uint8_t>(bdLen);

    if (!dbd) {
        uint32_t numBlocks = static_cast<uint32_t>(m_totalSectors);
        data[8] = 0x00;
        data[9] = static_cast<uint8_t>(numBlocks >> 16);
        data[10] = static_cast<uint8_t>(numBlocks >> 8);
        data[11] = static_cast<uint8_t>(numBlocks);
        data[12] = 0x00;
        data[13] = 0x00;
        data[14] = 0x02;
        data[15] = 0x00;
    }

    std::copy(pages.begin(), pages.end(), data.begin() + 8 + bdLen);

    int len = std::min(allocLen, totalLen);
    data.resize(len);
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), len, {}, 0, true, false };
}

ScsiResult ScsiDisk::CmdModeSelect10(const uint8_t* cdb)
{
    int paramLen = (cdb[7] << 8) | cdb[8];
    ClearSense();
    if (paramLen > 0) {
        return ScsiResult{ 0x00, {}, 0, std::vector<uint8_t>(paramLen, 0), paramLen, false, true };
    }
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiDisk::CmdStartStopUnit()
{
    ClearSense();
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiDisk::CmdPreventAllowRemoval()
{
    ClearSense();
    return ScsiResult{ 0x00 };
}

ScsiResult ScsiDisk::CmdSynchronizeCache()
{
    if (m_imageStream.is_open())
        m_imageStream.flush();
    ClearSense();
    return ScsiResult{ 0x00 };
}

std::vector<uint8_t> ScsiDisk::BuildModePages(int pageCode)
{
    int nsectors = 32;
    int ntracks = 64;
    int ncylinders = static_cast<int>(m_totalSectors / (nsectors * ntracks));

    std::vector<uint8_t> result;

    if (pageCode == 0x03 || pageCode == 0x3F) {
        // Page 3: Format Device Parameters (24 bytes)
        std::vector<uint8_t> p3(24, 0);
        p3[0] = 0x03;
        p3[1] = 22;
        p3[10] = 0;
        p3[11] = static_cast<uint8_t>(nsectors);
        p3[12] = 0x02;
        p3[13] = 0x00;
        result.insert(result.end(), p3.begin(), p3.end());
    }

    if (pageCode == 0x04 || pageCode == 0x3F) {
        // Page 4: Rigid Disk Drive Geometry (24 bytes)
        std::vector<uint8_t> p4(24, 0);
        p4[0] = 0x04;
        p4[1] = 22;
        p4[2] = static_cast<uint8_t>(ncylinders >> 16);
        p4[3] = static_cast<uint8_t>(ncylinders >> 8);
        p4[4] = static_cast<uint8_t>(ncylinders);
        p4[5] = static_cast<uint8_t>(ntracks);
        p4[20] = 0x0E;
        p4[21] = 0x10;
        result.insert(result.end(), p4.begin(), p4.end());
    }

    return result;
}

ScsiResult ScsiDisk::CmdReadCapacity()
{
    std::vector<uint8_t> data(8, 0);
    uint32_t lastLba = m_totalSectors > 0 ? static_cast<uint32_t>(m_totalSectors - 1) : 0;
    data[0] = static_cast<uint8_t>(lastLba >> 24);
    data[1] = static_cast<uint8_t>(lastLba >> 16);
    data[2] = static_cast<uint8_t>(lastLba >> 8);
    data[3] = static_cast<uint8_t>(lastLba);
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = 0x02;
    data[7] = 0x00;
    ClearSense();
    return ScsiResult{ 0x00, std::move(data), 8, {}, 0, true, false };
}

ScsiResult ScsiDisk::CmdRead6(const uint8_t* cdb)
{
    uint32_t lba = static_cast<uint32_t>((cdb[1] & 0x1F) << 16 | cdb[2] << 8 | cdb[3]);
    int count = cdb[4];
    if (count == 0) count = 256;
    return DoRead(lba, count);
}

ScsiResult ScsiDisk::CmdRead10(const uint8_t* cdb)
{
    uint32_t lba = static_cast<uint32_t>(cdb[2] << 24 | cdb[3] << 16 | cdb[4] << 8 | cdb[5]);
    int count = cdb[7] << 8 | cdb[8];
    if (count == 0) {
        ClearSense();
        return ScsiResult{ 0x00 };
    }
    return DoRead(lba, count);
}

ScsiResult ScsiDisk::CmdWrite6(const uint8_t* cdb)
{
    uint32_t lba = static_cast<uint32_t>((cdb[1] & 0x1F) << 16 | cdb[2] << 8 | cdb[3]);
    int count = cdb[4];
    if (count == 0) count = 256;
    return DoWrite(lba, count);
}

ScsiResult ScsiDisk::CmdWrite10(const uint8_t* cdb)
{
    uint32_t lba = static_cast<uint32_t>(cdb[2] << 24 | cdb[3] << 16 | cdb[4] << 8 | cdb[5]);
    int count = cdb[7] << 8 | cdb[8];
    if (count == 0) {
        ClearSense();
        return ScsiResult{ 0x00 };
    }
    return DoWrite(lba, count);
}

ScsiResult ScsiDisk::DoRead(uint32_t lba, int sectorCount)
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

ScsiResult ScsiDisk::DoWrite(uint32_t lba, int sectorCount)
{
    if (static_cast<int64_t>(lba) + sectorCount > m_totalSectors)
        return MakeCheckCondition(0x05, 0x21, 0x00);

    int byteCount = sectorCount * SectorSize;
    std::vector<uint8_t> buffer(byteCount, 0);
    ClearSense();
    return ScsiResult{ 0x00, {}, 0, std::move(buffer), byteCount, false, true };
}

void ScsiDisk::CompleteWrite(uint32_t lba, const uint8_t* data, int length)
{
    if (!m_imageStream.is_open()) return;
    m_imageStream.seekp(static_cast<int64_t>(lba) * SectorSize, std::ios::beg);
    m_imageStream.write(reinterpret_cast<const char*>(data), length);
    m_imageStream.flush();
}

ScsiResult ScsiDisk::MakeCheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq)
{
    std::memset(m_senseData, 0, 18);
    m_senseData[0] = 0x70;
    m_senseData[2] = senseKey;
    m_senseData[7] = 0x0A;
    m_senseData[12] = asc;
    m_senseData[13] = ascq;
    return ScsiResult{ 0x02 };
}

void ScsiDisk::ClearSense()
{
    std::memset(m_senseData, 0, 18);
    m_senseData[0] = 0x70;
    m_senseData[7] = 0x0A;
}

void ScsiDisk::SetString(uint8_t* buf, int offset, int maxLen, const char* s)
{
    int sLen = static_cast<int>(std::strlen(s));
    for (int i = 0; i < maxLen; i++)
        buf[offset + i] = i < sLen ? static_cast<uint8_t>(s[i]) : static_cast<uint8_t>(' ');
}

void ScsiDisk::PutBE32(uint8_t* buf, int offset, uint32_t value)
{
    buf[offset + 0] = static_cast<uint8_t>(value >> 24);
    buf[offset + 1] = static_cast<uint8_t>(value >> 16);
    buf[offset + 2] = static_cast<uint8_t>(value >> 8);
    buf[offset + 3] = static_cast<uint8_t>(value);
}

void ScsiDisk::PutBE16(uint8_t* buf, int offset, uint16_t value)
{
    buf[offset + 0] = static_cast<uint8_t>(value >> 8);
    buf[offset + 1] = static_cast<uint8_t>(value);
}

void ScsiDisk::SetLabelString(uint8_t* buf, int offset, int maxLen, const char* s)
{
    int sLen = static_cast<int>(std::strlen(s));
    for (int i = 0; i < maxLen; i++)
        buf[offset + i] = i < sLen ? static_cast<uint8_t>(s[i]) : static_cast<uint8_t>(0);
}

void ScsiDisk::WriteNetBsdDisklabel(const std::string& path)
{
    std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs.is_open()) return;

    fs.seekg(0, std::ios::end);
    int64_t fileSize = fs.tellg();
    int64_t totalSectors = fileSize / SectorSize;
    if (totalSectors < 1) return;

    int nsectors = 32;
    int ntracks = 64;
    int secpercyl = nsectors * ntracks;
    int ncylinders = static_cast<int>(totalSectors / secpercyl);
    int secperunit = static_cast<int>(totalSectors);

    uint8_t sector[SectorSize];
    std::memset(sector, 0, SectorSize);

    constexpr uint32_t DISKMAGIC = 0x82564557;
    int npartitions = 8;

    // ===== VID block (block 0, offsets 0x00 - 0xFF) =====
    SetLabelString(sector, 0x00, 4, "NBSD");
    PutBE32(sector, 0x14, 2);
    PutBE16(sector, 0x18, 30);
    PutBE16(sector, 0x1E, 0x003F);
    PutBE16(sector, 0x20, 0x0000);
    PutBE16(sector, 0x24, static_cast<uint16_t>(npartitions));
    SetLabelString(sector, 0x26, 16, "EMULATED");
    PutBE32(sector, 0x36, 8192);
    PutBE32(sector, 0x3A, DISKMAGIC);
    PutBE16(sector, 0x3E, 4);
    SetLabelString(sector, 0x42, 16, "NetBSD");
    PutBE32(sector, 0x80, static_cast<uint32_t>(secpercyl));
    PutBE32(sector, 0x84, static_cast<uint32_t>(secperunit));
    PutBE32(sector, 0x90, 1);
    sector[0x94] = 1;

    // Partitions 0-3 in vid_4[64] at offset 0x98 (each entry 16 bytes)
    // Partition b: 64 MB swap (also used for miniroot during installation)
    int swapSectors = std::min(131072, secperunit / 4); // 64 MB or 25% of disk
    swapSectors = std::max(swapSectors, 16384);          // minimum 8 MB
    int aSectors = secperunit - swapSectors;
    int bOffset = aSectors;

    int pa = 0x98;
    // a: root filesystem
    PutBE32(sector, pa + 0, static_cast<uint32_t>(aSectors));
    PutBE32(sector, pa + 4, 0);
    PutBE32(sector, pa + 8, 1024);
    sector[pa + 12] = 7;   // FS_BSDFFS
    sector[pa + 13] = 8;   // p_frag
    PutBE16(sector, pa + 14, 16);

    // b: swap (miniroot written here during installation Phase 1)
    PutBE32(sector, pa + 16 + 0, static_cast<uint32_t>(swapSectors));
    PutBE32(sector, pa + 16 + 4, static_cast<uint32_t>(bOffset));
    sector[pa + 16 + 12] = 1; // b: FS_SWAP

    // c: whole disk
    PutBE32(sector, pa + 32 + 0, static_cast<uint32_t>(secperunit));
    PutBE32(sector, pa + 32 + 4, 0);

    PutBE32(sector, 0xF4, 8192);
    SetLabelString(sector, 0xF8, 8, "MOTOROLA");

    // ===== CFG area (block 1, offsets 0x100 - 0x1FF) =====
    PutBE16(sector, 0x10A, 256);
    PutBE16(sector, 0x114, 3600);
    sector[0x118] = static_cast<uint8_t>(nsectors);
    sector[0x119] = static_cast<uint8_t>(ntracks);
    PutBE16(sector, 0x11A, static_cast<uint16_t>(ncylinders));
    sector[0x11C] = 1;
    PutBE16(sector, 0x11E, 512);
    PutBE32(sector, 0x13C, DISKMAGIC);

    // Write sector 0
    fs.seekp(0, std::ios::beg);
    fs.write(reinterpret_cast<const char*>(sector), SectorSize);
    fs.flush();
}

} // namespace Em68030::IO
