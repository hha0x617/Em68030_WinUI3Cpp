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

#include "IO/PccDevice.h"
#include "Core/MC68030.h"
#include "Core/Memory.h"

namespace Em68030::Tests {

// ============================================================================
// Test Fixture
// ============================================================================

class PccDeviceTest : public ::testing::Test {
protected:
    Core::Memory memory;
    Core::MC68030 cpu{ memory };
    IO::PccDevice pcc{ cpu };

    static constexpr uint32_t Base = 0xFFFE1000;

    // Register offsets
    static constexpr uint32_t Timer1Preload = Base + 0x10;
    static constexpr uint32_t Timer1Count   = Base + 0x12;
    static constexpr uint32_t Timer2Preload = Base + 0x14;
    static constexpr uint32_t Timer2Count   = Base + 0x16;
    static constexpr uint32_t Timer1Icr     = Base + 0x18;
    static constexpr uint32_t Timer1Control = Base + 0x19;
    static constexpr uint32_t Timer2Icr     = Base + 0x1A;
    static constexpr uint32_t Timer2Control = Base + 0x1B;
    static constexpr uint32_t SccIcr        = Base + 0x26;
    static constexpr uint32_t LanceIcr      = Base + 0x28;
    static constexpr uint32_t ScsiIcr       = Base + 0x2A;
    static constexpr uint32_t ScsiIcrAlias  = Base + 0x30;
    static constexpr uint32_t Soft1Icr      = Base + 0x2C;
    static constexpr uint32_t VectorBase    = Base + 0x2D;
    static constexpr uint32_t Soft2Icr      = Base + 0x2E;
    static constexpr uint32_t DmaIcr        = Base + 0x20;
    static constexpr uint32_t DmaControl    = Base + 0x21;
    static constexpr uint32_t PrinterIcr    = Base + 0x1E;
    static constexpr uint32_t AbortIcr      = Base + 0x24;
};

// ============================================================================
// ICR Write-1-to-Clear (W1C) and bit layout
// ============================================================================

TEST_F(PccDeviceTest, IcrDefault_NoInterrupt)
{
    // All ICRs default to 0 — no INT, no IEN
    EXPECT_EQ(pcc.ReadByte(Timer1Icr), 0x00);
    EXPECT_EQ(pcc.ReadByte(SccIcr), 0x00);
    EXPECT_EQ(pcc.ReadByte(ScsiIcr), 0x00);
    EXPECT_EQ(pcc.ReadByte(LanceIcr), 0x00);
}

TEST_F(PccDeviceTest, WriteIcr_SetsIenAndLevel)
{
    // Write IEN=1, level=5 to timer1 ICR
    pcc.WriteByte(Timer1Icr, 0x0D); // 0x08 (IEN) | 0x05 (level)
    EXPECT_EQ(pcc.ReadByte(Timer1Icr) & 0x0F, 0x0D);
}

TEST_F(PccDeviceTest, WriteIcr_W1C_ClearsInt)
{
    // Simulate timer overflow to set INT
    pcc.WriteByte(Timer1Icr, 0x0D); // IEN=1, level=5
    // Use SetDeviceInterrupt to set INT for a device ICR instead
    pcc.SetDeviceInterrupt("scc", true);
    EXPECT_EQ(pcc.ReadByte(SccIcr) & 0x80, 0x80); // INT set

    // Write 0x80 to clear INT (W1C)
    pcc.SetDeviceInterrupt("scc", false); // deassert device first
    pcc.WriteByte(SccIcr, 0x80);
    EXPECT_EQ(pcc.ReadByte(SccIcr) & 0x80, 0x00); // INT cleared
}

TEST_F(PccDeviceTest, WriteIcr_PreservesIntIfNotW1C)
{
    // Use DMA ICR (non-device) to test INT preservation
    // SetDmaDone sets DMA ICR INT
    pcc.SetDmaDone();
    EXPECT_EQ(pcc.ReadByte(DmaIcr) & 0x80, 0x80);

    // Write IEN/level WITHOUT bit 7 → INT preserved
    pcc.WriteByte(DmaIcr, 0x0D); // no bit 7
    EXPECT_EQ(pcc.ReadByte(DmaIcr) & 0x80, 0x80);
}

// ============================================================================
// Level-sensitive device ICR (SCC, SCSI, LANCE)
// ============================================================================

TEST_F(PccDeviceTest, DeviceIcr_Relatch_WhileActive)
{
    // Set SCSI device active
    pcc.SetDeviceInterrupt("scsi", true);
    EXPECT_EQ(pcc.ReadByte(ScsiIcr) & 0x80, 0x80);

    // Try to W1C while device still active — INT re-latches
    pcc.WriteByte(ScsiIcr, 0x8D); // W1C + IEN=1 + level=5
    EXPECT_EQ(pcc.ReadByte(ScsiIcr) & 0x80, 0x80); // re-latched
}

TEST_F(PccDeviceTest, DeviceIcr_ClearsAfterDeassert)
{
    pcc.SetDeviceInterrupt("lance", true);
    EXPECT_EQ(pcc.ReadByte(LanceIcr) & 0x80, 0x80);

    // Deassert device, then W1C
    pcc.SetDeviceInterrupt("lance", false);
    pcc.WriteByte(LanceIcr, 0x80);
    EXPECT_EQ(pcc.ReadByte(LanceIcr) & 0x80, 0x00); // cleared
}

TEST_F(PccDeviceTest, SetDeviceInterrupt_Deassert_ClearsInt)
{
    pcc.SetDeviceInterrupt("scc", true);
    EXPECT_EQ(pcc.ReadByte(SccIcr) & 0x80, 0x80);

    pcc.SetDeviceInterrupt("scc", false);
    EXPECT_EQ(pcc.ReadByte(SccIcr) & 0x80, 0x00);
}

// ============================================================================
// SCSI ICR alias at offset 0x30 (Linux uses this)
// ============================================================================

TEST_F(PccDeviceTest, ScsiIcrAlias_ReadsFromSameRegister)
{
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0D); // IEN=1, level=5 (INT re-latches)
    EXPECT_EQ(pcc.ReadByte(ScsiIcrAlias), pcc.ReadByte(ScsiIcr));
}

TEST_F(PccDeviceTest, ScsiIcrAlias_WritesAffectSameRegister)
{
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcrAlias, 0x0D); // IEN=1, level=5 via alias
    EXPECT_EQ(pcc.ReadByte(ScsiIcr) & 0x0F, 0x0D);
}

// ============================================================================
// Software Interrupt ICR
// ============================================================================

TEST_F(PccDeviceTest, SoftIcr_WriteBit7_SetsInt)
{
    // Writing bit 7 to soft ICR SETS INT (unlike device/timer ICR where it clears)
    pcc.WriteByte(Soft1Icr, 0x8D); // INT=1, IEN=1, level=5
    EXPECT_EQ(pcc.ReadByte(Soft1Icr) & 0x80, 0x80);
}

TEST_F(PccDeviceTest, SoftIcr_WriteBit7Zero_ClearsInt)
{
    pcc.WriteByte(Soft1Icr, 0x8D); // set
    pcc.WriteByte(Soft1Icr, 0x0D); // clear bit 7
    EXPECT_EQ(pcc.ReadByte(Soft1Icr) & 0x80, 0x00);
}

// ============================================================================
// UpdateIPL — Priority Resolution
// ============================================================================

TEST_F(PccDeviceTest, NoInterrupt_IplZero)
{
    EXPECT_EQ(cpu.GetPendingIPL(), 0);
}

TEST_F(PccDeviceTest, SingleDevice_SetsIpl)
{
    // SCSI: INT=1, IEN=1, level=2
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A); // IEN=1 + level=2 (INT re-latches)
    EXPECT_EQ(cpu.GetPendingIPL(), 2);
}

TEST_F(PccDeviceTest, HigherLevel_Wins)
{
    // SCSI at level 2
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A); // IEN=1, level=2

    // SCC at level 4
    pcc.SetDeviceInterrupt("scc", true);
    pcc.WriteByte(SccIcr, 0x0C); // IEN=1, level=4

    EXPECT_EQ(cpu.GetPendingIPL(), 4);
}

TEST_F(PccDeviceTest, DisabledIen_DoesNotContribute)
{
    // SCSI INT=1 but IEN=0 → should not assert IPL
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x02); // level=2, NO IEN
    EXPECT_EQ(cpu.GetPendingIPL(), 0);
}

TEST_F(PccDeviceTest, LevelZero_TreatedAsLevel1)
{
    // Level=0 in ICR should be treated as level 1
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x08); // IEN=1, level=0
    EXPECT_EQ(cpu.GetPendingIPL(), 1);
}

TEST_F(PccDeviceTest, ClearInterrupt_ResetsIpl)
{
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A); // IEN=1, level=2
    EXPECT_EQ(cpu.GetPendingIPL(), 2);

    pcc.SetDeviceInterrupt("scsi", false);
    pcc.WriteByte(ScsiIcr, 0x8A); // W1C
    EXPECT_EQ(cpu.GetPendingIPL(), 0);
}

// ============================================================================
// Vector Calculation
// ============================================================================

TEST_F(PccDeviceTest, DefaultVectorBase_Is0x40)
{
    EXPECT_EQ(pcc.ReadByte(VectorBase), 0x40);
}

TEST_F(PccDeviceTest, ScsiInterrupt_VectorIs0x45)
{
    // SCSI = PCCV_SCSI = 5, default base = 0x40 → vector = 0x45
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A); // IEN=1, level=2
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 5);
}

TEST_F(PccDeviceTest, SccInterrupt_VectorIs0x43)
{
    // SCC = PCCV_ZS = 3, default base = 0x40 → vector = 0x43
    pcc.SetDeviceInterrupt("scc", true);
    pcc.WriteByte(SccIcr, 0x0C); // IEN=1, level=4
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 3);
}

TEST_F(PccDeviceTest, LanceInterrupt_VectorIs0x44)
{
    // LANCE = PCCV_LE = 4, default base = 0x40 → vector = 0x44
    pcc.SetDeviceInterrupt("lance", true);
    pcc.WriteByte(LanceIcr, 0x0B); // IEN=1, level=3
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 4);
}

TEST_F(PccDeviceTest, CustomVectorBase)
{
    pcc.WriteByte(VectorBase, 0x50);
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A);
    EXPECT_EQ(cpu.GetPendingVector(), 0x50 + 5);
}

TEST_F(PccDeviceTest, SoftInterrupt_VectorIs0x4A)
{
    // SOFT1 = PCCV_SOFT1 = 10, default base = 0x40 → vector = 0x4A
    pcc.WriteByte(Soft1Icr, 0x8B); // INT=1, IEN=1, level=3
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 10);
}

// ============================================================================
// Timer Control
// ============================================================================

TEST_F(PccDeviceTest, TimerPreload_ReadWrite)
{
    pcc.WriteWord(Timer1Preload, 0xF9C0);
    EXPECT_EQ(pcc.ReadWord(Timer1Preload), 0xF9C0);
}

TEST_F(PccDeviceTest, TimerCount_ReadWrite)
{
    pcc.WriteWord(Timer1Count, 0x1234);
    // Timer is stopped (CEN=0), so count reads back the stored value
    EXPECT_EQ(pcc.ReadWord(Timer1Count), 0x1234);
}

TEST_F(PccDeviceTest, TimerControl_CciClear_ResetsCountToPreload)
{
    pcc.WriteWord(Timer1Preload, 0xF9C0);
    pcc.WriteWord(Timer1Count, 0x1234);
    // Write control with CCI=0 → counter resets to preload
    pcc.WriteByte(Timer1Control, 0x00); // PCC_TIMERCLEAR
    EXPECT_EQ(pcc.ReadWord(Timer1Count), 0xF9C0);
}

TEST_F(PccDeviceTest, TimerControl_CciSet_PreservesCount)
{
    pcc.WriteWord(Timer1Preload, 0xF9C0);
    pcc.WriteWord(Timer1Count, 0x1234);
    // Write control with CCI=1 → counter preserved
    pcc.WriteByte(Timer1Control, 0x01); // PCC_TIMERENABLE
    EXPECT_EQ(pcc.ReadWord(Timer1Count), 0x1234);
}

TEST_F(PccDeviceTest, TimerControl_ClearsOverflowCounter)
{
    // Read control register — overflow count in bits 7:4
    // After writing control, overflow count resets to 0
    pcc.WriteByte(Timer1Control, 0x07); // start
    uint8_t ctrl = pcc.ReadByte(Timer1Control);
    EXPECT_EQ(ctrl >> 4, 0); // overflow count = 0
}

TEST_F(PccDeviceTest, TimerControl_ReadIncludesOverflowCount)
{
    // Control bits 2:0 are the control field
    // Bits 7:4 are the overflow count (read-only)
    pcc.WriteByte(Timer1Control, 0x07); // CCI=1, COC=1, CEN=1
    uint8_t ctrl = pcc.ReadByte(Timer1Control);
    EXPECT_EQ(ctrl & 0x07, 0x07); // control bits preserved
}

// ============================================================================
// DMA Registers
// ============================================================================

TEST_F(PccDeviceTest, DmaDataAddress_ReadWrite)
{
    pcc.SetDmaDataAddress(0x00100000);
    EXPECT_EQ(pcc.GetDmaDataAddress(), 0x00100000u);
}

TEST_F(PccDeviceTest, DmaDataAddress_ViaRegisterAccess)
{
    // Write DMA Data Address ($04-$07) byte by byte
    pcc.WriteByte(Base + 0x04, 0x00);
    pcc.WriteByte(Base + 0x05, 0x20);
    pcc.WriteByte(Base + 0x06, 0x00);
    pcc.WriteByte(Base + 0x07, 0x00);
    EXPECT_EQ(pcc.GetDmaDataAddress(), 0x00200000u);
}

TEST_F(PccDeviceTest, DmaByteCount_ReadWrite)
{
    pcc.WriteByte(Base + 0x08, 0x00);
    pcc.WriteByte(Base + 0x09, 0x00);
    pcc.WriteByte(Base + 0x0A, 0x02);
    pcc.WriteByte(Base + 0x0B, 0x00);
    EXPECT_EQ(pcc.GetDmaByteCount(), 0x0200u);
}

TEST_F(PccDeviceTest, SetDmaDone_SetsDoneAndInt)
{
    pcc.WriteByte(DmaIcr, 0x0A); // IEN=1, level=2
    pcc.SetDmaDone();

    // DMAC_CSR_DONE bit set in DMA control
    EXPECT_EQ(pcc.ReadByte(DmaControl) & 0x80, 0x80);
    // DMA ICR INT set
    EXPECT_EQ(pcc.ReadByte(DmaIcr) & 0x80, 0x80);
    // IPL asserted
    EXPECT_EQ(cpu.GetPendingIPL(), 2);
}

// ============================================================================
// HardwareReset
// ============================================================================

TEST_F(PccDeviceTest, HardwareReset_ClearsAllState)
{
    // Set up various state
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A);
    pcc.WriteByte(Soft1Icr, 0x8D);
    pcc.WriteWord(Timer1Preload, 0xF9C0);
    pcc.WriteByte(Timer1Control, 0x07);

    EXPECT_NE(cpu.GetPendingIPL(), 0);

    // Reset
    pcc.HardwareReset();

    // Everything cleared
    EXPECT_EQ(pcc.ReadByte(ScsiIcr), 0x00);
    EXPECT_EQ(pcc.ReadByte(SccIcr), 0x00);
    EXPECT_EQ(pcc.ReadByte(LanceIcr), 0x00);
    EXPECT_EQ(pcc.ReadByte(Soft1Icr), 0x00);
    EXPECT_EQ(pcc.ReadByte(Timer1Icr), 0x00);
    EXPECT_EQ(pcc.ReadByte(Timer1Control) & 0x07, 0x00);
    EXPECT_EQ(cpu.GetPendingIPL(), 0);
}

// ============================================================================
// SuppressInterrupt for SCSI
// ============================================================================

TEST_F(PccDeviceTest, ScsiInterrupt_SuppressesInterrupt)
{
    // SetDeviceInterrupt("scsi", true) should call SuppressInterrupt(8)
    pcc.WriteByte(ScsiIcr, 0x0A); // IEN=1, level=2
    pcc.SetDeviceInterrupt("scsi", true);

    // IPL should be asserted (SuppressInterrupt doesn't affect IPL itself)
    EXPECT_EQ(cpu.GetPendingIPL(), 2);
}

TEST_F(PccDeviceTest, NonScsiInterrupt_NoSuppression)
{
    // SCC interrupt should not trigger SuppressInterrupt
    pcc.WriteByte(SccIcr, 0x0C); // IEN=1, level=4
    pcc.SetDeviceInterrupt("scc", true);
    EXPECT_EQ(cpu.GetPendingIPL(), 4);
}

// ============================================================================
// Multiple devices — priority and coexistence
// ============================================================================

TEST_F(PccDeviceTest, MultipleDevices_HighestWins)
{
    // SCC at level 4, SCSI at level 2, LANCE at level 3
    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0A); // level=2

    pcc.SetDeviceInterrupt("lance", true);
    pcc.WriteByte(LanceIcr, 0x0B); // level=3

    pcc.SetDeviceInterrupt("scc", true);
    pcc.WriteByte(SccIcr, 0x0C); // level=4

    EXPECT_EQ(cpu.GetPendingIPL(), 4);
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 3); // SCC = PCCV_ZS = 3
}

TEST_F(PccDeviceTest, ClearHighestDevice_NextHighestTakesOver)
{
    pcc.SetDeviceInterrupt("scc", true);
    pcc.WriteByte(SccIcr, 0x0C); // level=4

    pcc.SetDeviceInterrupt("lance", true);
    pcc.WriteByte(LanceIcr, 0x0B); // level=3

    EXPECT_EQ(cpu.GetPendingIPL(), 4);

    // Clear SCC
    pcc.SetDeviceInterrupt("scc", false);
    pcc.WriteByte(SccIcr, 0x8C); // W1C

    EXPECT_EQ(cpu.GetPendingIPL(), 3);
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 4); // LANCE = PCCV_LE = 4
}

TEST_F(PccDeviceTest, SameLevel_EarlierDeviceWins)
{
    // SCC (PCCV=3) and SCSI (PCCV=5) at same level
    // SCC is checked first, so it should win
    pcc.SetDeviceInterrupt("scc", true);
    pcc.WriteByte(SccIcr, 0x0B); // level=3

    pcc.SetDeviceInterrupt("scsi", true);
    pcc.WriteByte(ScsiIcr, 0x0B); // level=3

    EXPECT_EQ(cpu.GetPendingIPL(), 3);
    // At equal levels, the first checked (lower PCCV) wins.
    // SCC is checked before SCSI, but CheckIcr uses > (not >=),
    // so the first device at a level wins only if no higher exists.
    // Actually: SCC is checked first and sets maxLevel=3.
    // SCSI is checked second with level=3 — but 3 > 3 is false, so SCC wins.
    EXPECT_EQ(cpu.GetPendingVector(), 0x40 + 3); // SCC
}

// ============================================================================
// Watchdog Timer
// ============================================================================

TEST_F(PccDeviceTest, Watchdog_ArmValue_TriggersCallback)
{
    bool callbackInvoked = false;
    pcc.OnWatchdogReset = [&callbackInvoked]() {
        callbackInvoked = true;
    };

    // Writing 0xA5 to watchdog register arms it and triggers immediate reset
    pcc.WriteByte(Base + 0x1D, 0xA5);
    EXPECT_TRUE(callbackInvoked);
}

TEST_F(PccDeviceTest, Watchdog_ClearValue_DoesNotTriggerCallback)
{
    bool callbackInvoked = false;
    pcc.OnWatchdogReset = [&callbackInvoked]() {
        callbackInvoked = true;
    };

    // Writing 0x0A (clear) should NOT trigger watchdog — treated as normal ICR write
    pcc.WriteByte(Base + 0x1D, 0x0A);
    EXPECT_FALSE(callbackInvoked);
}

TEST_F(PccDeviceTest, Watchdog_OtherValues_DoNotTriggerCallback)
{
    int callCount = 0;
    pcc.OnWatchdogReset = [&callCount]() {
        callCount++;
    };

    // Various non-0xA5 values should not trigger watchdog
    pcc.WriteByte(Base + 0x1D, 0x00);
    pcc.WriteByte(Base + 0x1D, 0x0D); // IEN=1, level=5
    pcc.WriteByte(Base + 0x1D, 0x80); // W1C
    pcc.WriteByte(Base + 0x1D, 0xFF);
    EXPECT_EQ(callCount, 0);
}

TEST_F(PccDeviceTest, Watchdog_NoCallback_DoesNotCrash)
{
    // No callback set — writing 0xA5 should not crash
    pcc.OnWatchdogReset = nullptr;
    pcc.WriteByte(Base + 0x1D, 0xA5);
    // Just verify no crash
}

TEST_F(PccDeviceTest, Watchdog_LinuxRebootSequence)
{
    // Simulates Linux mvme147_reset(): clear then arm
    bool callbackInvoked = false;
    pcc.OnWatchdogReset = [&callbackInvoked]() {
        callbackInvoked = true;
    };

    pcc.WriteByte(Base + 0x1D, 0x0A); // Clear timer
    EXPECT_FALSE(callbackInvoked);

    pcc.WriteByte(Base + 0x1D, 0xA5); // Arm watchdog
    EXPECT_TRUE(callbackInvoked);
}

} // namespace Em68030::Tests
