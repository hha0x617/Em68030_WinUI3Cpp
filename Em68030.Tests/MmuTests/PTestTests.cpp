#include "pch.h"
#include <gtest/gtest.h>
#include "Helpers/MmuTestFixture.h"

using namespace Em68030::Core;

// PTEST 命令テスト。非破壊的テーブルウォークを行い、MMUSR に結果をセットする。

class PTestTests : public Em68030::Tests::MmuTestFixture {};

TEST_F(PTestTests, PTest_ValidPage_SetsLevelCount)
{
    SetupPageTableEntry(0x10000000, 0x01000000);

    Mmu.PTest(0x10000000, true, false, 5, 7);

    // N (bits 2-0) should be non-zero (levels searched)
    int levelsSearched = Mmu.MMUSR & 7;
    EXPECT_GT(levelsSearched, 0) << "Expected levelsSearched > 0";
    // With 2-level page table (TIA=4, TIB=4), expect 2 levels
    EXPECT_EQ(2, levelsSearched);
}

TEST_F(PTestTests, PTest_InvalidPage_SetsInvalidBit)
{
    SetupInvalidPage(0x20000000);

    Mmu.PTest(0x20000000, true, false, 5, 7);

    // I bit (0x0400) should be set
    EXPECT_NE(0, Mmu.MMUSR & 0x0400);
}

TEST_F(PTestTests, PTest_WriteProtectedPage_SetsWPBit)
{
    SetupWriteProtectedPage(0x30000000, 0x03000000);

    Mmu.PTest(0x30000000, true, true, 5, 7);

    // W bit (0x0800) should be set
    EXPECT_NE(0, Mmu.MMUSR & 0x0800);
}

TEST_F(PTestTests, PTest_ModifiedPage_SetsModifiedBit)
{
    SetupPageTableEntry(0x10000000, 0x01000000, /*wp=*/false, /*modified=*/true);

    Mmu.PTest(0x10000000, true, false, 5, 7);

    // M bit (0x0200) should be set
    EXPECT_NE(0, Mmu.MMUSR & 0x0200);
}

TEST_F(PTestTests, PTest_MmuDisabled_ReturnsZero)
{
    // Disable MMU
    Mmu.SetTC(0x00000000);

    Mmu.PTest(0x10000000, true, false, 5, 7);

    // MMUSR should be 0 when MMU is disabled
    EXPECT_EQ(0, static_cast<int>(Mmu.MMUSR));
}

TEST_F(PTestTests, PTest_Level0_Transparent_SetsTBit)
{
    // Setup TT0 to match address range 0x10xxxxxx
    Mmu.SetTT0(0x10008107); // Base=0x10, Mask=0x00, E=1, RWM=1, FC mask=7 (any FC)

    // PTest at level 0 checks TT registers
    Mmu.PTest(0x10000000, true, false, 5, 0);

    // T bit (0x0040) should be set
    EXPECT_NE(0, Mmu.MMUSR & 0x0040);
}

TEST_F(PTestTests, PTest_SetsLastDescriptorAddress)
{
    SetupPageTableEntry(0x10000000, 0x01000000);

    Mmu.PTest(0x10000000, true, false, 5, 7);

    // LastDescriptorAddress should point to the page descriptor
    EXPECT_NE(0u, Mmu.GetLastDescriptorAddress());
}

TEST_F(PTestTests, PTest_MaxLevel_LimitsSearch)
{
    SetupPageTableEntry(0x10000000, 0x01000000);

    // With maxLevel=1, should only search 1 level (Level A table descriptor)
    Mmu.PTest(0x10000000, true, false, 5, 1);

    // N (bits 2-0) should be 1 (stopped at level A)
    int levelsSearched = Mmu.MMUSR & 7;
    EXPECT_EQ(1, levelsSearched);
}
