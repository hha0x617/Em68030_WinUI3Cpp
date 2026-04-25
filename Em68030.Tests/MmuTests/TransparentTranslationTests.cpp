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

// TT0/TT1 透過的変換テスト。

class TransparentTranslationTests : public Em68030::Tests::MmuTestFixture {};

// TT0 でマッチするアドレスは物理アドレス = 論理アドレス（identity mapping）
TEST_F(TransparentTranslationTests, TT_Enabled_MatchingAddress_ReturnsIdentity)
{
    // TT0: Base=0xFF, Mask=0x00, E=1, RWM=1, FC mask=7 (any)
    Mmu.SetTT0(0xFF008107);

    // Address 0xFF000100 should match TT0 and return identity mapping
    uint32_t pa = Mmu.Translate(0xFF000100, true, false, 5);

    EXPECT_EQ(0xFF000100u, pa);
}

// E=0 では TT は無効でマッチしない
TEST_F(TransparentTranslationTests, TT_Disabled_NoMatch)
{
    // TT0: Base=0x10, Mask=0x00, E=0 (disabled)
    Mmu.SetTT0(0x10000107); // E bit (bit 15) is 0

    // Without TT match, page table is used
    SetupPageTableEntry(0x10000000, 0x01000000);
    FlushAtc();

    uint32_t pa = Mmu.Translate(0x10000000, true, false, 5);

    // Should use page table mapping, not identity
    EXPECT_EQ(0x01000000u, pa);
}

// アドレスマスクで範囲が拡大される
TEST_F(TransparentTranslationTests, TT_AddressMask_Works)
{
    // TT0: Base=0xF0, Mask=0x0F, E=1, RWM=1, FC mask=7
    Mmu.SetTT0(0xF00F8107);

    // Both 0xF0000000 and 0xFF000000 should match
    uint32_t pa1 = Mmu.Translate(0xF0000000, true, false, 5);
    uint32_t pa2 = Mmu.Translate(0xFF000000, true, false, 5);

    EXPECT_EQ(0xF0000000u, pa1);
    EXPECT_EQ(0xFF000000u, pa2);
}

// FC ベースとマスクが機能する
TEST_F(TransparentTranslationTests, TT_FCMatch_Works)
{
    // TT0: Base=0xFF, E=1, RWM=1, FCBase=5, FCMask=0 (exact match FC=5)
    Mmu.SetTT0(0xFF008150);

    // FC=5 (supervisor data) should match
    uint32_t pa = Mmu.Translate(0xFF000000, true, false, 5);
    EXPECT_EQ(0xFF000000u, pa);

    // FC=1 (user data) should NOT match TT0 → needs page table
    SetupPageTableEntry(0xF0000000, 0x0F000000);
    FlushAtc();

    // FC=1 should go through page table walk, not TT
    Mmu.LastFaultSSW = 0;
    (void)Mmu.Translate(0xFF000000, false, false, 1);
    EXPECT_NE(0u, Mmu.LastFaultSSW) << "Non-TT access should have raised a fault";
}

// R/W=1, RWM=0 → read のみ透過
TEST_F(TransparentTranslationTests, TT_RW_ReadOnly)
{
    // TT0: Base=0xFF, E=1, RWM=0, R/W=1 (read transparent), FC mask=7
    Mmu.SetTT0(0xFF008207);

    // Read should be transparent
    uint32_t pa = Mmu.Translate(0xFF000000, true, false, 5);
    EXPECT_EQ(0xFF000000u, pa);

    // Write should NOT be transparent → bus error (no page table entry)
    Mmu.LastFaultSSW = 0;
    (void)Mmu.Translate(0xFF000000, true, true, 5);
    EXPECT_NE(0u, Mmu.LastFaultSSW) << "Non-TT access should have raised a fault";
}

// R/W=0, RWM=0 → write のみ透過
TEST_F(TransparentTranslationTests, TT_RW_WriteOnly)
{
    // TT0: Base=0xFF, E=1, RWM=0, R/W=0 (write transparent), FC mask=7
    Mmu.SetTT0(0xFF008007);

    // Write should be transparent
    uint32_t pa = Mmu.Translate(0xFF000000, true, true, 5);
    EXPECT_EQ(0xFF000000u, pa);

    // Read should NOT be transparent → bus error (no page table entry)
    Mmu.LastFaultSSW = 0;
    (void)Mmu.Translate(0xFF000000, true, false, 5);
    EXPECT_NE(0u, Mmu.LastFaultSSW) << "Non-TT access should have raised a fault";
}

// RWM=1 → read/write 両方透過
TEST_F(TransparentTranslationTests, TT_RWM_BothDirections)
{
    // TT0: Base=0xFF, E=1, RWM=1, FC mask=7
    Mmu.SetTT0(0xFF008107);

    // Both read and write should be transparent
    uint32_t paRead = Mmu.Translate(0xFF000000, true, false, 5);
    uint32_t paWrite = Mmu.Translate(0xFF000000, true, true, 5);

    EXPECT_EQ(0xFF000000u, paRead);
    EXPECT_EQ(0xFF000000u, paWrite);
}

// TT1 も同様に機能する
TEST_F(TransparentTranslationTests, TT1_AlsoWorks)
{
    // TT1: Base=0xFE, E=1, RWM=1, FC mask=7
    Mmu.SetTT1(0xFE008107);

    uint32_t pa = Mmu.Translate(0xFE000000, true, false, 5);
    EXPECT_EQ(0xFE000000u, pa);
}
