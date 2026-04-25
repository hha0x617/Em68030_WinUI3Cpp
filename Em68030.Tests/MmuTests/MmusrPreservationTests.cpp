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

// MMUSR 保存テスト — TableWalk() が MMUSR を上書きするバグの回帰テスト。
// MC68030 PRM §9.5.3: 通常のアドレス変換では MMUSR を変更してはならない。

class MmusrPreservationTests : public Em68030::Tests::MmuTestFixture {
protected:
    void SetUp() override
    {
        // Setup a valid page mapping: VA 0x10000000 → PA 0x01000000
        SetupPageTableEntry(0x10000000, 0x01000000);
    }
};

TEST_F(MmusrPreservationTests, Translate_DoesNot_ModifyMmusr)
{
    // Set a known MMUSR value (simulating a prior PTEST result)
    Mmu.MMUSR = 0x1234;

    // Translate should NOT modify MMUSR
    Mmu.Translate(0x10000000, true, false, 5);

    EXPECT_EQ(0x1234, Mmu.MMUSR);
}

TEST_F(MmusrPreservationTests, Translate_AtcMiss_PreservesMmusr)
{
    // Ensure ATC miss by flushing
    FlushAtc();
    Mmu.MMUSR = 0xABCD;

    // This will trigger a full TableWalk (ATC miss)
    Mmu.Translate(0x10000000, true, false, 5);

    EXPECT_EQ(0xABCD, Mmu.MMUSR);
}

TEST_F(MmusrPreservationTests, Translate_WriteProtect_PreservesMmusr)
{
    // Setup write-protected page
    SetupWriteProtectedPage(0x20000000, 0x02000000);
    FlushAtc();
    Mmu.MMUSR = 0x5678;

    // Write to WP page should set BusErrorPending and record IsWrite=true.
    // MMUSR behavior is preserved by the Mmu/test contract: this call takes
    // the ATC-hit-WP path (no MMUSR update) or the TableWalk WP path
    // (MMUSR updated with fault bits) — whichever, the *caller's* 0x5678
    // must not be silently wiped before the fault is reported. The test
    // retains its EXPECT_EQ to detect regressions in either path.
    Mmu.LastFaultIsWrite = false;
    (void)Mmu.Translate(0x20000000, true, true, 5);
    EXPECT_TRUE(Mmu.LastFaultIsWrite);

    EXPECT_EQ(0x5678, Mmu.MMUSR);
}

TEST_F(MmusrPreservationTests, Translate_ModifiedBitReWalk_PreservesMmusr)
{
    // First: read access to populate ATC (without Modified bit)
    FlushAtc();
    Mmu.Translate(0x10000000, true, false, 5);

    // Set known MMUSR
    Mmu.MMUSR = 0x9999;

    // Write access: ATC hit but Modified=false → triggers re-walk to set M bit
    Mmu.Translate(0x10000000, true, true, 5);

    EXPECT_EQ(0x9999, Mmu.MMUSR);
}

TEST_F(MmusrPreservationTests, PTest_Does_ModifyMmusr)
{
    Mmu.MMUSR = 0x0000;

    // PTest SHOULD modify MMUSR
    Mmu.PTest(0x10000000, true, false, 5, 7);

    // After PTest on a valid page, MMUSR should reflect the walk result
    EXPECT_NE(static_cast<uint16_t>(0), Mmu.MMUSR);
    // I bit (0x0400) should NOT be set for a valid page
    EXPECT_EQ(0, Mmu.MMUSR & 0x0400);
}

TEST_F(MmusrPreservationTests, PLoad_DoesNot_ModifyMmusr)
{
    Mmu.MMUSR = 0x4321;

    // PLoad should NOT modify MMUSR (per MC68030 UM)
    Mmu.PLoad(0x10000000, true, false, 5);

    EXPECT_EQ(0x4321, Mmu.MMUSR);
}

TEST_F(MmusrPreservationTests, PTest_Then_Translate_PreservesPTestResult)
{
    // Setup another page for translation
    SetupPageTableEntry(0x30000000, 0x03000000);
    FlushAtc();

    // PTest sets MMUSR with valid result
    Mmu.PTest(0x10000000, true, false, 5, 7);
    uint16_t ptestResult = Mmu.MMUSR;

    // Translate should NOT disturb the PTEST result
    Mmu.Translate(0x30000000, true, false, 5);

    EXPECT_EQ(ptestResult, Mmu.MMUSR);
}
