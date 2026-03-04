#include "pch.h"
#include <gtest/gtest.h>

#include "Helpers/CpuTestFixture.h"
#include "Core/JitCompiler.h"
#include "Core/Alu.h"

namespace Em68030::Tests {

class JitCompilerTest : public CpuTestFixture {
protected:
    Core::JitCompiler compiler;
    Core::JitCache cache;

    // Helper: compile block at given PC / physical address
    std::unique_ptr<Core::CompiledBlock> CompileAt(uint32_t pc, uint32_t physAddr)
    {
        Cpu.PC = pc;
        return compiler.TryCompile(Cpu, pc, physAddr);
    }
};

// ============================================================================
// MOVEQ tests
// ============================================================================

TEST_F(JitCompilerTest, Moveq_PositiveValue)
{
    // MOVEQ #42, D3 → opcode = 0x762A
    // Followed by RTS (unsupported) to terminate the block at 1 instruction
    Memory.WriteWord(0x1000, 0x762A);
    Memory.WriteWord(0x1002, 0x4E75); // RTS (unsupported → terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[3] = 0;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[3], 42u);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, Moveq_NegativeValue)
{
    // MOVEQ #-1, D0 → opcode = 0x70FF
    Memory.WriteWord(0x1000, 0x70FF);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 0xFFFFFFFFu);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, Moveq_Zero)
{
    // MOVEQ #0, D1 → opcode = 0x7200
    Memory.WriteWord(0x1000, 0x7200);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[1] = 0x12345678;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[1], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_FALSE(Cpu.GetFlagN());
}

TEST_F(JitCompilerTest, Moveq_PreservesXFlag)
{
    // MOVEQ #1, D0 → 0x7001
    Memory.WriteWord(0x1000, 0x7001);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.SetCCRByte(0x10); // X=1
    uint32_t nextPC = block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 1u);
    EXPECT_TRUE(Cpu.GetFlagX()); // X preserved
}

// ============================================================================
// MOVE.L Dn,Dm tests
// ============================================================================

TEST_F(JitCompilerTest, MoveLDnDm_CopiesRegister)
{
    // MOVE.L D2, D5 → opcode = 0x2A02
    // MOVE.L: 0010 ddd ddd sss sss → dst=D5(101), dstMode=000, src=D2(010), srcMode=000
    // = 0010 101 000 000 010 = 0x2A02
    Memory.WriteWord(0x1000, 0x2A02);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[2] = 0xDEADBEEF;
    Cpu.D[5] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[5], 0xDEADBEEFu);
    EXPECT_TRUE(Cpu.GetFlagN()); // bit 31 set
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MoveLDnDm_SetsZeroFlag)
{
    // MOVE.L D0, D1 → 0x2200
    Memory.WriteWord(0x1000, 0x2200);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.D[1] = 0x12345678;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[1], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
}

// ============================================================================
// ADD.L Dn,Dm tests
// ============================================================================

TEST_F(JitCompilerTest, AddLDnDm_AddsRegisters)
{
    // ADD.L D1, D0 → 1101 000 010 000 001 = 0xD081
    Memory.WriteWord(0x1000, 0xD081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 100;
    Cpu.D[1] = 200;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 300u);
}

TEST_F(JitCompilerTest, AddLDnDm_SetsCarryOnOverflow)
{
    // ADD.L D1, D0 → 0xD081
    Memory.WriteWord(0x1000, 0xD081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xFFFFFFFF;
    Cpu.D[1] = 1;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_TRUE(Cpu.GetFlagC());
    EXPECT_TRUE(Cpu.GetFlagX());
}

// ============================================================================
// SUB.L Dn,Dm tests
// ============================================================================

TEST_F(JitCompilerTest, SubLDnDm_SubtractsRegisters)
{
    // SUB.L D1, D0 → 1001 000 010 000 001 = 0x9081
    Memory.WriteWord(0x1000, 0x9081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 500;
    Cpu.D[1] = 200;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 300u);
}

// ============================================================================
// CMP.L Dn,Dm tests
// ============================================================================

TEST_F(JitCompilerTest, CmpLDnDm_SetsFlags_DoesNotModifyDest)
{
    // CMP.L D1, D0 → 1011 000 010 000 001 = 0xB081
    Memory.WriteWord(0x1000, 0xB081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 100;
    Cpu.D[1] = 100;
    Cpu.SetCCRByte(0x10); // X=1
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 100u); // Not modified
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_TRUE(Cpu.GetFlagX()); // X preserved by CMP
}

TEST_F(JitCompilerTest, CmpLDnDm_SetsNegative)
{
    // CMP.L D1, D0 → 0xB081
    Memory.WriteWord(0x1000, 0xB081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 50;
    Cpu.D[1] = 100;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 50u); // Not modified
    EXPECT_TRUE(Cpu.GetFlagN()); // 50-100 is negative
}

// ============================================================================
// AND.L / OR.L / EOR.L tests
// ============================================================================

TEST_F(JitCompilerTest, AndLDnDm_AndsRegisters)
{
    // AND.L D1, D0 → 1100 000 010 000 001 = 0xC081
    Memory.WriteWord(0x1000, 0xC081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xFF00FF00;
    Cpu.D[1] = 0x0F0F0F0F;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x0F000F00u);
}

TEST_F(JitCompilerTest, OrLDnDm_OrsRegisters)
{
    // OR.L D1, D0 → 1000 000 010 000 001 = 0x8081
    Memory.WriteWord(0x1000, 0x8081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xFF000000;
    Cpu.D[1] = 0x000000FF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFF0000FFu);
    EXPECT_TRUE(Cpu.GetFlagN());
}

TEST_F(JitCompilerTest, EorLDnDm_XorsRegisters)
{
    // EOR.L D0, D1 → 1011 000 110 000 001 = 0xB181
    Memory.WriteWord(0x1000, 0xB181);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xFFFFFFFF;
    Cpu.D[1] = 0xFF00FF00;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[1], 0x00FF00FFu);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

// ============================================================================
// Bcc.B tests
// ============================================================================

TEST_F(JitCompilerTest, BccB_Taken)
{
    // BEQ.B +4 → 0x6704 (cond=7=EQ, disp=+4)
    Memory.WriteWord(0x1000, 0x6704);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.SetCCRByte(0x04); // Z=1
    uint32_t nextPC = block->Execute(Cpu);
    // target = (0x1000+2) + 4 = 0x1006
    EXPECT_EQ(nextPC, 0x1006u);
}

TEST_F(JitCompilerTest, BccB_NotTaken)
{
    // BEQ.B +4 → 0x6704
    Memory.WriteWord(0x1000, 0x6704);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.SetCCRByte(0); // Z=0
    uint32_t nextPC = block->Execute(Cpu);
    // fallthrough = 0x1002
    EXPECT_EQ(nextPC, 0x1002u);
}

// ============================================================================
// BRA.B tests
// ============================================================================

TEST_F(JitCompilerTest, BraB_UnconditionalBranch)
{
    // BRA.B +6 → 0x6006
    Memory.WriteWord(0x1000, 0x6006);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    uint32_t nextPC = block->Execute(Cpu);
    // target = (0x1000+2) + 6 = 0x1008
    EXPECT_EQ(nextPC, 0x1008u);
}

// ============================================================================
// NOP tests
// ============================================================================

TEST_F(JitCompilerTest, Nop_AdvancesPC)
{
    // NOP → 0x4E71
    Memory.WriteWord(0x1000, 0x4E71);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    uint32_t nextPC = block->Execute(Cpu);
    EXPECT_EQ(nextPC, 0x1002u);
}

// ============================================================================
// Multi-instruction block tests
// ============================================================================

TEST_F(JitCompilerTest, MultiInstruction_MoveqAddSub)
{
    // MOVEQ #10, D0 → 0x700A
    // MOVEQ #20, D1 → 0x7214
    // ADD.L D1, D0 → 0xD081
    Memory.WriteWord(0x1000, 0x700A);
    Memory.WriteWord(0x1002, 0x7214);
    Memory.WriteWord(0x1004, 0xD081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Cpu.D[0] = 0;
    Cpu.D[1] = 0;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
    EXPECT_EQ(nextPC, 0x1006u);
    EXPECT_EQ(Cpu.D[0], 30u);
    EXPECT_EQ(Cpu.D[1], 20u);
}

// ============================================================================
// Dead flag elimination tests
// ============================================================================

TEST_F(JitCompilerTest, DeadFlagElimination_MiddleInstructionSkipsFlags)
{
    // MOVEQ #10, D0 → 0x700A (flags dead — overwritten by ADD)
    // ADD.L D1, D0 → 0xD081 (flags live — last instruction)
    Memory.WriteWord(0x1000, 0x700A);
    Memory.WriteWord(0x1002, 0xD081);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    // Check that the first op has needsFlags=false
    EXPECT_FALSE(block->Ops[0].needsFlags);
    // Last op has needsFlags=true
    EXPECT_TRUE(block->Ops[1].needsFlags);
}

TEST_F(JitCompilerTest, DeadFlagElimination_BccMakesPriorFlagsLive)
{
    // MOVEQ #0, D0 → 0x7000 (flags live — Bcc reads them)
    // BEQ.B +4 → 0x6704 (branch reads flags)
    Memory.WriteWord(0x1000, 0x7000);
    Memory.WriteWord(0x1002, 0x6704);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    // MOVEQ should have needsFlags=true because Bcc reads the flags
    EXPECT_TRUE(block->Ops[0].needsFlags);
}

// ============================================================================
// Backward branch exclusion
// ============================================================================

TEST_F(JitCompilerTest, BackwardBranch_ExcludedFromBlock)
{
    // BRA.B -2 → 0x60FE (backward to self → infinite loop)
    Memory.WriteWord(0x1000, 0x60FE);
    auto block = CompileAt(0x1000, 0x1000);
    // Backward branch at first instruction → no block
    EXPECT_EQ(block, nullptr);
}

TEST_F(JitCompilerTest, BackwardBranch_TerminatesBeforeIt)
{
    // MOVEQ #1, D0 → 0x7001
    // BRA.B -4 → 0x60FC (backward)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0x60FC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    // Block should contain only MOVEQ (backward BRA excluded)
    EXPECT_EQ(block->InstructionCount, 1);
}

// ============================================================================
// Unsupported instruction terminates block
// ============================================================================

TEST_F(JitCompilerTest, UnsupportedInstruction_TerminatesBlock)
{
    // MOVEQ #5, D0 → 0x700A
    // ILLEGAL → 0x4AFC (unsupported)
    Memory.WriteWord(0x1000, 0x700A);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);
}

TEST_F(JitCompilerTest, UnsupportedAtStart_ReturnsNull)
{
    // RTS → 0x4E75 (unsupported)
    Memory.WriteWord(0x1000, 0x4E75);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
}

// ============================================================================
// JitCache tests
// ============================================================================

TEST_F(JitCompilerTest, JitCache_AddAndRetrieve)
{
    auto block = std::make_unique<Core::CompiledBlock>();
    block->PhysicalAddress = 0x1000;
    block->InstructionCount = 3;
    auto* rawPtr = block.get();

    cache.AddBlock(0x1000, std::move(block));
    auto* found = cache.TryGetBlock(0x1000);
    EXPECT_EQ(found, rawPtr);
    EXPECT_EQ(cache.GetBlockCount(), 1);
}

TEST_F(JitCompilerTest, JitCache_MissReturnsNull)
{
    auto* found = cache.TryGetBlock(0x2000);
    EXPECT_EQ(found, nullptr);
}

TEST_F(JitCompilerTest, JitCache_InvalidateAll)
{
    auto block = std::make_unique<Core::CompiledBlock>();
    block->PhysicalAddress = 0x1000;
    cache.AddBlock(0x1000, std::move(block));

    cache.InvalidateAll();
    EXPECT_EQ(cache.TryGetBlock(0x1000), nullptr);
    EXPECT_EQ(cache.GetBlockCount(), 0);
}

TEST_F(JitCompilerTest, JitCache_Uncompilable)
{
    EXPECT_FALSE(cache.IsUncompilable(0x1000));
    cache.MarkUncompilable(0x1000);
    EXPECT_TRUE(cache.IsUncompilable(0x1000));

    // InvalidateAll clears uncompilable flags
    cache.InvalidateAll();
    EXPECT_FALSE(cache.IsUncompilable(0x1000));
}

TEST_F(JitCompilerTest, JitCache_IncrementAndGetCount)
{
    EXPECT_EQ(cache.IncrementAndGetCount(0x1000), 1);
    EXPECT_EQ(cache.IncrementAndGetCount(0x1000), 2);
    EXPECT_EQ(cache.IncrementAndGetCount(0x1000), 3);

    // Different address
    EXPECT_EQ(cache.IncrementAndGetCount(0x2000), 1);
}

TEST_F(JitCompilerTest, JitCache_CountSaturatesAt255)
{
    for (int i = 0; i < 260; i++)
        cache.IncrementAndGetCount(0x1000);
    EXPECT_EQ(cache.IncrementAndGetCount(0x1000), 255);
}

// ============================================================================
// Forward branch inclusion
// ============================================================================

TEST_F(JitCompilerTest, ForwardBranch_IncludedAndTerminatesBlock)
{
    // MOVEQ #1, D0 → 0x7001
    // BRA.B +4 → 0x6004 (forward)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0x6004);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    // Block should contain both instructions
    EXPECT_EQ(block->InstructionCount, 2);
}

// ============================================================================
// BSR not supported
// ============================================================================

TEST_F(JitCompilerTest, Bsr_NotSupported)
{
    // BSR.B +4 → 0x6104 (cond=1=BSR)
    Memory.WriteWord(0x1000, 0x6104);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr); // BSR is unsupported
}

} // namespace Em68030::Tests
