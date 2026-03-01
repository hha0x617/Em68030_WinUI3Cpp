#include "pch.h"

#include "Mmu.h"
#include "Memory.h"
#include "BusErrorException.h"

#include <cstring>
#include <format>
#include <sstream>

namespace Em68030::Core {

// ============================================================================
// Constructor
// ============================================================================

Mmu::Mmu(Memory& physicalMemory)
    : m_physicalMemory(physicalMemory)
{
}

// ============================================================================
// Reset
// ============================================================================

void Mmu::Reset()
{
    CRP = 0;
    SRP = 0;
    SetTC(0);  // setter updates all cached fields
    SetTT0(0);
    SetTT1(0);
    MMUSR = 0;
    m_lastDescriptorAddress = 0;
    std::memset(m_atc, 0, sizeof(m_atc));
    std::memset(m_atcDescAddr, 0, sizeof(m_atcDescAddr));
}

// ============================================================================
// TC property setter - caches derived fields
// ============================================================================

void Mmu::SetTC(uint32_t value)
{
    m_tc = value;
    m_enabled = (value & 0x80000000) != 0;
    m_sre = (value & 0x02000000) != 0;
    int bits = static_cast<int>((value >> 20) & 0xF);
    m_pageSizeCached = 1 << bits;
    CachedPageMask = static_cast<uint32_t>(m_pageSizeCached - 1);
    m_initialShiftCached = static_cast<int>((value >> 16) & 0xF);
    m_tiaCached = static_cast<int>((value >> 12) & 0xF);
    m_tibCached = static_cast<int>((value >> 8) & 0xF);
    m_ticCached = static_cast<int>((value >> 4) & 0xF);
    m_tidCached = static_cast<int>(value & 0xF);
}

// ============================================================================
// TT0/TT1 property setters - update _ttEnabled flag
// ============================================================================

void Mmu::SetTT0(uint32_t value)
{
    m_tt0 = value;
    m_ttEnabled = (m_tt0 & 0x8000) != 0 || (m_tt1 & 0x8000) != 0;
}

void Mmu::SetTT1(uint32_t value)
{
    m_tt1 = value;
    m_ttEnabled = (m_tt0 & 0x8000) != 0 || (m_tt1 & 0x8000) != 0;
}

// ============================================================================
// ATC key management
// ============================================================================

uint64_t Mmu::MakeAtcKey(uint32_t logicalAddress, uint8_t functionCode) const
{
    uint32_t pageAddr = logicalAddress & ~CachedPageMask;
    return (static_cast<uint64_t>(functionCode & 7) << 32) | pageAddr;
}

int Mmu::MakeAtcIndex(uint64_t atcKey)
{
    uint32_t addr = static_cast<uint32_t>(atcKey & 0xFFFFFFFF);
    uint32_t fc = static_cast<uint32_t>(atcKey >> 32);
    // Improved hash for better distribution across larger ATC
    uint32_t hash = (addr >> 12) ^ (addr >> 18) ^ (addr >> 24) ^ (fc * 0x9E3779B9u);
    return static_cast<int>(hash & AtcMask);
}

// ============================================================================
// SSW construction
// ============================================================================

bool Mmu::IsDataAccess(uint8_t functionCode)
{
    // FC=1 (user data), FC=5 (supervisor data) -> true
    // FC=2 (user program), FC=6 (supervisor program) -> false
    return (functionCode & 2) == 0;
}

uint16_t Mmu::BuildSSW(uint8_t functionCode, bool isRead)
{
    // MC68030 SSW format:
    //   Bit 15: FC  (Fault on stage C of instruction pipe)
    //   Bit 14: FB  (Fault on stage B of instruction pipe)
    //   Bit 13: RC  (Rerun stage C)
    //   Bit 12: RB  (Rerun stage B)
    //   Bit  8: DF  (Data Fault -- 1=data access, 0=instruction fetch)
    //   Bit  7: RM  (Read-Modify-Write)
    //   Bit  6: RW  (1=read, 0=write)
    //   Bits 2-0: FC (Function Code)
    //
    // IMPORTANT: Always set DF=1 so the kernel reads the fault address from
    // the frame's Data Cycle Fault Address field (which our emulator always
    // fills with the correct faulting address).
    uint16_t ssw = 0;
    ssw |= static_cast<uint16_t>(functionCode & 7);   // FC2-FC0 in bits 2-0
    ssw |= 0x0100;                                      // DF bit 8 -- always set
    if (isRead) ssw |= 0x0040;                          // RW bit 6 (1=read)
    return ssw;
}

// ============================================================================
// Main translation entry point
// ============================================================================

uint32_t Mmu::Translate(uint32_t logicalAddress, bool supervisorMode, bool write, uint8_t functionCode)
{
    if (!m_enabled)
        return logicalAddress;

    // Check Transparent Translation registers (skip if both disabled)
    if (m_ttEnabled && (IsTransparent(logicalAddress, functionCode, m_tt0, write) ||
                        IsTransparent(logicalAddress, functionCode, m_tt1, write)))
        return logicalAddress;

    // Check ATC (Tag==0 means invalid entry)
    uint64_t atcKey = MakeAtcKey(logicalAddress, functionCode);
    int atcIdx = MakeAtcIndex(atcKey);
    AtcEntry& entry = m_atc[atcIdx];
    if (entry.Tag == atcKey)
    {
        // Write-protect check
        if (write && (entry.Flags & ATC_FLAG_WRITE_PROTECTED))
        {
            uint16_t ssw = BuildSSW(functionCode, /*isRead=*/false);
            throw BusErrorException(logicalAddress, true, functionCode, ssw);
        }

        // If write and not yet modified, invalidate ATC and re-walk
        // to set the Modified bit in the page table descriptor
        if (write && !(entry.Flags & ATC_FLAG_MODIFIED))
        {
            entry.Tag = 0; // Invalidate
            uint16_t savedMMUSR = MMUSR;
            uint32_t result = TableWalk(logicalAddress, supervisorMode, write, functionCode);
            MMUSR = savedMMUSR;
            return result;
        }

        return entry.PhysicalPage | (logicalAddress & CachedPageMask);
    }

    // Table walk — preserve MMUSR (only PTEST should modify it, MC68030 PRM §9.5.3)
    {
        uint16_t savedMMUSR = MMUSR;
        try
        {
            uint32_t result = TableWalk(logicalAddress, supervisorMode, write, functionCode);
            MMUSR = savedMMUSR;
            return result;
        }
        catch (...)
        {
            MMUSR = savedMMUSR;
            throw;
        }
    }
}

// Backward-compatible overload (for old call sites during transition)
uint32_t Mmu::Translate(uint32_t logicalAddress, bool supervisorMode, bool write)
{
    uint8_t fc = supervisorMode ? static_cast<uint8_t>(5) : static_cast<uint8_t>(1); // Default: data access
    return Translate(logicalAddress, supervisorMode, write, fc);
}

// ============================================================================
// Transparent Translation
// ============================================================================

bool Mmu::IsTransparent(uint32_t address, uint8_t functionCode, uint32_t tt, bool isWrite) const
{
    if ((tt & 0x8000) == 0) return false; // E bit (bit 15)

    // Address match: compare high byte with base, ignoring masked bits
    uint8_t addrBase = static_cast<uint8_t>(tt >> 24);
    uint8_t addrMask = static_cast<uint8_t>(tt >> 16);
    uint8_t addrHigh = static_cast<uint8_t>(address >> 24);
    if ((addrHigh & ~addrMask) != (addrBase & ~addrMask))
        return false;

    // Function code match: compare FC with FC BASE, ignoring FC MASK bits
    int fcBase = static_cast<int>((tt >> 4) & 7);   // bits 6-4
    int fcMask = static_cast<int>(tt & 7);           // bits 2-0
    if ((functionCode & ~fcMask & 7) != (fcBase & ~fcMask & 7))
        return false;

    // R/W check: RWM (bit 8) masks the R/W field (bit 9)
    bool rwm = (tt & 0x0100) != 0;  // bit 8
    if (!rwm)
    {
        // RWM=0: R/W field is used. R/W=1 means read transparent, R/W=0 means write transparent.
        // For read-modify-write cycles, neither read nor write is transparent when RWM=0.
        bool rwBit = (tt & 0x0200) != 0; // bit 9: 1=read, 0=write
        if (isWrite && rwBit) return false;   // write access, but only reads are transparent
        if (!isWrite && !rwBit) return false;  // read access, but only writes are transparent
    }

    return true;
}

// ============================================================================
// Page Table Walk
// ============================================================================

uint32_t Mmu::TableWalk(uint32_t logicalAddress, bool supervisorMode, bool write, uint8_t functionCode)
{
    // MC68030: Root pointer selection is based on FC2 bit of the function code,
    // NOT the CPU's current supervisor mode. This is critical for MOVES instruction
    // which accesses user space (FC=1) while the CPU is in supervisor mode.
    // FC2=1 (FC 4-7) = supervisor -> use SRP if SRE; FC2=0 (FC 0-3) = user -> use CRP
    bool isSupervisorAccess = (functionCode & 4) != 0;
    uint64_t rootPointer = (isSupervisorAccess && m_sre) ? SRP : CRP;

    // Root pointer format (64-bit):
    // Upper long: bits 1-0 = DT (determines first-level entry size)
    // Lower long: bits 31-4 = table base address
    uint32_t rpUpper = static_cast<uint32_t>(rootPointer >> 32);
    uint32_t rpLower = static_cast<uint32_t>(rootPointer & 0xFFFFFFFF);

    int rpDT = static_cast<int>(rpUpper & 3);
    uint32_t tableAddr = rpLower & 0xFFFFFFF0;

    // If root pointer DT is invalid (0), bus error
    if (rpDT == 0)
    {
        MMUSR = 0x0400; // I (Invalid) - bit 10
        uint16_t ssw = BuildSSW(functionCode, /*isRead=*/!write);
        throw BusErrorException(logicalAddress, write, functionCode, ssw);
    }

    int shift = 32 - m_initialShiftCached;
    m_tableIndexBits[0] = m_tiaCached;
    m_tableIndexBits[1] = m_tibCached;
    m_tableIndexBits[2] = m_ticCached;
    m_tableIndexBits[3] = m_tidCached;
    bool writeProtected = false;
    bool cacheInhibit = false;
    int levelsSearched = 0;

    // Entry size for the first table level is determined by root pointer DT
    int entrySize = (rpDT == 3) ? 8 : 4;

    for (int level = 0; level < 4; level++)
    {
        int indexBits = m_tableIndexBits[level];
        if (indexBits == 0) continue;

        shift -= indexBits;
        int index = static_cast<int>((logicalAddress >> shift) & ((1 << indexBits) - 1));

        uint32_t descAddr = tableAddr + static_cast<uint32_t>(index * entrySize);
        m_lastDescriptorAddress = descAddr;

        // Read descriptor
        uint32_t descriptorLo;
        uint32_t descriptorHi = 0;
        if (entrySize == 8)
        {
            descriptorHi = m_physicalMemory.ReadLong(descAddr);
            descriptorLo = m_physicalMemory.ReadLong(descAddr + 4);
        }
        else
        {
            descriptorLo = m_physicalMemory.ReadLong(descAddr);
        }

        int dt = static_cast<int>(descriptorLo & 3);
        levelsSearched++;

        switch (dt)
        {
            case 0: // Invalid descriptor
            {
                MMUSR = static_cast<uint16_t>(
                    (levelsSearched & 7) |  // N (Number of levels, bits 2-0)
                    0x0400                   // I (Invalid, bit 10)
                );
                uint16_t ssw = BuildSSW(functionCode, /*isRead=*/!write);
                throw BusErrorException(logicalAddress, write, functionCode, ssw);
            }

            case 1: // Page descriptor (early termination)
            {
                // Extract physical page address
                // Page size determines which bits are the page offset
                // For early termination, remaining index bits also become part of offset
                // The effective page size is determined by the remaining shift value
                uint32_t pageMask = 0xFFFFFFFF << shift;
                uint32_t physPage = (descriptorLo & pageMask) & 0xFFFFFF00;

                bool wp = (descriptorLo & 0x04) != 0; // Write Protect
                bool u  = (descriptorLo & 0x08) != 0; // Used
                bool m  = (descriptorLo & 0x10) != 0; // Modified
                bool ci = (descriptorLo & 0x40) != 0; // Cache Inhibit

                writeProtected |= wp;
                cacheInhibit |= ci;

                // Set Used bit if not already set
                if (!u)
                {
                    if (entrySize == 8)
                        m_physicalMemory.WriteLong(descAddr + 4, descriptorLo | 0x08);
                    else
                        m_physicalMemory.WriteLong(descAddr, descriptorLo | 0x08);
                }

                // Check write protect
                if (write && writeProtected)
                {
                    MMUSR = static_cast<uint16_t>(
                        (levelsSearched & 7) |           // N (Number of levels, bits 2-0)
                        0x0800 |                         // W (Write protected, bit 11)
                        (m ? 0x0200 : 0)                 // M (Modified, bit 9)
                    );
                    uint16_t ssw = BuildSSW(functionCode, /*isRead=*/false);
                    throw BusErrorException(logicalAddress, true, functionCode, ssw);
                }

                // Set Modified bit on write
                if (write && !m)
                {
                    uint32_t currentDesc;
                    uint32_t mDescAddr;
                    if (entrySize == 8)
                    {
                        mDescAddr = descAddr + 4;
                        currentDesc = m_physicalMemory.ReadLong(mDescAddr);
                    }
                    else
                    {
                        mDescAddr = descAddr;
                        currentDesc = m_physicalMemory.ReadLong(mDescAddr);
                    }
                    m_physicalMemory.WriteLong(mDescAddr, currentDesc | 0x10);
                    m = true;
                }

                uint32_t pageOffset = static_cast<uint32_t>(logicalAddress & ~pageMask);
                uint32_t physAddr = physPage | pageOffset;

                // Update MMUSR
                MMUSR = static_cast<uint16_t>(
                    (levelsSearched & 7) |               // N (Number of levels, bits 2-0)
                    (writeProtected ? 0x0800 : 0) |      // W (Write protected, bit 11)
                    (m ? 0x0200 : 0)                     // M (Modified, bit 9)
                );

                // Cache in ATC
                CacheInAtc(logicalAddress, functionCode, physPage,
                           writeProtected, cacheInhibit, m,
                           entrySize == 8 ? descAddr + 4 : descAddr);

                return physAddr;
            }

            case 2: // Valid 4-byte table descriptor (short format)
            {
                // Accumulate write-protect from table descriptors
                writeProtected |= (descriptorLo & 0x04) != 0;

                // Set Used bit if not set
                if ((descriptorLo & 0x08) == 0)
                {
                    if (entrySize == 8)
                        m_physicalMemory.WriteLong(descAddr + 4, descriptorLo | 0x08);
                    else
                        m_physicalMemory.WriteLong(descAddr, descriptorLo | 0x08);
                }

                // Next table address from descriptor
                tableAddr = descriptorLo & 0xFFFFFFF0;
                // DT=2 means next level uses 4-byte entries
                entrySize = 4;
                break;
            }

            case 3: // Valid 8-byte table descriptor (long format)
            {
                // Accumulate write-protect
                writeProtected |= (descriptorLo & 0x04) != 0;

                // Set Used bit if not set
                if ((descriptorLo & 0x08) == 0)
                {
                    if (entrySize == 8)
                        m_physicalMemory.WriteLong(descAddr + 4, descriptorLo | 0x08);
                    else
                        m_physicalMemory.WriteLong(descAddr, descriptorLo | 0x08);
                }

                // Next table address
                tableAddr = descriptorLo & 0xFFFFFFF0;
                // DT=3 means next level uses 8-byte entries
                entrySize = 8;
                break;
            }
        }
    }

    // If we get here without finding a page descriptor, it's invalid
    MMUSR = static_cast<uint16_t>((levelsSearched & 7) | 0x0400);
    uint16_t sswFinal = BuildSSW(functionCode, /*isRead=*/!write);
    throw BusErrorException(logicalAddress, write, functionCode, sswFinal);
}

// ============================================================================
// ATC cache management
// ============================================================================

void Mmu::CacheInAtc(uint32_t logicalAddress, uint8_t functionCode,
                     uint32_t physPage, bool writeProtected,
                     bool cacheInhibit, bool modified,
                     uint32_t descriptorAddress)
{
    uint64_t atcKey = MakeAtcKey(logicalAddress, functionCode);
    int idx = MakeAtcIndex(atcKey);

    uint8_t flags = 0;
    if (writeProtected) flags |= ATC_FLAG_WRITE_PROTECTED;
    if (modified)        flags |= ATC_FLAG_MODIFIED;
    if (cacheInhibit)    flags |= ATC_FLAG_CACHE_INHIBIT;

    m_atc[idx] = AtcEntry{
        atcKey,             // Tag (non-zero = valid)
        physPage,           // PhysicalPage
        flags,              // Flags
        functionCode,       // FunctionCode
        0                   // _pad
    };
    m_atcDescAddr[idx] = descriptorAddress;
}

// ============================================================================
// PFLUSH variants
// ============================================================================

void Mmu::FlushAll()
{
    std::memset(m_atc, 0, sizeof(m_atc)); // Tag=0 = invalid
    if (OnFlush) OnFlush();
}

void Mmu::Flush(uint32_t logicalAddress)
{
    // Flush all FC variants for this address
    uint32_t pageAddr = logicalAddress & ~CachedPageMask;
    for (int i = 0; i < AtcSize; i++)
    {
        if (m_atc[i].Tag != 0 && static_cast<uint32_t>(m_atc[i].Tag & 0xFFFFFFFF) == pageAddr)
            m_atc[i].Tag = 0;
    }
    if (OnFlush) OnFlush();
}

void Mmu::FlushByFC(uint8_t functionCode, uint8_t mask)
{
    // PFLUSH FC,#mask: flush entries where (entryFC & mask) == (fc & mask)
    for (int i = 0; i < AtcSize; i++)
    {
        if (m_atc[i].Tag != 0 && (m_atc[i].FunctionCode & mask) == (functionCode & mask))
            m_atc[i].Tag = 0;
    }
    if (OnFlush) OnFlush();
}

void Mmu::FlushByFCAndAddress(uint8_t functionCode, uint8_t mask, uint32_t logicalAddress)
{
    // PFLUSH FC,#mask,(ea): flush matching FC+mask for specific page
    uint32_t pageAddr = logicalAddress & ~CachedPageMask;
    for (int i = 0; i < AtcSize; i++)
    {
        if (m_atc[i].Tag != 0 &&
            (m_atc[i].FunctionCode & mask) == (functionCode & mask) &&
            static_cast<uint32_t>(m_atc[i].Tag & 0xFFFFFFFF) == pageAddr)
            m_atc[i].Tag = 0;
    }
    if (OnFlush) OnFlush();
}

// ============================================================================
// PLOAD
// ============================================================================

void Mmu::PLoad(uint32_t logicalAddress, bool supervisorMode, bool write)
{
    uint8_t fc = supervisorMode ? static_cast<uint8_t>(5) : static_cast<uint8_t>(1);
    PLoad(logicalAddress, supervisorMode, write, fc);
}

void Mmu::PLoad(uint32_t logicalAddress, bool supervisorMode, bool write, uint8_t functionCode)
{
    // Per MC68030 UM: "The PLOAD instruction does not alter the MMUSR."
    uint16_t savedMMUSR = MMUSR;
    try
    {
        TableWalk(logicalAddress, supervisorMode, write, functionCode);
    }
    catch (const BusErrorException&)
    {
        // PLOAD does not generate bus error exceptions
    }
    MMUSR = savedMMUSR;
}

// ============================================================================
// PTEST
// ============================================================================

void Mmu::PTest(uint32_t logicalAddress, bool supervisorMode, bool write)
{
    uint8_t fc = supervisorMode ? static_cast<uint8_t>(5) : static_cast<uint8_t>(1);
    PTest(logicalAddress, supervisorMode, write, fc, 7);
}

void Mmu::PTest(uint32_t logicalAddress, bool supervisorMode, bool write,
                uint8_t functionCode, int maxLevel)
{
    MMUSR = 0;
    m_lastDescriptorAddress = 0;

    if (!m_enabled)
    {
        MMUSR = 0; // Valid translation (no flags set -- no I bit)
        return;
    }

    // Per M68000 PRM: TTx check only applies to level 0 (ATC search).
    // For levels 1-7, T bit is always 0 and table walk is always performed.
    if (maxLevel == 0)
    {
        // Level 0: check TTx registers (with R/W sensitivity)
        if (m_ttEnabled && (IsTransparent(logicalAddress, functionCode, m_tt0, write) ||
            IsTransparent(logicalAddress, functionCode, m_tt1, write)))
        {
            MMUSR = 0x0040; // T (Transparent, bit 6)
            return;
        }
        // Level 0: ATC-only search (not yet implemented -- return I bit for ATC miss)
        // TODO: implement proper ATC search for level 0
        MMUSR = 0x0400; // I (Invalid) -- no ATC entry found
        return;
    }

    // Levels 1-7: Non-destructive table walk for PTEST
    PTestTableWalk(logicalAddress, supervisorMode, write, functionCode, maxLevel);
}

void Mmu::PTestTableWalk(uint32_t logicalAddress, bool supervisorMode,
                         bool write, uint8_t functionCode, int maxLevel)
{
    // Root pointer selection based on FC2 bit (same as TableWalk)
    bool isSupervisorAccess = (functionCode & 4) != 0;
    uint64_t rootPointer = (isSupervisorAccess && m_sre) ? SRP : CRP;

    uint32_t rpUpper = static_cast<uint32_t>(rootPointer >> 32);
    uint32_t rpLower = static_cast<uint32_t>(rootPointer & 0xFFFFFFFF);

    int rpDT = static_cast<int>(rpUpper & 3);
    uint32_t tableAddr = rpLower & 0xFFFFFFF0;

    if (rpDT == 0)
    {
        MMUSR = 0x0400; // I (Invalid, bit 10)
        return;
    }

    int shift = 32 - m_initialShiftCached;
    int tia = m_tiaCached, tib = m_tibCached, tic = m_ticCached, tid = m_tidCached;
    bool writeProtected = false;
    bool cacheInhibit = false;
    int levelsSearched = 0;
    int entrySize = (rpDT == 3) ? 8 : 4;
    int tableIndexBits[4] = { tia, tib, tic, tid };

    for (int level = 0; level < 4; level++)
    {
        int indexBits = tableIndexBits[level];
        if (indexBits == 0) continue;

        if (levelsSearched >= maxLevel) break;

        shift -= indexBits;
        int index = static_cast<int>((logicalAddress >> shift) & ((1 << indexBits) - 1));

        uint32_t descAddr = tableAddr + static_cast<uint32_t>(index * entrySize);
        m_lastDescriptorAddress = descAddr;

        uint32_t descriptorLo;
        if (entrySize == 8)
        {
            m_physicalMemory.ReadLong(descAddr); // read and discard upper long
            descriptorLo = m_physicalMemory.ReadLong(descAddr + 4);
        }
        else
        {
            descriptorLo = m_physicalMemory.ReadLong(descAddr);
        }

        int dt = static_cast<int>(descriptorLo & 3);
        levelsSearched++;

        switch (dt)
        {
            case 0: // Invalid
                MMUSR = static_cast<uint16_t>(
                    (levelsSearched & 7) |  // N (bits 2-0)
                    0x0400                   // I (Invalid, bit 10)
                );
                return;

            case 1: // Page descriptor
            {
                bool wp = (descriptorLo & 0x04) != 0;
                bool m  = (descriptorLo & 0x10) != 0;
                bool ci = (descriptorLo & 0x40) != 0;

                writeProtected |= wp;
                cacheInhibit |= ci;

                MMUSR = static_cast<uint16_t>(
                    (levelsSearched & 7) |               // N (bits 2-0)
                    (writeProtected ? 0x0800 : 0) |      // W (bit 11)
                    (m ? 0x0200 : 0)                     // M (bit 9)
                );
                return;
            }

            case 2: // Short table descriptor
                writeProtected |= (descriptorLo & 0x04) != 0;
                tableAddr = descriptorLo & 0xFFFFFFF0;
                entrySize = 4;
                break;

            case 3: // Long table descriptor
                writeProtected |= (descriptorLo & 0x04) != 0;
                tableAddr = descriptorLo & 0xFFFFFFF0;
                entrySize = 8;
                break;
        }
    }

    // Reached max level without finding page descriptor
    MMUSR = static_cast<uint16_t>(
        (levelsSearched & 7) |               // N (bits 2-0)
        (writeProtected ? 0x0800 : 0)        // W (bit 11)
    );
}

// ============================================================================
// Diagnostic page table walk (non-destructive, for logging)
// ============================================================================

std::string Mmu::DiagnosticTableWalk(uint32_t logicalAddress, uint8_t functionCode)
{
    if (!m_enabled) return "MMU disabled";

    bool isSupervisorAccess = (functionCode & 4) != 0;
    uint64_t rootPointer = (isSupervisorAccess && m_sre) ? SRP : CRP;
    const char* rpName = (isSupervisorAccess && m_sre) ? "SRP" : "CRP";

    uint32_t rpUpper = static_cast<uint32_t>(rootPointer >> 32);
    uint32_t rpLower = static_cast<uint32_t>(rootPointer & 0xFFFFFFFF);
    int rpDT = static_cast<int>(rpUpper & 3);
    uint32_t tableAddr = rpLower & 0xFFFFFFF0;

    std::string result = std::format("{}=${:016X} DT={} tbl=${:08X}", rpName, rootPointer, rpDT, tableAddr);

    if (rpDT == 0) { result += " INVALID-ROOT"; return result; }

    int shift = 32 - m_initialShiftCached;
    int tableIndexBits[4] = { m_tiaCached, m_tibCached, m_ticCached, m_tidCached };
    const char* levelNames[4] = { "A", "B", "C", "D" };
    int entrySize = (rpDT == 3) ? 8 : 4;
    bool wp = false;

    for (int level = 0; level < 4; level++)
    {
        int indexBits = tableIndexBits[level];
        if (indexBits == 0) continue;

        shift -= indexBits;
        int index = static_cast<int>((logicalAddress >> shift) & ((1 << indexBits) - 1));
        uint32_t descAddr = tableAddr + static_cast<uint32_t>(index * entrySize);

        uint32_t descriptorLo = 0, descriptorHi = 0;
        try
        {
            if (entrySize == 8)
            {
                descriptorHi = m_physicalMemory.ReadLong(descAddr);
                descriptorLo = m_physicalMemory.ReadLong(descAddr + 4);
            }
            else
            {
                descriptorLo = m_physicalMemory.ReadLong(descAddr);
            }
        }
        catch (...)
        {
            result += std::format(" | L{}: idx={} @${:08X} READ-FAIL", levelNames[level], index, descAddr);
            return result;
        }

        int dt = static_cast<int>(descriptorLo & 3);
        bool descWP = (descriptorLo & 0x04) != 0;
        bool descU = (descriptorLo & 0x08) != 0;
        bool descM = (descriptorLo & 0x10) != 0;
        wp |= descWP;

        std::string descStr = (entrySize == 8)
            ? std::format("${:08X}_{:08X}", descriptorHi, descriptorLo)
            : std::format("${:08X}", descriptorLo);

        result += std::format(" | L{}: idx={} @${:08X} desc={} DT={}", levelNames[level], index, descAddr, descStr, dt);

        switch (dt)
        {
            case 0:
                result += " INVALID";
                return result;
            case 1:
            {
                uint32_t pageMask = 0xFFFFFFFF << shift;
                uint32_t physPage = (descriptorLo & pageMask) & 0xFFFFFF00;
                uint32_t pageOffset = static_cast<uint32_t>(logicalAddress & ~pageMask);
                uint32_t physAddr = physPage | pageOffset;
                result += std::format(" PAGE WP={} U={} M={} phys=${:08X} (accum-WP={})",
                    descWP ? "True" : "False",
                    descU ? "True" : "False",
                    descM ? "True" : "False",
                    physAddr,
                    wp ? "True" : "False");
                return result;
            }
            case 2:
                tableAddr = descriptorLo & 0xFFFFFFF0;
                entrySize = 4;
                result += std::format(" TBL4 WP={}", descWP ? "True" : "False");
                break;
            case 3:
                tableAddr = descriptorLo & 0xFFFFFFF0;
                entrySize = 8;
                result += std::format(" TBL8 WP={}", descWP ? "True" : "False");
                break;
        }
    }

    result += " | NO-PAGE-FOUND";
    return result;
}

std::string Mmu::DumpLevelATable(uint8_t functionCode)
{
    if (!m_enabled) return "MMU disabled";

    bool isSupervisorAccess = (functionCode & 4) != 0;
    uint64_t rootPointer = (isSupervisorAccess && m_sre) ? SRP : CRP;
    const char* rpName = (isSupervisorAccess && m_sre) ? "SRP" : "CRP";

    uint32_t rpUpper = static_cast<uint32_t>(rootPointer >> 32);
    uint32_t rpLower = static_cast<uint32_t>(rootPointer & 0xFFFFFFFF);
    int rpDT = static_cast<int>(rpUpper & 3);
    uint32_t tableAddr = rpLower & 0xFFFFFFF0;

    std::string result = std::format("[PTDUMP] {}=${:016X} DT={} tbl=${:08X} TIA={}\n",
        rpName, rootPointer, rpDT, tableAddr, m_tiaCached);

    if (rpDT == 0) { result += "[PTDUMP] ROOT INVALID\n"; return result; }
    if (m_tiaCached == 0) { result += "[PTDUMP] TIA=0, no level-A table\n"; return result; }

    int numEntries = 1 << m_tiaCached;
    int entrySize = (rpDT == 3) ? 8 : 4;
    int validCount = 0;

    // Dump all level-A entries with a compact format
    for (int i = 0; i < numEntries; i++)
    {
        uint32_t descAddr = tableAddr + static_cast<uint32_t>(i * entrySize);
        uint32_t descriptorLo = 0;
        try
        {
            if (entrySize == 8)
            {
                m_physicalMemory.ReadLong(descAddr); // skip upper long
                descriptorLo = m_physicalMemory.ReadLong(descAddr + 4);
            }
            else
            {
                descriptorLo = m_physicalMemory.ReadLong(descAddr);
            }
        }
        catch (...) { continue; }

        int dt = static_cast<int>(descriptorLo & 3);
        if (dt != 0) validCount++;

        // Only log first 8 entries and any valid entries (to keep output manageable)
        if (i < 8 || dt != 0)
        {
            uint32_t shift = 32 - m_tiaCached;
            uint32_t vaStart = static_cast<uint32_t>(i) << shift;
            result += std::format("[PTDUMP]   [{}] @${:08X} desc=${:08X} DT={} VA=${:08X}-${:08X}",
                i, descAddr, descriptorLo, dt, vaStart, vaStart + ((1u << shift) - 1));
            if (dt == 2) result += std::format(" ->tbl4=${:08X}", descriptorLo & 0xFFFFFFF0);
            if (dt == 3) result += std::format(" ->tbl8=${:08X}", descriptorLo & 0xFFFFFFF0);
            result += "\n";
        }
    }
    result += std::format("[PTDUMP] Total: {}/{} entries valid\n", validCount, numEntries);
    return result;
}

} // namespace Em68030::Core
