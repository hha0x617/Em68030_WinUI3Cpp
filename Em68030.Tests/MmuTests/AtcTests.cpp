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

#include "pch.h"
#include <gtest/gtest.h>
#include "Helpers/MmuTestFixture.h"
#include "Core/BusErrorException.h"

using namespace Em68030::Core;

// ATC (Address Translation Cache / TLB) テスト。

class AtcTests : public Em68030::Tests::MmuTestFixture {};

TEST_F(AtcTests, Atc_CachesTranslation)
{
    SetupPageTableEntry(0x10000000, 0x01000000);
    FlushAtc();

    // First translation: TableWalk (ATC miss)
    uint32_t pa1 = Mmu.Translate(0x10000000, true, false, 5);

    // Second translate should use ATC (ATC hit path)
    uint32_t pa2 = Mmu.Translate(0x10000000, true, false, 5);

    EXPECT_EQ(pa1, pa2);
    EXPECT_EQ(0x01000000u, pa1);
}

TEST_F(AtcTests, Atc_PFlush_InvalidatesAll)
{
    SetupPageTableEntry(0x10000000, 0x01000000);

    // Populate ATC
    Mmu.Translate(0x10000000, true, false, 5);

    // Invalidate the page table entry AFTER ATC is populated
    int levelAIndex = 1; // VA 0x10000000 → Level A index = 1
    uint32_t levelBTableAddr = 0x00101000 + static_cast<uint32_t>(levelAIndex * 64);
    Memory.WriteLong(levelBTableAddr, 0x00000000); // DT=0 (invalid)

    // After flush, the next translate will do a table walk and find invalid
    Mmu.FlushAll();

    EXPECT_THROW(Mmu.Translate(0x10000000, true, false, 5), BusErrorException);
}

TEST_F(AtcTests, Atc_DifferentFC_DifferentEntries)
{
    SetupPageTableEntry(0x10000000, 0x01000000);
    FlushAtc();

    // Translate with FC=5 (supervisor data)
    uint32_t pa1 = Mmu.Translate(0x10000000, true, false, 5);

    // Translate with FC=1 (user data) - should also work (same CRP)
    uint32_t pa2 = Mmu.Translate(0x10000000, false, false, 1);

    // Both should resolve correctly (same page table via CRP)
    EXPECT_EQ(0x01000000u, pa1);
    EXPECT_EQ(0x01000000u, pa2);

    // Now flush only FC=5 entries
    Mmu.FlushByFC(5, 0x07); // exact match FC=5

    // Invalidate page table
    int levelAIndex = 1;
    uint32_t levelBTableAddr = 0x00101000 + static_cast<uint32_t>(levelAIndex * 64);
    Memory.WriteLong(levelBTableAddr, 0x00000000); // invalid

    // FC=5 should fail (flushed, table walk finds invalid)
    EXPECT_THROW(Mmu.Translate(0x10000000, true, false, 5), BusErrorException);
}

TEST_F(AtcTests, Atc_WriteProtect_Cached)
{
    SetupWriteProtectedPage(0x30000000, 0x03000000);
    FlushAtc();

    // First access (read) populates ATC with WP flag
    uint32_t pa = Mmu.Translate(0x30000000, true, false, 5);
    EXPECT_EQ(0x03000000u, pa);

    // Second access (write) should hit ATC and still enforce write-protect
    EXPECT_THROW(Mmu.Translate(0x30000000, true, true, 5), BusErrorException);
}
