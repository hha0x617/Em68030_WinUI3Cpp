#include "pch.h"
#include <gtest/gtest.h>

#include "Helpers/CpuTestFixture.h"
#include "Core/InstructionDecoder.h"
#include "Core/JitCompiler.h"

namespace Em68030::Tests {

class CycleTableTest : public CpuTestFixture {
protected:
    Core::JitCompiler compiler;
};

// ============================================================================
// Basic cycle table lookups
// ============================================================================

TEST_F(CycleTableTest, Moveq_2Cycles)
{
    // MOVEQ #0, D0 = 0x7000
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x7000), 2);
}

TEST_F(CycleTableTest, MoveLDnDm_2Cycles)
{
    // MOVE.L D0, D1 = 0x2200
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x2200), 2);
}

TEST_F(CycleTableTest, MoveLMemSrc_HasEaCost)
{
    // MOVE.L (A0), D0 = 0x2010 — src mode=2 (An indirect), dst mode=0 (Dn)
    // 2 + 4 (srcRead) + 0 (dstWrite) = 6
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x2010), 6);
}

TEST_F(CycleTableTest, MoveLMemDst_HasEaCost)
{
    // MOVE.L D0, (A0) = 0x2080 — src mode=0 (Dn), dst mode=2 (An indirect)
    // 2 + 0 (srcRead) + 4 (dstWrite) = 6
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x2080), 6);
}

TEST_F(CycleTableTest, AddLDnDm_2Cycles)
{
    // ADD.L D0, D1 = 0xD280
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0xD280), 2);
}

TEST_F(CycleTableTest, AddLMemSrc_HasEaCost)
{
    // ADD.L (A0), D1 = 0xD290 — src mode=2 (An indirect)
    // 2 + 4 = 6
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0xD290), 6);
}

TEST_F(CycleTableTest, Rts_10Cycles)
{
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x4E75), 10);
}

TEST_F(CycleTableTest, BccB_6Cycles)
{
    // BEQ.B +2 = 0x6702
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x6702), 6);
}

TEST_F(CycleTableTest, Nop_2Cycles)
{
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x4E71), 2);
}

TEST_F(CycleTableTest, DivuW_38Cycles)
{
    // DIVU.W D0, D1 = 0x82C0
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0x82C0), 38);
}

TEST_F(CycleTableTest, MuluW_28Cycles)
{
    // MULU.W D0, D1 = 0xC2C0
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0xC2C0), 28);
}

TEST_F(CycleTableTest, FpuOp_40Cycles)
{
    // Any Line-F opcode
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0xF200), 40);
}

TEST_F(CycleTableTest, LineA_34Cycles)
{
    EXPECT_EQ(Core::InstructionDecoder::GetCycles(0xA000), 34);
}

// ============================================================================
// JIT block TotalCycles
// ============================================================================

TEST_F(CycleTableTest, JitBlock_TotalCycles)
{
    // Block: MOVEQ #1,D0 (2) + MOVEQ #2,D1 (2) + ADD.L D0,D1 (2) = 6
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1, D0
    Memory.WriteWord(0x1002, 0x7201); // MOVEQ #1, D1
    Memory.WriteWord(0x1004, 0xD280); // ADD.L D0, D1
    Memory.WriteWord(0x1006, 0x4E75); // RTS (terminates block)

    Cpu.PC = 0x1000;
    auto block = compiler.TryCompile(Cpu, 0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);
    EXPECT_EQ(block->TotalCycles, 6); // 2+2+2

    // Verify TotalCycles matches sum of individual GetCycles
    int sum = Core::InstructionDecoder::GetCycles(0x7001)
            + Core::InstructionDecoder::GetCycles(0x7201)
            + Core::InstructionDecoder::GetCycles(0xD280);
    EXPECT_EQ(block->TotalCycles, sum);
}

// ============================================================================
// Integration: CycleCount and InstructionCount increments
// ============================================================================

TEST_F(CycleTableTest, CycleCountIncrement)
{
    // Place a MOVEQ #42, D0 at PC
    Memory.WriteWord(0x1000, 0x7000 | 42); // MOVEQ #42, D0
    Cpu.PC = 0x1000;
    Cpu.CycleCount = 0;
    Cpu.InstructionCount = 0;

    Cpu.ExecuteNextFast();

    uint16_t opcode = 0x7000 | 42;
    EXPECT_EQ(Cpu.CycleCount, Core::InstructionDecoder::GetCycles(opcode));
}

TEST_F(CycleTableTest, InstructionCountIncrement)
{
    Memory.WriteWord(0x1000, 0x4E71); // NOP
    Cpu.PC = 0x1000;
    Cpu.CycleCount = 0;
    Cpu.InstructionCount = 0;

    Cpu.ExecuteNextFast();

    EXPECT_EQ(Cpu.InstructionCount, 1);
    EXPECT_EQ(Cpu.CycleCount, 2); // NOP = 2 cycles
}

} // namespace Em68030::Tests
