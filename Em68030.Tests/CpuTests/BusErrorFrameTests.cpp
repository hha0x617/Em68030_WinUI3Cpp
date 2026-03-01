#include "pch.h"
#include <gtest/gtest.h>
#include "Helpers/CpuTestFixture.h"

using namespace Em68030::Core;

// バスエラーフレーム生成テスト。
// Format $A (short bus cycle fault) フレームの構造を検証する。

class BusErrorFrameTests : public Em68030::Tests::CpuTestFixture {
protected:
    void SetUpBusErrorHandler()
    {
        // MMU disabled (TC=0) for frame layout tests
        uint32_t handlerAddr = 0x00002000;
        Memory.WriteWord(handlerAddr, 0x4E73); // RTE

        // Vector table: bus error vector (vector 2) at VBR + 8
        Cpu.VBR = 0x00000000;
        Memory.WriteLong(0x00000008, handlerAddr);
    }
};

TEST_F(BusErrorFrameTests, BusError_PushesFormatA_Frame)
{
    SetUpBusErrorHandler();
    uint32_t originalSP = Cpu.A[7];
    uint32_t originalPC = 0x00001000;
    uint16_t originalSR = Cpu.SR;
    Cpu.PC = originalPC;

    // Trigger bus error
    Cpu.RaiseBusError(0xDEADBEEF, false, 5, 0x0145);

    EXPECT_FALSE(Cpu.Halted) << "CPU should not be halted";

    // Format $A frame is 32 bytes (16 words)
    uint32_t expectedSP = originalSP - 32;
    EXPECT_EQ(expectedSP, Cpu.A[7]);

    // +$00: SR (2 bytes)
    uint16_t frameSR = Memory.ReadWord(expectedSP);
    EXPECT_EQ(originalSR, frameSR);

    // +$02: PC (4 bytes)
    uint32_t framePC = Memory.ReadLong(expectedSP + 2);
    EXPECT_EQ(originalPC, framePC);

    // +$06: Format/Vector word
    uint16_t formatVector = Memory.ReadWord(expectedSP + 6);
    int format = (formatVector >> 12) & 0xF;
    EXPECT_EQ(0xA, format); // Format $A
    int vectorOffset = formatVector & 0x0FFF;
    EXPECT_EQ(8, vectorOffset); // Vector 2 * 4 = 8
}

TEST_F(BusErrorFrameTests, BusError_Frame_ContainsFaultAddress)
{
    SetUpBusErrorHandler();
    Cpu.PC = 0x00001000;

    Cpu.RaiseBusError(0xDEADBEEF, false, 5, 0x0145);

    EXPECT_FALSE(Cpu.Halted) << "CPU should not be halted";

    // Fault address is at offset +$10 in the frame
    uint32_t sp = Cpu.A[7];
    uint32_t faultAddr = Memory.ReadLong(sp + 0x10);
    EXPECT_EQ(0xDEADBEEFu, faultAddr);
}

TEST_F(BusErrorFrameTests, BusError_Frame_ContainsSSW)
{
    SetUpBusErrorHandler();
    Cpu.PC = 0x00001000;

    uint16_t testSSW = 0x0145;
    Cpu.RaiseBusError(0xDEADBEEF, false, 5, testSSW);

    EXPECT_FALSE(Cpu.Halted) << "CPU should not be halted";

    // SSW is at offset +$0A in the frame
    uint32_t sp = Cpu.A[7];
    uint16_t frameSSW = Memory.ReadWord(sp + 0x0A);
    EXPECT_EQ(testSSW, frameSSW);
}

TEST_F(BusErrorFrameTests, BusError_DoubleBusError_Halts)
{
    Em68030::Core::Memory localMem;
    // Only map a small region that does NOT include the stack area
    localMem.AddRegion(0x00000000, 0x10000, RegionType::Ram);
    // Write reset vectors
    localMem.WriteLong(0x00000000, 0x00800000); // SSP
    localMem.WriteLong(0x00000004, 0x00001000); // PC

    MC68030 localCpu(localMem);
    localCpu.SR = 0x2700;
    localCpu.A[7] = 0x00800000; // SSP in unmapped area
    localCpu.SSP = 0x00800000;
    localCpu.VBR = 0x00000000;

    // Bus error vector handler
    localMem.WriteLong(0x00000008, 0x00002000);
    localMem.WriteWord(0x00002000, 0x4E73); // RTE

    localCpu.PC = 0x00001000;

    // Stack is at 0x00800000 which is unmapped -> push will fail -> double fault
    localCpu.RaiseBusError(0xDEADBEEF, false, 5, 0x0145);

    EXPECT_TRUE(localCpu.Halted);
}
