// Copyright 2026 hha0x617
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace Em68030::Core {

class Memory;    // Forward declaration
class MC68030;   // Forward declaration — for SetBusError back-ref

class Mmu {
public:
    explicit Mmu(Memory& physicalMemory);

    // Owning CPU. Used to report page-walk faults via MC68030::SetBusError
    // instead of throwing. Set once by MC68030's constructor.
    void SetCpu(MC68030* cpu) { m_cpu = cpu; }

    // Last fault info — mirrors what was sent to the CPU's SetBusError.
    // Set unconditionally (even when no CPU is attached) so unit tests that
    // drive Mmu directly can inspect SSW / FC / address after a failing
    // Translate call.
    uint32_t LastFaultAddress      = 0;
    bool     LastFaultIsWrite      = false;
    uint8_t  LastFaultFunctionCode = 0;
    uint16_t LastFaultSSW          = 0;

    void Reset();

    // --- MMU Registers ---
    uint64_t CRP = 0;   // CPU Root Pointer (64-bit)
    uint64_t SRP = 0;   // Supervisor Root Pointer (64-bit)
    uint16_t MMUSR = 0; // MMU Status Register

    // Last descriptor address (for PTEST A-register result)
    uint32_t GetLastDescriptorAddress() const { return m_lastDescriptorAddress; }

    // --- TC register with cached derived fields ---
    uint32_t GetTC() const { return m_tc; }
    void SetTC(uint32_t value);

    // --- TT registers with _ttEnabled flag ---
    uint32_t GetTT0() const { return m_tt0; }
    void SetTT0(uint32_t value);

    uint32_t GetTT1() const { return m_tt1; }
    void SetTT1(uint32_t value);

    // Public accessors for cached values
    bool GetEnabled() const { return m_enabled; }
    bool GetSRE() const { return m_sre; }
    uint32_t CachedPageMask = 0xFFF;

    // --- Main translation entry point ---
    uint32_t Translate(uint32_t logicalAddress, bool supervisorMode, bool write, uint8_t functionCode);
    uint32_t Translate(uint32_t logicalAddress, bool supervisorMode, bool write);

    // Inline fast path: returns physical address on ATC read-hit, UINT32_MAX on miss.
    // Caller must fall back to full Translate() on miss.
    inline uint32_t TranslateReadFast(uint32_t logicalAddress, uint8_t functionCode) {
        if (!m_enabled) return logicalAddress;
        uint32_t pageAddr = logicalAddress & ~CachedPageMask;
        uint64_t atcKey = (static_cast<uint64_t>(functionCode & 7) << 32) | pageAddr;
        uint32_t addr32 = static_cast<uint32_t>(atcKey & 0xFFFFFFFF);
        uint32_t fc32 = static_cast<uint32_t>(atcKey >> 32);
        uint32_t hash = (addr32 >> 12) ^ (addr32 >> 18) ^ (addr32 >> 24) ^ (fc32 * 0x9E3779B9u);
        int idx = static_cast<int>(hash & AtcMask);
        const AtcEntry& e = m_atc[idx];
        if (e.Tag == atcKey)
            return e.PhysicalPage | (logicalAddress & CachedPageMask);
        return UINT32_MAX; // Miss — caller must use full Translate
    }

    // --- PFLUSH variants ---
    void FlushAll();
    void Flush(uint32_t logicalAddress);
    void FlushByFC(uint8_t functionCode, uint8_t mask);
    void FlushByFCAndAddress(uint8_t functionCode, uint8_t mask, uint32_t logicalAddress);

    // --- PLOAD ---
    void PLoad(uint32_t logicalAddress, bool supervisorMode, bool write);
    void PLoad(uint32_t logicalAddress, bool supervisorMode, bool write, uint8_t functionCode);

    // --- PTEST ---
    void PTest(uint32_t logicalAddress, bool supervisorMode, bool write);
    void PTest(uint32_t logicalAddress, bool supervisorMode, bool write,
               uint8_t functionCode, int maxLevel);

    // --- Diagnostic page table walk (non-destructive, for logging) ---
    std::string DiagnosticTableWalk(uint32_t logicalAddress, uint8_t functionCode);

    // --- Dump level-A table summary (for diagnosing level-1 invalid faults) ---
    std::string DumpLevelATable(uint8_t functionCode);

    // Callback to invalidate CPU fetch cache on any TLB flush
    std::function<void()> OnFlush;

private:
    // --- ATC entry (compact: 16 bytes per entry) ---
    // Tag==0 means invalid (no valid mapping uses FC=0 + addr=0 simultaneously)
    static constexpr uint8_t ATC_FLAG_WRITE_PROTECTED = 0x01;
    static constexpr uint8_t ATC_FLAG_MODIFIED         = 0x02;
    static constexpr uint8_t ATC_FLAG_CACHE_INHIBIT    = 0x04;

    struct AtcEntry {
        uint64_t Tag = 0;              // Full key (FC << 32 | pageAddr); 0 = invalid
        uint32_t PhysicalPage = 0;
        uint8_t Flags = 0;            // Packed: WP|M|CI
        uint8_t FunctionCode = 0;
        uint16_t _pad = 0;
    };

    // --- ATC key management ---
    uint64_t MakeAtcKey(uint32_t logicalAddress, uint8_t functionCode) const;
    static int MakeAtcIndex(uint64_t atcKey);

    // --- SSW construction ---
    static bool IsDataAccess(uint8_t functionCode);
    static uint16_t BuildSSW(uint8_t functionCode, bool isRead);

    // --- Transparent Translation ---
    bool IsTransparent(uint32_t address, uint8_t functionCode, uint32_t tt, bool isWrite) const;

    // --- Page Table Walk ---
    uint32_t TableWalk(uint32_t logicalAddress, bool supervisorMode, bool write, uint8_t functionCode);

    // --- ATC cache management ---
    void CacheInAtc(uint32_t logicalAddress, uint8_t functionCode,
                    uint32_t physPage, bool writeProtected,
                    bool cacheInhibit, bool modified,
                    uint32_t descriptorAddress);

    // --- PTEST table walk ---
    void PTestTableWalk(uint32_t logicalAddress, bool supervisorMode,
                        bool write, uint8_t functionCode, int maxLevel);

    // --- Private fields ---
    uint32_t m_tc = 0;
    uint32_t m_tt0 = 0;
    uint32_t m_tt1 = 0;
    uint32_t m_lastDescriptorAddress = 0;

    // Cached TC-derived fields (updated on TC write)
    bool m_enabled = false;
    bool m_sre = false;
    int m_pageSizeCached = 4096;
    int m_initialShiftCached = 0;
    int m_tiaCached = 0;
    int m_tibCached = 0;
    int m_ticCached = 0;
    int m_tidCached = 0;
    bool m_ttEnabled = false;

    // Pre-allocated array for TableWalk (avoids heap allocation per walk)
    int m_tableIndexBits[4] = {};

    // ATC (Address Translation Cache) - direct-mapped, expanded for better hit rate
    static constexpr int AtcSize = 4096;
    static constexpr int AtcMask = AtcSize - 1;
    AtcEntry m_atc[AtcSize] = {};
    uint32_t m_atcDescAddr[AtcSize] = {}; // Parallel: descriptor address for M-bit writeback

    Memory& m_physicalMemory;
    MC68030* m_cpu = nullptr;  // for SetBusError on fault
};

} // namespace Em68030::Core
