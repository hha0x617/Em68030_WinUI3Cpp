#include "pch.h"
#include <gtest/gtest.h>
#include "Helpers/CpuTestFixture.h"

using namespace Em68030::Core;

// SR/スタック切り替えテスト。
// 例外処理時の SR 保存、スーパーバイザモード遷移、VBR 参照を検証する。

class StackAndSrTests : public Em68030::Tests::CpuTestFixture {};

TEST_F(StackAndSrTests, ExceptionProcessing_SavesSR)
{
    uint32_t originalSP = Cpu.A[7];
    uint16_t originalSR = 0x2700;
    Cpu.SR = originalSR;
    Cpu.PC = 0x00001000;

    // Place a handler (NOP + RTE) at the vector address
    uint32_t handlerAddr = 0x00002000;
    Memory.WriteWord(handlerAddr, 0x4E71); // NOP
    Memory.WriteWord(handlerAddr + 2, 0x4E73); // RTE

    // Setup vector table: vector 32 (TRAP #0) at VBR + 32*4 = VBR + 0x80
    Cpu.VBR = 0x00000000;
    Memory.WriteLong(0x00000080, handlerAddr);

    // Raise exception (vector 32 = TRAP #0)
    Cpu.RaiseException(32);

    // SR should be saved on the stack (at the top of the frame)
    // Format 0 frame: SR(2) + PC(4) + Format/Vector(2) = 8 bytes
    uint32_t frameSP = Cpu.A[7];
    uint16_t savedSR = Memory.ReadWord(frameSP);

    EXPECT_EQ(originalSR, savedSR);
}

TEST_F(StackAndSrTests, ExceptionProcessing_EntersSupervisorMode)
{
    // Start in user mode
    Cpu.USP = 0x00700000;
    Cpu.SR = 0x0000; // User mode, no flags
    Cpu.A[7] = 0x00700000; // USP

    // Setup handler
    uint32_t handlerAddr = 0x00002000;
    Memory.WriteWord(handlerAddr, 0x4E71); // NOP

    Cpu.VBR = 0x00000000;
    Memory.WriteLong(0x00000080, handlerAddr); // TRAP #0 vector

    // Raise exception
    Cpu.RaiseException(32);

    // CPU should now be in supervisor mode
    EXPECT_TRUE(Cpu.GetSupervisorMode());
    // S bit (bit 13) should be set
    EXPECT_NE(0, Cpu.SR & 0x2000);
}

TEST_F(StackAndSrTests, ExceptionProcessing_UsesVBR)
{
    Cpu.PC = 0x00001000;

    // Set VBR to a non-zero base
    uint32_t vbr = 0x00010000;
    Cpu.VBR = vbr;

    // Place handler address in vector table at VBR + vector*4
    uint32_t handlerAddr = 0x00003000;
    Memory.WriteWord(handlerAddr, 0x4E71); // NOP

    // Vector 4 = Illegal Instruction at VBR + 4*4 = VBR + 16
    Memory.WriteLong(vbr + 16, handlerAddr);

    Cpu.RaiseException(4);

    // PC should be the handler address read from VBR-relative vector table
    EXPECT_EQ(handlerAddr, Cpu.PC);
}

TEST_F(StackAndSrTests, SetSR_UserToSupervisor_SwapsStack)
{
    // Start in supervisor mode with known SSP
    Cpu.SR = 0x2700;
    Cpu.A[7] = 0x00800000; // SSP
    Cpu.SSP = 0x00800000;
    Cpu.USP = 0x00600000;

    // Switch to user mode
    Cpu.SetSR(0x0000);
    EXPECT_EQ(0x00600000u, Cpu.A[7]); // A7 should be USP now

    // Switch back to supervisor mode
    Cpu.SetSR(0x2700);
    EXPECT_EQ(0x00800000u, Cpu.A[7]); // A7 should be SSP again
}
