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

#include <gtest/gtest.h>
#include "Core/Memory.h"
#include "Core/Mmu.h"

namespace Em68030::Tests {

/// MMU テスト用ページテーブル構築ヘルパー。
/// 2レベルページテーブル（TIA=4, TIB=4, PS=12(4KB)）を構築する。
///
/// TC = 0x80C04400
///   bit 31: Enable=1
///   bits 23-20: PS=0xC (12 → 4KB pages)
///   bits 15-12: TIA=4
///   bits 11-8:  TIB=4
class MmuTestFixture : public ::testing::Test {
protected:
    Em68030::Core::Memory Memory{ 16 * 1024 * 1024 }; // 16MB
    Em68030::Core::Mmu Mmu{ Memory };

    // Page table base addresses in physical memory
    static constexpr uint32_t LevelATableBase  = 0x00100000; // 1MB mark
    static constexpr uint32_t LevelBTablesBase = 0x00101000; // After Level A table

    // TC: Enable=1, PS=12(4KB), IS=0, TIA=4, TIB=4
    static constexpr uint32_t TcValue = 0x80C04400;

    MmuTestFixture()
    {
        // Setup TC
        Mmu.SetTC(TcValue);

        // Setup CRP: DT=2 (short-format), table base at LevelATableBase
        uint64_t crpUpper = 2; // DT=2
        uint64_t crpLower = LevelATableBase;
        Mmu.CRP = (crpUpper << 32) | crpLower;

        // Initialize Level A table with zeros (invalid entries)
        for (uint32_t i = 0; i < 16 * 4; i += 4)
        {
            Memory.WriteLong(LevelATableBase + i, 0x00000000);
        }
    }

    /// VA→PA マッピングを構築する。
    void SetupPageTableEntry(uint32_t va, uint32_t pa, bool wp = false, bool modified = false)
    {
        int levelAIndex = static_cast<int>((va >> 28) & 0xF);
        int levelBIndex = static_cast<int>((va >> 24) & 0xF);

        // Ensure Level A entry points to a valid Level B table
        uint32_t levelAEntryAddr = LevelATableBase + static_cast<uint32_t>(levelAIndex * 4);
        uint32_t levelBTableAddr = LevelBTablesBase + static_cast<uint32_t>(levelAIndex * 64);

        // Level A descriptor: DT=2 (short table pointer), address = Level B table base
        uint32_t levelADesc = (levelBTableAddr & 0xFFFFFFF0) | 0x02; // DT=2
        Memory.WriteLong(levelAEntryAddr, levelADesc);

        // Level B descriptor: DT=1 (page descriptor), address = physical page
        uint32_t levelBEntryAddr = levelBTableAddr + static_cast<uint32_t>(levelBIndex * 4);
        uint32_t pageDesc = (pa & 0xFF000000) | 0x01; // DT=1 (page descriptor)
        if (wp) pageDesc |= 0x04;        // WP bit
        if (modified) pageDesc |= 0x10;  // Modified bit

        Memory.WriteLong(levelBEntryAddr, pageDesc);
    }

    /// 指定 VA に無効ページエントリを設定する。
    void SetupInvalidPage(uint32_t va)
    {
        int levelAIndex = static_cast<int>((va >> 28) & 0xF);
        int levelBIndex = static_cast<int>((va >> 24) & 0xF);

        uint32_t levelAEntryAddr = LevelATableBase + static_cast<uint32_t>(levelAIndex * 4);
        uint32_t levelBTableAddr = LevelBTablesBase + static_cast<uint32_t>(levelAIndex * 64);

        // Ensure Level A entry is valid
        uint32_t levelADesc = (levelBTableAddr & 0xFFFFFFF0) | 0x02; // DT=2
        Memory.WriteLong(levelAEntryAddr, levelADesc);

        // Level B entry: DT=0 (invalid)
        uint32_t levelBEntryAddr = levelBTableAddr + static_cast<uint32_t>(levelBIndex * 4);
        Memory.WriteLong(levelBEntryAddr, 0x00000000); // DT=0
    }

    /// WP (Write Protected) ページを設定するショートカット。
    void SetupWriteProtectedPage(uint32_t va, uint32_t pa)
    {
        SetupPageTableEntry(va, pa, /*wp=*/true, /*modified=*/false);
    }

    /// ATC をフラッシュして、次のアクセスで必ず TableWalk させる。
    void FlushAtc()
    {
        Mmu.FlushAll();
    }
};

} // namespace Em68030::Tests
