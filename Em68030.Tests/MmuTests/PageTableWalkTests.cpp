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

class PageTableWalkTests : public Em68030::Tests::MmuTestFixture {};

TEST_F(PageTableWalkTests, Walk_ValidPage_ReturnsPhysicalAddress)
{
    SetupPageTableEntry(0x10000000, 0x01000000);
    FlushAtc();

    uint32_t pa = Mmu.Translate(0x10000000, true, false, 5);

    EXPECT_EQ(0x01000000u, pa);
}

TEST_F(PageTableWalkTests, Walk_ValidPage_PreservesPageOffset)
{
    SetupPageTableEntry(0x10000000, 0x01000000);
    FlushAtc();

    // Access with page offset 0x123
    uint32_t pa = Mmu.Translate(0x10000123, true, false, 5);

    EXPECT_EQ(0x01000123u, pa);
}

TEST_F(PageTableWalkTests, Walk_InvalidPage_ThrowsBusError)
{
    SetupInvalidPage(0x20000000);
    FlushAtc();

    Mmu.LastFaultAddress = 0;
    (void)Mmu.Translate(0x20000000, true, false, 5);
    EXPECT_EQ(0x20000000u, Mmu.LastFaultAddress);
}

TEST_F(PageTableWalkTests, Walk_WriteProtectedPage_ReadSucceeds)
{
    SetupWriteProtectedPage(0x30000000, 0x03000000);
    FlushAtc();

    // Read should succeed on write-protected page
    uint32_t pa = Mmu.Translate(0x30000000, true, false, 5);

    EXPECT_EQ(0x03000000u, pa);
}

TEST_F(PageTableWalkTests, Walk_WriteProtectedPage_WriteThrowsBusError)
{
    SetupWriteProtectedPage(0x30000000, 0x03000000);
    FlushAtc();

    Mmu.LastFaultIsWrite = false;
    Mmu.LastFaultAddress = 0;
    (void)Mmu.Translate(0x30000000, true, true, 5);
    EXPECT_TRUE(Mmu.LastFaultIsWrite);
    EXPECT_EQ(0x30000000u, Mmu.LastFaultAddress);
}

TEST_F(PageTableWalkTests, Walk_SetsModifiedBit_OnWrite)
{
    // Setup page without Modified bit
    SetupPageTableEntry(0x10000000, 0x01000000, /*wp=*/false, /*modified=*/false);
    FlushAtc();

    // Write access should set Modified bit in the page descriptor
    Mmu.Translate(0x10000000, true, true, 5);

    // Verify via PTest that Modified bit is now set
    FlushAtc();
    Mmu.PTest(0x10000000, true, true, 5, 7);

    // M bit (0x0200) should be set in MMUSR
    EXPECT_NE(0, Mmu.MMUSR & 0x0200);
}

TEST_F(PageTableWalkTests, Walk_SetsUsedBit)
{
    SetupPageTableEntry(0x10000000, 0x01000000);
    FlushAtc();

    // Read the page descriptor before access
    // Level A index = 1, Level B index = 0
    // Level B table addr = 0x00101000 + (1 * 64) = 0x00101040
    // Level B entry addr = 0x00101040 + (0 * 4) = 0x00101040
    uint32_t levelBEntryAddr = 0x00101000 + (1 * 64) + (0 * 4);
    uint32_t descBefore = Memory.ReadLong(levelBEntryAddr);
    EXPECT_EQ(0, static_cast<int>(descBefore & 0x08)); // U bit should be 0 initially

    // Access the page
    Mmu.Translate(0x10000000, true, false, 5);

    // Used bit should now be set
    uint32_t descAfter = Memory.ReadLong(levelBEntryAddr);
    EXPECT_NE(0, static_cast<int>(descAfter & 0x08)); // U bit (0x08) should be set
}

TEST_F(PageTableWalkTests, Walk_MultiplePages_IndependentMapping)
{
    SetupPageTableEntry(0x10000000, 0x01000000);
    SetupPageTableEntry(0x20000000, 0x02000000);
    SetupPageTableEntry(0x30000000, 0x03000000);
    FlushAtc();

    uint32_t pa1 = Mmu.Translate(0x10000000, true, false, 5);
    uint32_t pa2 = Mmu.Translate(0x20000000, true, false, 5);
    uint32_t pa3 = Mmu.Translate(0x30000000, true, false, 5);

    EXPECT_EQ(0x01000000u, pa1);
    EXPECT_EQ(0x02000000u, pa2);
    EXPECT_EQ(0x03000000u, pa3);
}
