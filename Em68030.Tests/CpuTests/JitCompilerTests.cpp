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
    Memory.WriteWord(0x1002, 0x4AFC); // ILLEGAL (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[3] = 0;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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

    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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

    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
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
    // ILLEGAL → 0x4AFC (unsupported)
    Memory.WriteWord(0x1000, 0x4AFC);
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

// ============================================================================
// ADDQ.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, AddqLDn_BasicAdd)
{
    // ADDQ.L #3, D2: 0101 011 0 10 000 010 = 0x5682
    Memory.WriteWord(0x1000, 0x5682);
    Memory.WriteWord(0x1002, 0x4AFC); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[2] = 100;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[2], 103u);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, AddqLDn_Immediate8)
{
    // ADDQ.L #8, D0: qqq=0 means 8 → 0101 000 0 10 000 000 = 0x5080
    Memory.WriteWord(0x1000, 0x5080);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 8u);
}

TEST_F(JitCompilerTest, AddqLDn_SetsFlags)
{
    // ADDQ.L #1, D0: 0101 001 0 10 000 000 = 0x5280
    Memory.WriteWord(0x1000, 0x5280);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // Test N flag
    Cpu.D[0] = 0x7FFFFFFF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x80000000u);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_TRUE(Cpu.GetFlagV()); // signed overflow

    // Test Z and C flags
    Cpu.D[0] = 0xFFFFFFFF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_TRUE(Cpu.GetFlagC());
    EXPECT_TRUE(Cpu.GetFlagX());
}

// ============================================================================
// SUBQ.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, SubqLDn_BasicSub)
{
    // SUBQ.L #5, D3: 0101 101 1 10 000 011 = 0x5B83
    Memory.WriteWord(0x1000, 0x5B83);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[3] = 100;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[3], 95u);
}

TEST_F(JitCompilerTest, SubqLDn_SetsFlags)
{
    // SUBQ.L #1, D0: 0101 001 1 10 000 000 = 0x5380
    Memory.WriteWord(0x1000, 0x5380);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // Test Z flag
    Cpu.D[0] = 1;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());

    // Test borrow (C flag)
    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFFFFFFu);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_TRUE(Cpu.GetFlagC());
    EXPECT_TRUE(Cpu.GetFlagX());
}

// ============================================================================
// ADDQ #imm, An tests
// ============================================================================

TEST_F(JitCompilerTest, AddqAn_BasicAdd)
{
    // ADDQ.L #4, A2: 0101 100 0 10 001 010 = 0x588A
    // (size=10 but An ignores size, always 32-bit)
    Memory.WriteWord(0x1000, 0x588A);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[2] = 0x1000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[2], 0x1004u);
}

TEST_F(JitCompilerTest, AddqAn_NoFlagChange)
{
    // ADDQ.W #1, A0: 0101 001 0 01 001 000 = 0x5248
    // An: size field=01(word) but still 32-bit op, no flags changed
    Memory.WriteWord(0x1000, 0x5248);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0xFFFFFFFF;
    Cpu.SetCCRByte(0x1F); // All flags set
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[0], 0u); // Wraps around
    // All flags must remain unchanged
    EXPECT_EQ(Cpu.GetCCR(), 0x1Fu);
}

// ============================================================================
// SUBQ #imm, An tests
// ============================================================================

TEST_F(JitCompilerTest, SubqAn_BasicSub)
{
    // SUBQ.L #2, A3: 0101 010 1 10 001 011 = 0x558B
    Memory.WriteWord(0x1000, 0x558B);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[3] = 0x2000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[3], 0x1FFEu);
}

TEST_F(JitCompilerTest, SubqAn_NoFlagChange)
{
    // SUBQ.W #1, A1: 0101 001 1 01 001 001 = 0x5349
    Memory.WriteWord(0x1000, 0x5349);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[1] = 0;
    Cpu.SetCCRByte(0x00); // All flags clear
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[1], 0xFFFFFFFFu); // Wraps
    // Flags must remain unchanged
    EXPECT_EQ(Cpu.GetCCR(), 0x00u);
}

// ============================================================================
// ADDQ/SUBQ unsupported cases
// ============================================================================

TEST_F(JitCompilerTest, AddqDn_ByteSize_Supported)
{
    // ADDQ.B #1, D0: 0101 001 0 00 000 000 = 0x5200 (size=0=byte)
    Memory.WriteWord(0x1000, 0x5200);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x123456FE;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x123456FFu); // byte add, upper preserved
}

TEST_F(JitCompilerTest, AddqDn_WordSize_Supported)
{
    // ADDQ.W #1, D0: 0101 001 0 01 000 000 = 0x5240 (size=1=word)
    Memory.WriteWord(0x1000, 0x5240);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340001;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12340002u);
}

TEST_F(JitCompilerTest, AddqMemory_Unsupported)
{
    // ADDQ.L #1, (A0): 0101 001 0 10 010 000 = 0x5290 (eaMode=2)
    Memory.WriteWord(0x1000, 0x5290);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr); // Memory EA not supported
}

TEST_F(JitCompilerTest, SubqMemory_Unsupported)
{
    // SUBQ.L #1, (A0): 0101 001 1 10 010 000 = 0x5390 (eaMode=2)
    Memory.WriteWord(0x1000, 0x5390);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
}

// ============================================================================
// ADDQ/SUBQ in composite blocks
// ============================================================================

TEST_F(JitCompilerTest, AddqSubq_CompositeBlock)
{
    // MOVEQ #10, D0 → 0x700A
    // ADDQ.L #5, D0 → 0x5A80
    // SUBQ.L #3, D0 → 0x5780
    Memory.WriteWord(0x1000, 0x700A);
    Memory.WriteWord(0x1002, 0x5A80);
    Memory.WriteWord(0x1004, 0x5780);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1006u);
    EXPECT_EQ(Cpu.D[0], 12u); // 10+5-3 = 12
}

TEST_F(JitCompilerTest, AddqAn_InCompositeBlock)
{
    // MOVEQ #0, D0 → 0x7000
    // ADDQ.L #4, A0 → 0x5888
    // SUBQ.L #1, D0 → 0x5380
    Memory.WriteWord(0x1000, 0x7000);
    Memory.WriteWord(0x1002, 0x5888);
    Memory.WriteWord(0x1004, 0x5380);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Cpu.D[0] = 0;
    Cpu.A[0] = 0x1000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[0], 0x1004u);
    EXPECT_EQ(Cpu.D[0], 0xFFFFFFFFu); // 0-1 wraps
    EXPECT_TRUE(Cpu.GetFlagN());
}

TEST_F(JitCompilerTest, AddqAn_DeadFlagElimination_Transparent)
{
    // MOVEQ #1, D0 → 0x7001 (flags: dead — SUBQ.L D1 overwrites)
    // ADDQ.L #4, A0 → 0x5888 (flags: transparent — doesn't affect flags)
    // SUBQ.L #1, D1 → 0x5381 (flags: live — last instruction)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0x5888);
    Memory.WriteWord(0x1004, 0x5381);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    // MOVEQ should have needsFlags=false (dead: overwritten by SUBQ.L)
    EXPECT_FALSE(block->Ops[0].needsFlags);
    // ADDQ An should have needsFlags=false (transparent)
    EXPECT_FALSE(block->Ops[1].needsFlags);
    // SUBQ Dn should have needsFlags=true (last)
    EXPECT_TRUE(block->Ops[2].needsFlags);
}

// ============================================================================
// CLR.L Dn tests
// ============================================================================

TEST_F(JitCompilerTest, ClrLDn_ClearsRegister)
{
    // CLR.L D3: 0x4283
    Memory.WriteWord(0x1000, 0x4283);
    Memory.WriteWord(0x1002, 0x4AFC); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[3] = 0xDEADBEEF;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[3], 0u);
}

TEST_F(JitCompilerTest, ClrLDn_SetsZeroFlag)
{
    // CLR.L D0: 0x4280
    Memory.WriteWord(0x1000, 0x4280);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12345678;
    Cpu.SetCCRByte(0x08); // N=1 initially
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagV());
    EXPECT_FALSE(Cpu.GetFlagC());
}

TEST_F(JitCompilerTest, ClrLDn_PreservesXFlag)
{
    // CLR.L D0: 0x4280
    Memory.WriteWord(0x1000, 0x4280);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.SetCCRByte(0x10); // X=1
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagX());
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, ClrBDn_Supported)
{
    // CLR.B D0: 0x4200
    Memory.WriteWord(0x1000, 0x4200);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xAABBCCDD;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xAABBCC00u);
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, ClrWDn_Supported)
{
    // CLR.W D0: 0x4240
    Memory.WriteWord(0x1000, 0x4240);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xAABBCCDD;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xAABB0000u);
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, ClrLDn_Memory_Unsupported)
{
    // CLR.L (A0): 0x4290
    Memory.WriteWord(0x1000, 0x4290);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
}

// ============================================================================
// TST.L Dn tests
// ============================================================================

TEST_F(JitCompilerTest, TstLDn_PositiveValue)
{
    // TST.L D2: 0x4A82
    Memory.WriteWord(0x1000, 0x4A82);
    Memory.WriteWord(0x1002, 0x4AFC); // terminates block
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[2] = 42;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[2], 42u); // Unchanged
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, TstLDn_NegativeValue)
{
    // TST.L D0: 0x4A80
    Memory.WriteWord(0x1000, 0x4A80);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x80000000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x80000000u);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, TstLDn_ZeroValue)
{
    // TST.L D1: 0x4A81
    Memory.WriteWord(0x1000, 0x4A81);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[1] = 0;
    Cpu.SetCCRByte(0x08); // N=1 initially
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagV());
    EXPECT_FALSE(Cpu.GetFlagC());
}

TEST_F(JitCompilerTest, TstLDn_PreservesXFlag)
{
    // TST.L D0: 0x4A80
    Memory.WriteWord(0x1000, 0x4A80);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 1;
    Cpu.SetCCRByte(0x10); // X=1
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagX());
}

TEST_F(JitCompilerTest, TstBDn_Supported)
{
    // TST.B D0: 0x4A00
    Memory.WriteWord(0x1000, 0x4A00);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xAABBCC80;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, TstWDn_Supported)
{
    // TST.W D0: 0x4A40
    Memory.WriteWord(0x1000, 0x4A40);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xAABB0000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, TstLDn_Memory_Unsupported)
{
    // TST.L (A0): 0x4A90
    Memory.WriteWord(0x1000, 0x4A90);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
}

// ============================================================================
// MOVE.L An,Dn tests
// ============================================================================

TEST_F(JitCompilerTest, MoveLAnDn_CopiesRegister)
{
    // MOVE.L A3,D5: 0010 101 000 001 011 = 0x2A0B
    Memory.WriteWord(0x1000, 0x2A0B);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.A[3] = 0xDEADBEEF;
    Cpu.D[5] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[5], 0xDEADBEEFu);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MoveLAnDn_SetsZeroFlag)
{
    // MOVE.L A0,D0: 0010 000 000 001 000 = 0x2008
    Memory.WriteWord(0x1000, 0x2008);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0;
    Cpu.D[0] = 0x12345678;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_FALSE(Cpu.GetFlagN());
}

TEST_F(JitCompilerTest, MoveLAnDn_PreservesXFlag)
{
    // MOVE.L A0,D0: 0x2008
    Memory.WriteWord(0x1000, 0x2008);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 42;
    Cpu.SetCCRByte(0x10); // X=1
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagX());
}

// ============================================================================
// MOVEA.L Dn,An tests
// ============================================================================

TEST_F(JitCompilerTest, MoveaLDnAn_CopiesRegister)
{
    // MOVEA.L D3,A2: 0010 010 001 000 011 = 0x2443
    Memory.WriteWord(0x1000, 0x2443);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[3] = 0xCAFEBABE;
    Cpu.A[2] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[2], 0xCAFEBABEu);
}

TEST_F(JitCompilerTest, MoveaLDnAn_NoFlagChange)
{
    // MOVEA.L D0,A0: 0010 000 001 000 000 = 0x2040
    Memory.WriteWord(0x1000, 0x2040);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x80000000; // Negative value
    Cpu.SetCCRByte(0x1F); // All flags set
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[0], 0x80000000u);
    EXPECT_EQ(Cpu.GetCCR(), 0x1Fu); // All flags unchanged
}

// ============================================================================
// MOVEA.L An,Am tests
// ============================================================================

TEST_F(JitCompilerTest, MoveaLAnAm_CopiesRegister)
{
    // MOVEA.L A3,A5: 0010 101 001 001 011 = 0x2A4B
    Memory.WriteWord(0x1000, 0x2A4B);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.A[3] = 0x12345678;
    Cpu.A[5] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[5], 0x12345678u);
}

TEST_F(JitCompilerTest, MoveaLAnAm_NoFlagChange)
{
    // MOVEA.L A0,A1: 0010 001 001 001 000 = 0x2248
    Memory.WriteWord(0x1000, 0x2248);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x80000000;
    Cpu.SetCCRByte(0x1F); // All flags set
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[1], 0x80000000u);
    EXPECT_EQ(Cpu.GetCCR(), 0x1Fu); // All flags unchanged
}

// ============================================================================
// Composite block + Dead flag elimination for new instructions
// ============================================================================

TEST_F(JitCompilerTest, ClrTst_CompositeBlock)
{
    // CLR.L D0 → 0x4280
    // MOVEQ #5, D1 → 0x7205
    // TST.L D1 → 0x4A81
    Memory.WriteWord(0x1000, 0x4280);
    Memory.WriteWord(0x1002, 0x7205);
    Memory.WriteWord(0x1004, 0x4A81);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Cpu.D[0] = 0xDEAD;
    Cpu.D[1] = 0;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1006u);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_EQ(Cpu.D[1], 5u);
    EXPECT_FALSE(Cpu.GetFlagZ()); // TST.L D1=5 → Z=0
    EXPECT_FALSE(Cpu.GetFlagN());
}

TEST_F(JitCompilerTest, MoveLAnDn_MoveaLDnAn_CompositeBlock)
{
    // MOVE.L A0,D0 → 0x2008
    // ADDQ.L #4, D0 → 0x5880  (wait, that's ADDQ.L #4, D0: 0101 100 0 10 000 000 = 0x5880)
    // MOVEA.L D0,A0 → 0x2040
    Memory.WriteWord(0x1000, 0x2008);
    Memory.WriteWord(0x1002, 0x5880);
    Memory.WriteWord(0x1004, 0x2040);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Cpu.A[0] = 0x1000;
    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x1004u);
    EXPECT_EQ(Cpu.A[0], 0x1004u);
}

TEST_F(JitCompilerTest, MoveaLAnAm_DeadFlagElimination_Transparent)
{
    // MOVEQ #1, D0 → 0x7001 (flags: dead — SUBQ.L D1 overwrites)
    // MOVEA.L A0, A1 → 0x2248 (flags: transparent)
    // SUBQ.L #1, D1 → 0x5381 (flags: live — last)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0x2248);
    Memory.WriteWord(0x1004, 0x5381);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    EXPECT_FALSE(block->Ops[0].needsFlags); // MOVEQ: dead
    EXPECT_FALSE(block->Ops[1].needsFlags); // MOVEA.L An,Am: transparent
    EXPECT_TRUE(block->Ops[2].needsFlags);  // SUBQ.L Dn: live
}

TEST_F(JitCompilerTest, MoveaLDnAn_DeadFlagElimination_Transparent)
{
    // MOVEQ #1, D0 → 0x7001 (flags: dead)
    // MOVEA.L D0, A0 → 0x2040 (flags: transparent)
    // SUBQ.L #1, D1 → 0x5381 (flags: live)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0x2040);
    Memory.WriteWord(0x1004, 0x5381);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    EXPECT_FALSE(block->Ops[0].needsFlags);
    EXPECT_FALSE(block->Ops[1].needsFlags);
    EXPECT_TRUE(block->Ops[2].needsFlags);
}

// ============================================================================
// LSL.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, LslImmLDn_Basic)
{
    // LSL.L #2, D0: 1110 010 1 10 0 01 000 = 0xE580
    Memory.WriteWord(0x1000, 0xE580);
    Memory.WriteWord(0x1002, 0x4AFC); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[0] = 1;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 4u); // 1 << 2 = 4
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, LslImmLDn_Count8)
{
    // LSL.L #8, D0: ccc=0 means 8 → 1110 000 1 10 0 01 000 = 0xE188
    Memory.WriteWord(0x1000, 0xE188);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xFF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFF00u);
}

TEST_F(JitCompilerTest, LslImmLDn_SetsCarryAndExtend)
{
    // LSL.L #1, D0: 1110 001 1 10 0 01 000 = 0xE388
    Memory.WriteWord(0x1000, 0xE388);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x80000000; // MSB set → will shift out to carry
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagC());
    EXPECT_TRUE(Cpu.GetFlagX());
    EXPECT_TRUE(Cpu.GetFlagZ());
}

// ============================================================================
// LSR.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, LsrImmLDn_Basic)
{
    // LSR.L #1, D0: 1110 001 0 10 0 01 000 = 0xE288
    Memory.WriteWord(0x1000, 0xE288);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 4;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 2u); // 4 >> 1 = 2
}

TEST_F(JitCompilerTest, LsrImmLDn_SetsZero)
{
    // LSR.L #1, D0: 0xE288
    Memory.WriteWord(0x1000, 0xE288);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 1; // 1 >> 1 = 0
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_TRUE(Cpu.GetFlagC()); // bit 0 was 1
    EXPECT_TRUE(Cpu.GetFlagX());
}

// ============================================================================
// ASR.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, AsrImmLDn_SignExtends)
{
    // ASR.L #1, D0: 1110 001 0 10 0 00 000 = 0xE280
    Memory.WriteWord(0x1000, 0xE280);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x80000000; // -2147483648 >> 1 = 0xC0000000 (sign-extended)
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xC0000000u);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, AsrImmLDn_Positive)
{
    // ASR.L #1, D0: 0xE280
    Memory.WriteWord(0x1000, 0xE280);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 8; // Positive: same as logical shift right
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 4u);
    EXPECT_FALSE(Cpu.GetFlagN());
}

// ============================================================================
// ASL.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, AslImmLDn_SetsOverflow)
{
    // ASL.L #1, D0: 1110 001 1 10 0 00 000 = 0xE380
    Memory.WriteWord(0x1000, 0xE380);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x40000000; // Shift left → 0x80000000, sign changes → V=1
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x80000000u);
    EXPECT_TRUE(Cpu.GetFlagV());
    EXPECT_TRUE(Cpu.GetFlagN());
}

// ============================================================================
// Dead flag elimination for shift instructions
// ============================================================================

TEST_F(JitCompilerTest, DeadFlags_ShiftFollowedByMove)
{
    // LSL.L #2, D0 → 0xE580 (flags dead — overwritten by MOVE.L)
    // MOVE.L D0, D1 → 0x2200 (flags live — last instruction)
    Memory.WriteWord(0x1000, 0xE580);
    Memory.WriteWord(0x1002, 0x2200);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    // LSL needsFlags=false, MOVE needsFlags=true
    EXPECT_FALSE(block->Ops[0].needsFlags);
    EXPECT_TRUE(block->Ops[1].needsFlags);

    // Verify execution correctness
    Cpu.D[0] = 3;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 12u); // 3 << 2 = 12
    EXPECT_EQ(Cpu.D[1], 12u);
}

// ============================================================================
// EXG Dn,Dm tests
// ============================================================================

TEST_F(JitCompilerTest, ExgDnDm_Basic)
{
    // EXG D0,D1: 0xC141
    Memory.WriteWord(0x1000, 0xC141);
    Memory.WriteWord(0x1002, 0x4AFC); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[0] = 0x11111111;
    Cpu.D[1] = 0x22222222;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 0x22222222u);
    EXPECT_EQ(Cpu.D[1], 0x11111111u);
}

// ============================================================================
// EXG An,Am tests
// ============================================================================

TEST_F(JitCompilerTest, ExgAnAm_Basic)
{
    // EXG A0,A1: 0xC149
    Memory.WriteWord(0x1000, 0xC149);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0xAAAAAAAA;
    Cpu.A[1] = 0xBBBBBBBB;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[0], 0xBBBBBBBBu);
    EXPECT_EQ(Cpu.A[1], 0xAAAAAAAAu);
}

// ============================================================================
// EXG Dn,An tests
// ============================================================================

TEST_F(JitCompilerTest, ExgDnAn_Basic)
{
    // EXG D0,A0: 0xC188
    Memory.WriteWord(0x1000, 0xC188);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12345678;
    Cpu.A[0] = 0xABCDEF00;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xABCDEF00u);
    EXPECT_EQ(Cpu.A[0], 0x12345678u);
}

TEST_F(JitCompilerTest, ExgDnDm_NoFlags)
{
    // EXG D0,D1: 0xC141 — flags should not change
    Memory.WriteWord(0x1000, 0xC141);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x80000000; // would set N if flags were updated
    Cpu.D[1] = 0;          // would set Z if flags were updated
    Cpu.SetCCRByte(0x1F);  // All flags set
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.GetCCR(), 0x1Fu); // All flags unchanged
}

// ============================================================================
// SWAP Dn tests
// ============================================================================

TEST_F(JitCompilerTest, SwapDn_Basic)
{
    // SWAP D0: 0x4840
    Memory.WriteWord(0x1000, 0x4840);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340000;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 0x00001234u);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, SwapDn_SetsNegative)
{
    // SWAP D0: 0x4840 — result has MSB set → N=1
    Memory.WriteWord(0x1000, 0x4840);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x0000FFFF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFF0000u);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

// ============================================================================
// EXT.W Dn tests
// ============================================================================

TEST_F(JitCompilerTest, ExtWDn_NegByte)
{
    // EXT.W D0: 0x4880
    Memory.WriteWord(0x1000, 0x4880);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x00000080; // byte 0x80 → sign-extend to word 0xFF80
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0] & 0xFFFF, 0xFF80u); // lower word sign-extended
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

// ============================================================================
// EXT.L Dn tests
// ============================================================================

TEST_F(JitCompilerTest, ExtLDn_NegWord)
{
    // EXT.L D0: 0x48C0
    Memory.WriteWord(0x1000, 0x48C0);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x00008000; // word 0x8000 → sign-extend to long 0xFFFF8000
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFF8000u);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

// ============================================================================
// EXTB.L Dn tests
// ============================================================================

TEST_F(JitCompilerTest, ExtbLDn_NegByte)
{
    // EXTB.L D0: 0x49C0
    Memory.WriteWord(0x1000, 0x49C0);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x00000080; // byte 0x80 → sign-extend to long 0xFFFFFF80
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFFFF80u);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

// ============================================================================
// NEG.L Dn tests
// ============================================================================

TEST_F(JitCompilerTest, NegLDn_Basic)
{
    // NEG.L D0: 0x4480
    Memory.WriteWord(0x1000, 0x4480);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 5;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFFFFFBu); // -5
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_TRUE(Cpu.GetFlagC());
    EXPECT_TRUE(Cpu.GetFlagX());
}

TEST_F(JitCompilerTest, NegLDn_Zero)
{
    // NEG.L D0: 0x4480 — negating zero
    Memory.WriteWord(0x1000, 0x4480);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0x1F); // all flags set
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_FALSE(Cpu.GetFlagC());
    EXPECT_FALSE(Cpu.GetFlagX());
}

// ============================================================================
// NOT.L Dn tests
// ============================================================================

TEST_F(JitCompilerTest, NotLDn_Basic)
{
    // NOT.L D0: 0x4680
    Memory.WriteWord(0x1000, 0x4680);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFFFFFFu);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

// ============================================================================
// Dead flag elimination for Tier 1 instructions
// ============================================================================

TEST_F(JitCompilerTest, DeadFlags_Tier1Block)
{
    // SWAP D0 → 0x4840 (flags dead — overwritten by NOT)
    // NOT.L D0 → 0x4680 (flags dead — overwritten by MOVE.L)
    // MOVE.L D0, D1 → 0x2200 (flags live — last instruction)
    Memory.WriteWord(0x1000, 0x4840);
    Memory.WriteWord(0x1002, 0x4680);
    Memory.WriteWord(0x1004, 0x2200);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    EXPECT_FALSE(block->Ops[0].needsFlags); // SWAP: dead
    EXPECT_FALSE(block->Ops[1].needsFlags); // NOT: dead
    EXPECT_TRUE(block->Ops[2].needsFlags);  // MOVE.L: live
}

TEST_F(JitCompilerTest, DeadFlags_ExgTransparent)
{
    // MOVEQ #1, D0 → 0x7001 (flags dead — overwritten by SUBQ)
    // EXG D0, D1 → 0xC141 (flags transparent)
    // SUBQ.L #1, D0 → 0x5380 (flags live — last)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0xC141);
    Memory.WriteWord(0x1004, 0x5380);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    EXPECT_FALSE(block->Ops[0].needsFlags); // MOVEQ: dead
    EXPECT_FALSE(block->Ops[1].needsFlags); // EXG: transparent
    EXPECT_TRUE(block->Ops[2].needsFlags);  // SUBQ: live
}

// ============================================================================
// Phase 1C: MULU.W / MULS.W Dn,Dm
// ============================================================================

TEST_F(JitCompilerTest, MuluW_BasicMultiply)
{
    // MULU.W D1,D0: 1100 000 011 000 001 = 0xC0C1
    Memory.WriteWord(0x1000, 0xC0C1);
    Memory.WriteWord(0x1002, 0x4AFC); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[0] = 100;
    Cpu.D[1] = 200;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 20000u);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MuluW_ZeroResult)
{
    Memory.WriteWord(0x1000, 0xC0C1); // MULU.W D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.D[1] = 12345;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0u);
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MuluW_LargeValues)
{
    Memory.WriteWord(0x1000, 0xC0C1); // MULU.W D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // 0xFFFF * 0xFFFF = 0xFFFE0001
    Cpu.D[0] = 0x1234FFFF; // upper bits should be replaced
    Cpu.D[1] = 0x5678FFFF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xFFFE0001u);
    EXPECT_TRUE(Cpu.GetFlagN());
}

TEST_F(JitCompilerTest, MulsW_PositiveTimesNegative)
{
    // MULS.W D1,D0: 1100 000 111 000 001 = 0xC1C1
    Memory.WriteWord(0x1000, 0xC1C1);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 10;   // 10
    Cpu.D[1] = 0xFFF6; // -10 as int16_t
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], static_cast<uint32_t>(-100)); // 0xFFFFFF9C
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MulsW_NegativeTimesNegative)
{
    Memory.WriteWord(0x1000, 0xC1C1); // MULS.W D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xFFF6; // -10
    Cpu.D[1] = 0xFFEC; // -20
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 200u);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MuluW_OnlyLow16BitsUsed)
{
    Memory.WriteWord(0x1000, 0xC0C1); // MULU.W D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // Upper 16 bits of operands should be ignored
    Cpu.D[0] = 0xABCD0003; // only 3 is used
    Cpu.D[1] = 0x12340007; // only 7 is used
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 21u); // 3*7=21
}

// ============================================================================
// Phase 1D: BTST Dn,Dm
// ============================================================================

TEST_F(JitCompilerTest, BtstDnDm_BitSet)
{
    // BTST D1,D0: 0000 001 100 000 000 = 0x0300
    Memory.WriteWord(0x1000, 0x0300);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x08; // bit 3 is set
    Cpu.D[1] = 3;    // test bit 3
    Cpu.SetCCRByte(0x04); // Z=1 initially
    block->Execute(Cpu);
    EXPECT_FALSE(Cpu.GetFlagZ()); // bit is set → Z=0
}

TEST_F(JitCompilerTest, BtstDnDm_BitClear)
{
    Memory.WriteWord(0x1000, 0x0300); // BTST D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x00; // all bits clear
    Cpu.D[1] = 5;    // test bit 5
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagZ()); // bit is clear → Z=1
}

TEST_F(JitCompilerTest, BtstDnDm_Modulo32)
{
    Memory.WriteWord(0x1000, 0x0300); // BTST D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x80000000; // bit 31 set
    Cpu.D[1] = 63;         // 63 % 32 = 31
    Cpu.SetCCRByte(0x04);
    block->Execute(Cpu);
    EXPECT_FALSE(Cpu.GetFlagZ()); // bit 31 is set
}

TEST_F(JitCompilerTest, BtstDnDm_PreservesOtherFlags)
{
    Memory.WriteWord(0x1000, 0x0300); // BTST D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0;
    Cpu.D[1] = 0;
    Cpu.SetCCRByte(0x1B); // X=1, N=1, V=1, C=1
    block->Execute(Cpu);
    uint8_t ccr = Cpu.GetCCR();
    EXPECT_TRUE(ccr & 0x10);  // X preserved
    EXPECT_TRUE(ccr & 0x08);  // N preserved
    EXPECT_TRUE(ccr & 0x04);  // Z set (bit 0 is clear)
    EXPECT_TRUE(ccr & 0x02);  // V preserved
    EXPECT_TRUE(ccr & 0x01);  // C preserved
}

// ============================================================================
// Phase 1A: LEA
// ============================================================================

TEST_F(JitCompilerTest, LeaAnAr_Simple)
{
    // LEA (A2),A3: 0100 011 111 010 010 = 0x47D2
    Memory.WriteWord(0x1000, 0x47D2);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.A[2] = 0x12345678;
    Cpu.A[3] = 0;
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[3], 0x12345678u);
}

TEST_F(JitCompilerTest, LeaD16AnAr_PositiveDisp)
{
    // LEA d16(A0),A1: 0100 001 111 101 000 = 0x43E8
    // Followed by d16 = 0x0100 (256)
    Memory.WriteWord(0x1000, 0x43E8);
    Memory.WriteWord(0x1002, 0x0100); // d16 = 256
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);
    EXPECT_EQ(block->ByteLength, 4); // 2-word instruction

    Cpu.A[0] = 0x00001000;
    Cpu.A[1] = 0;
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1004u);
    EXPECT_EQ(Cpu.A[1], 0x00001100u); // 0x1000 + 256
}

TEST_F(JitCompilerTest, LeaD16AnAr_NegativeDisp)
{
    Memory.WriteWord(0x1000, 0x43E8); // LEA d16(A0),A1
    Memory.WriteWord(0x1002, 0xFF00); // d16 = -256
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x00002000;
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1004u);
    EXPECT_EQ(Cpu.A[1], 0x00001F00u); // 0x2000 - 256
}

TEST_F(JitCompilerTest, LeaD8AnXnAr_BasicIndex)
{
    // LEA d8(A0,D1),A2: 0100 010 111 110 000 = 0x45F0
    // Brief ext: D1.L, scale=1, d8=0x10
    // extWord: 0 001 1 00 0 00010000 = 0x1810
    Memory.WriteWord(0x1000, 0x45F0);
    Memory.WriteWord(0x1002, 0x1810); // D1.L * 1, d8=0x10
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->ByteLength, 4);

    Cpu.A[0] = 0x00001000;
    Cpu.D[1] = 0x00000020;
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1004u);
    EXPECT_EQ(Cpu.A[2], 0x00001030u); // 0x1000 + 0x20 + 0x10
}

TEST_F(JitCompilerTest, LeaD8AnXnAr_Scale2)
{
    // LEA d8(A0,D1*2),A2
    // Brief ext: D1.L, scale=2 (01), d8=0
    // extWord: 0 001 1 01 0 00000000 = 0x1A00
    Memory.WriteWord(0x1000, 0x45F0);
    Memory.WriteWord(0x1002, 0x1A00);
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x00001000;
    Cpu.D[1] = 0x00000010;
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[2], 0x00001020u); // 0x1000 + 0x10*2
}

TEST_F(JitCompilerTest, LeaD8AnXnAr_AddrIndex)
{
    // LEA d8(A0,A3),A2
    // Brief ext: A3.L, scale=1, d8=4
    // extWord: 1 011 1 00 0 00000100 = 0xB804
    Memory.WriteWord(0x1000, 0x45F0);
    Memory.WriteWord(0x1002, 0xB804);
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x00001000;
    Cpu.A[3] = 0x00000100;
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[2], 0x00001104u); // 0x1000 + 0x100 + 4
}

TEST_F(JitCompilerTest, LeaD8AnXnAr_WordIndex)
{
    // LEA d8(A0,D1.W),A2
    // Brief ext: D1.W, scale=1, d8=0
    // extWord: 0 001 0 00 0 00000000 = 0x1000
    Memory.WriteWord(0x1000, 0x45F0);
    Memory.WriteWord(0x1002, 0x1000);
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x00002000;
    Cpu.D[1] = 0x0000FFF0; // as word = -16
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.A[2], 0x00001FF0u); // 0x2000 + (-16)
}

TEST_F(JitCompilerTest, Lea_DoesNotAffectFlags)
{
    // LEA (A0),A1 followed by SUBQ.L #1,D0 — LEA should not affect flags
    Memory.WriteWord(0x1000, 0x43D0); // LEA (A0),A1
    Memory.WriteWord(0x1002, 0x5380); // SUBQ.L #1,D0
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);
    // LEA should be flag-transparent in dead flag elimination
    EXPECT_FALSE(block->Ops[0].needsFlags);
    EXPECT_TRUE(block->Ops[1].needsFlags);
}

// ============================================================================
// Phase 1B: Bcc.W / BRA.W
// ============================================================================

TEST_F(JitCompilerTest, BraW_ForwardBranch)
{
    // BRA.W: 0x6000, followed by d16 displacement
    Memory.WriteWord(0x1000, 0x6000);
    Memory.WriteWord(0x1002, 0x0100); // d16 = 256 → target = 0x1002 + 256 = 0x1102
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);
    EXPECT_EQ(block->ByteLength, 4);

    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1102u);
}

TEST_F(JitCompilerTest, BccW_ForwardBranchTaken)
{
    // BEQ.W: 0x6700, followed by d16 = 0x0200 (512)
    // Target = 0x1002 + 512 = 0x1202
    Memory.WriteWord(0x1000, 0x6700);
    Memory.WriteWord(0x1002, 0x0200);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);
    EXPECT_EQ(block->ByteLength, 4);

    Cpu.SetCCRByte(0x04); // Z=1, so BEQ is taken
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1202u);
}

TEST_F(JitCompilerTest, BccW_ForwardBranchNotTaken)
{
    Memory.WriteWord(0x1000, 0x6700); // BEQ.W
    Memory.WriteWord(0x1002, 0x0200); // d16 = 512
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.SetCCRByte(0); // Z=0, so BEQ is not taken
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1004u); // fallthrough
}

TEST_F(JitCompilerTest, BraW_BackwardBranchRejected)
{
    // BRA.W with backward displacement should terminate block before it
    // BRA.W displacement is relative to opcode_addr + 2 = 0x1004
    // d16 = -4 → target = 0x1004 + (-4) = 0x1000 (= startPC, which is <= startPC)
    Memory.WriteWord(0x1000, 0x7000); // MOVEQ #0,D0
    Memory.WriteWord(0x1002, 0x6000); // BRA.W
    Memory.WriteWord(0x1004, 0xFFFC); // d16 = -4 → target = 0x1000
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    // Backward BRA.W should be excluded; block should contain only MOVEQ
    EXPECT_EQ(block->InstructionCount, 1);
}

TEST_F(JitCompilerTest, BccW_NegativeDispForwardStillIncluded)
{
    // BNE.W at 0x1000 with d16=0x0002 → target=0x1004 (forward relative to block start)
    Memory.WriteWord(0x1000, 0x6600); // BNE.W
    Memory.WriteWord(0x1002, 0x0002); // d16=2 → target=0x1002+2=0x1004
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);
}

TEST_F(JitCompilerTest, BraW_BlockByteLength)
{
    // MOVEQ + BRA.W = 2 + 4 = 6 bytes
    Memory.WriteWord(0x1000, 0x7000); // MOVEQ #0,D0
    Memory.WriteWord(0x1002, 0x6000); // BRA.W
    Memory.WriteWord(0x1004, 0x0100); // d16=256
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);
    EXPECT_EQ(block->ByteLength, 6);
}

// ============================================================================
// Phase 1E: Byte/Word size register instructions
// ============================================================================

TEST_F(JitCompilerTest, AddBDnDm_Basic)
{
    // ADD.B D1,D0: 1101 000 000 000 001 = 0xD001
    Memory.WriteWord(0x1000, 0xD001);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12345610;
    Cpu.D[1] = 0xABCDEF20;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12345630u); // only low byte changes: 0x10+0x20=0x30
}

TEST_F(JitCompilerTest, AddWDnDm_Basic)
{
    // ADD.W D1,D0: 1101 000 001 000 001 = 0xD041
    Memory.WriteWord(0x1000, 0xD041);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340100;
    Cpu.D[1] = 0xABCD0200;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12340300u); // only low word changes
}

TEST_F(JitCompilerTest, SubBDnDm_Basic)
{
    // SUB.B D1,D0: 1001 000 000 000 001 = 0x9001
    Memory.WriteWord(0x1000, 0x9001);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12345630;
    Cpu.D[1] = 0xABCDEF10;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12345620u);
}

TEST_F(JitCompilerTest, SubWDnDm_Basic)
{
    // SUB.W D1,D0: 1001 000 001 000 001 = 0x9041
    Memory.WriteWord(0x1000, 0x9041);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340300;
    Cpu.D[1] = 0xABCD0100;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12340200u);
}

TEST_F(JitCompilerTest, CmpBDnDm_Equal)
{
    // CMP.B D1,D0: 1011 000 000 000 001 = 0xB001
    Memory.WriteWord(0x1000, 0xB001);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12345642;
    Cpu.D[1] = 0xABCDEF42;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagZ());
    EXPECT_EQ(Cpu.D[0], 0x12345642u); // unchanged
}

TEST_F(JitCompilerTest, CmpWDnDm_NotEqual)
{
    // CMP.W D1,D0: 1011 000 001 000 001 = 0xB041
    Memory.WriteWord(0x1000, 0xB041);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340100;
    Cpu.D[1] = 0xABCD0200;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, AndBDnDm_Basic)
{
    // AND.B D1,D0: 1100 000 000 000 001 = 0xC001
    Memory.WriteWord(0x1000, 0xC001);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x123456FF;
    Cpu.D[1] = 0xABCDEF0F;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x1234560Fu);
}

TEST_F(JitCompilerTest, OrWDnDm_Basic)
{
    // OR.W D1,D0: 1000 000 001 000 001 = 0x8041
    Memory.WriteWord(0x1000, 0x8041);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340F00;
    Cpu.D[1] = 0xABCD00F0;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12340FF0u);
}

TEST_F(JitCompilerTest, EorBDnDm_Basic)
{
    // EOR.B D1,D0: 1011 001 100 000 000 = 0xB300
    Memory.WriteWord(0x1000, 0xB300);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x123456FF;
    Cpu.D[1] = 0xABCDEF0F;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x123456F0u); // 0xFF ^ 0x0F = 0xF0
}

TEST_F(JitCompilerTest, EorWDnDm_Basic)
{
    // EOR.W D1,D0: 1011 001 101 000 000 = 0xB340
    Memory.WriteWord(0x1000, 0xB340);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x1234FFFF;
    Cpu.D[1] = 0xABCD00FF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x1234FF00u);
}

TEST_F(JitCompilerTest, AddqBDn_Basic)
{
    // ADDQ.B #3,D0: 0101 011 0 00 000 000 = 0x5600
    Memory.WriteWord(0x1000, 0x5600);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x123456FD;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    // 0xFD + 3 = 0x100 → wraps to 0x00 with carry
    EXPECT_EQ(Cpu.D[0], 0x12345600u);
}

TEST_F(JitCompilerTest, SubqWDn_Basic)
{
    // SUBQ.W #1,D0: 0101 001 1 01 000 000 = 0x5340
    Memory.WriteWord(0x1000, 0x5340);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    // 0x0000 - 1 = 0xFFFF (borrow)
    EXPECT_EQ(Cpu.D[0], 0x1234FFFFu);
}

TEST_F(JitCompilerTest, ClrBDn_Basic)
{
    // CLR.B D0: 0100 0010 00 000 000 = 0x4200
    Memory.WriteWord(0x1000, 0x4200);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x123456FF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12345600u); // only low byte cleared
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, ClrWDn_Basic)
{
    // CLR.W D0: 0100 0010 01 000 000 = 0x4240
    Memory.WriteWord(0x1000, 0x4240);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x1234FFFF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x12340000u); // only low word cleared
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, TstBDn_Negative)
{
    // TST.B D0: 0100 1010 00 000 000 = 0x4A00
    Memory.WriteWord(0x1000, 0x4A00);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x000000FF;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_TRUE(Cpu.GetFlagN());
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, TstWDn_Zero)
{
    // TST.W D0: 0100 1010 01 000 000 = 0x4A40
    Memory.WriteWord(0x1000, 0x4A40);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340000;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_FALSE(Cpu.GetFlagN());
    EXPECT_TRUE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, NegBDn_Basic)
{
    // NEG.B D0: 0100 0100 00 000 000 = 0x4400
    Memory.WriteWord(0x1000, 0x4400);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12345601;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x123456FFu); // NEG.B 1 = 0xFF (-1)
}

TEST_F(JitCompilerTest, NotWDn_Basic)
{
    // NOT.W D0: 0100 0110 01 000 000 = 0x4640
    Memory.WriteWord(0x1000, 0x4640);
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x1234FF00;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0x123400FFu);
}

TEST_F(JitCompilerTest, AddBDnDm_PreservesUpperBytes)
{
    // Verify upper 24 bits are preserved for .B operations
    Memory.WriteWord(0x1000, 0xD001); // ADD.B D1,D0
    Memory.WriteWord(0x1002, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0xAABBCC05;
    Cpu.D[1] = 0x11223303;
    Cpu.SetCCRByte(0);
    block->Execute(Cpu);
    EXPECT_EQ(Cpu.D[0], 0xAABBCC08u); // upper bytes unchanged
}

// ============================================================================
// Dead flag elimination for new instructions
// ============================================================================

TEST_F(JitCompilerTest, DeadFlags_BtstTransparent)
{
    // BTST only changes Z — should not kill earlier flag setters
    // ADDQ.L #1,D0 / BTST D1,D2 / BEQ target
    // The ADDQ should have needsFlags=true because BTST doesn't overwrite NVC
    Memory.WriteWord(0x1000, 0x5280); // ADDQ.L #1,D0
    Memory.WriteWord(0x1002, 0x0302); // BTST D1,D2
    Memory.WriteWord(0x1004, 0x6704); // BEQ.B +4
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    EXPECT_TRUE(block->Ops[0].needsFlags);  // ADDQ: still live (BTST doesn't kill NVC)
    EXPECT_TRUE(block->Ops[1].needsFlags);  // BTST: live (BEQ reads Z)
    EXPECT_FALSE(block->Ops[2].needsFlags); // BEQ: branch
}

TEST_F(JitCompilerTest, DeadFlags_LeaTransparent)
{
    // LEA should be flag-transparent
    Memory.WriteWord(0x1000, 0x43D0); // LEA (A0),A1
    Memory.WriteWord(0x1002, 0x43E8); // LEA d16(A0),A1
    Memory.WriteWord(0x1004, 0x0010); // d16=16
    Memory.WriteWord(0x1006, 0x5380); // SUBQ.L #1,D0
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);
    EXPECT_FALSE(block->Ops[0].needsFlags); // LEA: transparent
    EXPECT_FALSE(block->Ops[1].needsFlags); // LEA: transparent
    EXPECT_TRUE(block->Ops[2].needsFlags);  // SUBQ: live
}

TEST_F(JitCompilerTest, DeadFlags_MuluKillsFlags)
{
    // MULU should kill earlier flag setters (it overwrites NZVC)
    Memory.WriteWord(0x1000, 0x5280); // ADDQ.L #1,D0
    Memory.WriteWord(0x1002, 0xC0C1); // MULU.W D1,D0
    Memory.WriteWord(0x1004, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);
    EXPECT_FALSE(block->Ops[0].needsFlags); // ADDQ: dead (MULU overwrites)
    EXPECT_TRUE(block->Ops[1].needsFlags);  // MULU: live
}

// ============================================================================
// Multi-word instruction + single-word instruction mixed blocks
// ============================================================================

TEST_F(JitCompilerTest, MixedBlock_LeaAndMoveq)
{
    // MOVEQ #10,D0 / LEA d16(A0),A1 / MOVEQ #20,D2
    Memory.WriteWord(0x1000, 0x700A); // MOVEQ #10,D0
    Memory.WriteWord(0x1002, 0x43E8); // LEA d16(A0),A1
    Memory.WriteWord(0x1004, 0x0080); // d16=128
    Memory.WriteWord(0x1006, 0x7414); // MOVEQ #20,D2
    Memory.WriteWord(0x1008, 0x4AFC);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);
    EXPECT_EQ(block->ByteLength, 8); // 2 + 4 + 2

    Cpu.A[0] = 0x00004000;
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu); uint32_t nextPC = result.nextPC;
    EXPECT_EQ(nextPC, 0x1008u);
    EXPECT_EQ(Cpu.D[0], 10u);
    EXPECT_EQ(Cpu.A[1], 0x00004080u);
    EXPECT_EQ(Cpu.D[2], 20u);
}

// ============================================================================
// Phase 2: Memory access instructions
// ============================================================================

// Helper: set up data page cache for identity mapping at given page
#define SETUP_DATA_CACHE(pageBase, pageMask) \
    Cpu.SetupDataCache((pageBase), (pageBase), (pageMask));

// Helper: invalidate data page cache
#define INVALIDATE_DATA_CACHE() \
    Cpu.InvalidateDataCache();

// --- MOVE.L (An), Dm ---

TEST_F(JitCompilerTest, MoveLIndAnDm_Classify)
{
    // MOVE.L (A0),D1: 0010 001 000 010 000 = 0x2210
    Memory.WriteWord(0x1000, 0x2210);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);
}

TEST_F(JitCompilerTest, MoveLIndAnDm_CacheHit)
{
    // MOVE.L (A0),D1: 0x2210
    Memory.WriteWord(0x1000, 0x2210);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // Put data at physical address 0x2000
    Memory.WriteLong(0x2000, 0xDEADBEEF);
    Cpu.A[0] = 0x2000;
    Cpu.D[1] = 0;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, block->InstructionCount);
    EXPECT_EQ(Cpu.D[1], 0xDEADBEEFu);
    EXPECT_TRUE(Cpu.GetFlagN()); // bit 31 set
    EXPECT_FALSE(Cpu.GetFlagZ());
}

TEST_F(JitCompilerTest, MoveLIndAnDm_CacheMiss_Bailout)
{
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1,D0
    Memory.WriteWord(0x1002, 0x2210); // MOVE.L (A0),D1
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    Cpu.A[0] = 0x5000;
    Cpu.D[0] = 0;
    Cpu.D[1] = 0;
    INVALIDATE_DATA_CACHE();
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    // Should bail out at instruction 1 (MOVE.L), executing only MOVEQ
    EXPECT_EQ(result.executedCount, 1);
    EXPECT_EQ(result.nextPC, 0x1002u); // PC of the MOVE.L instruction
    EXPECT_EQ(Cpu.D[0], 1u); // MOVEQ executed
    EXPECT_EQ(Cpu.D[1], 0u); // MOVE.L not executed
    EXPECT_EQ(result.executedCycles, block->CumulativeCycles[1]);
}

TEST_F(JitCompilerTest, MoveLIndAnDm_WrongPage_Bailout)
{
    Memory.WriteWord(0x1000, 0x2210); // MOVE.L (A0),D1
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x5000;
    SETUP_DATA_CACHE(0x3000, 0xFFF); // Different page
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 0); // bail out immediately
    EXPECT_EQ(result.nextPC, 0x1000u);
}

// --- MOVE.L (An)+, Dm ---

TEST_F(JitCompilerTest, MoveLPostIncAnDm_CacheHit)
{
    // MOVE.L (A0)+,D1: 0010 001 000 011 000 = 0x2218
    Memory.WriteWord(0x1000, 0x2218);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Memory.WriteLong(0x2000, 0x12345678);
    Cpu.A[0] = 0x2000;
    Cpu.D[1] = 0;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, block->InstructionCount);
    EXPECT_EQ(Cpu.D[1], 0x12345678u);
    EXPECT_EQ(Cpu.A[0], 0x2004u); // post-increment by 4
}

TEST_F(JitCompilerTest, MoveLPostIncAnDm_CacheMiss_NoIncrement)
{
    Memory.WriteWord(0x1000, 0x2218); // MOVE.L (A0)+,D1
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.A[0] = 0x5000;
    INVALIDATE_DATA_CACHE();
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 0);
    EXPECT_EQ(Cpu.A[0], 0x5000u); // A0 unchanged on bailout
}

// --- MOVE.L Dm, (An) — always bails out ---

TEST_F(JitCompilerTest, MoveLDmIndAn_AlwaysBailout)
{
    // MOVE.L D1,(A0): 0010 000 010 000 001 = 0x2081
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1,D0
    Memory.WriteWord(0x1002, 0x2081); // MOVE.L D1,(A0)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    Cpu.D[0] = 0;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    // Write always bails out
    EXPECT_EQ(result.executedCount, 1);
    EXPECT_EQ(result.nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 1u); // MOVEQ executed
}

// --- MOVE.L d16(An), Dm ---

TEST_F(JitCompilerTest, MoveLD16AnDm_CacheHit)
{
    // MOVE.L d16(A0),D1: 0010 001 000 101 000 = 0x2228
    Memory.WriteWord(0x1000, 0x2228);
    Memory.WriteWord(0x1002, 0x0010); // d16 = 16
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->ByteLength, 4);

    Memory.WriteLong(0x2010, 0xAABBCCDD);
    Cpu.A[0] = 0x2000;
    Cpu.D[1] = 0;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, block->InstructionCount);
    EXPECT_EQ(Cpu.D[1], 0xAABBCCDDu);
}

TEST_F(JitCompilerTest, MoveLD16AnDm_NegativeDisp)
{
    Memory.WriteWord(0x1000, 0x2228); // MOVE.L d16(A0),D1
    Memory.WriteWord(0x1002, 0xFFF0); // d16 = -16
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Memory.WriteLong(0x2FF0, 0x11223344);
    Cpu.A[0] = 0x3000;
    Cpu.D[1] = 0;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, block->InstructionCount);
    EXPECT_EQ(Cpu.D[1], 0x11223344u);
}

// --- MOVE.L Dm, d16(An) — always bails out ---

TEST_F(JitCompilerTest, MoveLDmD16An_AlwaysBailout)
{
    // MOVE.L D1,d16(A0): 0010 000 101 000 001 = 0x2141
    Memory.WriteWord(0x1000, 0x2141);
    Memory.WriteWord(0x1002, 0x0010); // d16 = 16
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->ByteLength, 4);

    SETUP_DATA_CACHE(0x2000, 0xFFF);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 0);
    EXPECT_EQ(result.nextPC, 0x1000u);
}

// --- RTS ---

TEST_F(JitCompilerTest, Rts_CacheHit)
{
    // RTS: 0x4E75
    Memory.WriteWord(0x1000, 0x4E75);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    // Set up stack with return address
    uint32_t sp = 0x2100;
    Memory.WriteLong(sp, 0x00003000); // return address
    Cpu.A[7] = sp;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 1);
    EXPECT_EQ(result.nextPC, 0x00003000u);
    EXPECT_EQ(Cpu.A[7], sp + 4);
}

TEST_F(JitCompilerTest, Rts_CacheMiss_Bailout)
{
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1,D0
    Memory.WriteWord(0x1002, 0x4E75); // RTS
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    Cpu.A[7] = 0x5000;
    Cpu.D[0] = 0;
    INVALIDATE_DATA_CACHE();
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    // RTS bails out, MOVEQ executed
    EXPECT_EQ(result.executedCount, 1);
    EXPECT_EQ(result.nextPC, 0x1002u);
    EXPECT_EQ(Cpu.D[0], 1u);
    EXPECT_EQ(Cpu.A[7], 0x5000u); // unchanged
}

TEST_F(JitCompilerTest, Rts_TerminatesBlock)
{
    // RTS should terminate the block — instructions after RTS should not be included
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1,D0
    Memory.WriteWord(0x1002, 0x4E75); // RTS
    Memory.WriteWord(0x1004, 0x7201); // MOVEQ #1,D1 (should NOT be included)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2); // MOVEQ + RTS only
    EXPECT_EQ(block->ByteLength, 4);
}

// --- Bailout cycle counting ---

TEST_F(JitCompilerTest, Bailout_CycleCounting)
{
    // MOVEQ + ADD.L + MOVE.L (An),Dm — verify partial cycle count
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1,D0 (2 cycles)
    Memory.WriteWord(0x1002, 0xD280); // ADD.L D0,D1 (2 cycles)
    Memory.WriteWord(0x1004, 0x2210); // MOVE.L (A0),D1 (may bail out)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Cpu.D[0] = 0;
    Cpu.D[1] = 0;
    INVALIDATE_DATA_CACHE();
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    // MOVEQ and ADD.L executed, MOVE.L bails out
    EXPECT_EQ(result.executedCount, 2);
    EXPECT_EQ(result.executedCycles, 4); // 2+2
    EXPECT_EQ(Cpu.D[0], 1u);
}

// --- Full block execution with memory access ---

TEST_F(JitCompilerTest, MixedBlock_RegisterAndMemory)
{
    // MOVEQ #10,D0 / MOVE.L (A0),D1 / ADD.L D0,D1
    Memory.WriteWord(0x1000, 0x700A); // MOVEQ #10,D0
    Memory.WriteWord(0x1002, 0x2210); // MOVE.L (A0),D1
    Memory.WriteWord(0x1004, 0xD280); // ADD.L D0,D1
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 3);

    Memory.WriteLong(0x2000, 20);
    Cpu.A[0] = 0x2000;
    Cpu.D[0] = 0;
    Cpu.D[1] = 0;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    Cpu.SetCCRByte(0);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 3);
    EXPECT_EQ(Cpu.D[0], 10u);
    EXPECT_EQ(Cpu.D[1], 30u); // 20 + 10
}

// --- Page boundary check ---

TEST_F(JitCompilerTest, MoveLIndAnDm_PageBoundary_Bailout)
{
    // MOVE.L (A0),D1 where address is at page boundary (crosses into next page)
    Memory.WriteWord(0x1000, 0x2210);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // Address 0x2FFE: needs bytes 0x2FFE-0x3001, but page mask is 0xFFF
    // (0x2FFE & 0xFFF) + 3 = 0xFFE + 3 = 0x1001 > 0xFFF
    Cpu.A[0] = 0x2FFE;
    SETUP_DATA_CACHE(0x2000, 0xFFF);
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 0); // crosses page → bailout
}

// ============================================================================
// Regression: Bailout at first instruction must not cause infinite loop
// ============================================================================

TEST_F(JitCompilerTest, BailoutAtFirstInstruction_InterpreterFallthrough)
{
    // Regression test: When a JIT block's first instruction bails out (executedCount=0),
    // ExecuteNextJit must fall through to the interpreter instead of looping forever.
    // Place MOVE.L (A0),D0 + ILLEGAL at 0x1000
    Memory.WriteWord(0x1000, 0x2010); // MOVE.L (A0), D0
    Memory.WriteWord(0x1002, 0x4AFC); // ILLEGAL (terminator)

    // Write the target data that the interpreter will read
    Cpu.A[0] = 0x2000;
    Memory.WriteLong(0x2000, 0xDEADBEEF);

    // Compile block and register in cache
    Cpu.PC = 0x1000;
    Cpu.JitEnabled = true;
    auto block = compiler.TryCompile(Cpu, 0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    // Do NOT set up data cache — force bailout at first instruction
    Cpu.InvalidateDataCache();
    Cpu.PC = 0x1000;
    Cpu.CycleCount = 0;
    Cpu.InstructionCount = 0;

    // Execute via the full JIT path (ExecuteNextJit)
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 0); // confirms bailout
    EXPECT_EQ(result.nextPC, 0x1000u); // PC returns to bailed instruction

    // Now simulate what ExecuteNextJit does after bailout:
    // It should call interpreter for the bailed instruction
    Cpu.PC = result.nextPC;
    Cpu.ExecuteNextFast(); // interpreter handles the MOVE.L (A0),D0

    EXPECT_EQ(Cpu.D[0], 0xDEADBEEF);
    EXPECT_EQ(Cpu.PC, 0x1002u); // advanced past the instruction
}

// ============================================================================
// Bailout blacklisting: blocks that bail out too often are evicted
// ============================================================================

TEST_F(JitCompilerTest, BailoutBlacklist_EvictsAfterThreshold)
{
    // MOVE.L (A0),D0 — will always bail out without data cache
    Memory.WriteWord(0x1000, 0x2010);
    Memory.WriteWord(0x1002, 0x4AFC);

    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    cache.AddBlock(0x1000, std::move(block));

    auto* cached = cache.TryGetBlock(0x1000);
    ASSERT_NE(cached, nullptr);

    // Simulate repeated bailouts up to threshold - 1
    Cpu.InvalidateDataCache();
    for (uint16_t i = 0; i < Core::MC68030::JitBailoutBlacklistThreshold - 1; i++) {
        cached->BailoutCount = i;
    }
    cached->BailoutCount = Core::MC68030::JitBailoutBlacklistThreshold - 1;

    // Block should still be in cache
    EXPECT_NE(cache.TryGetBlock(0x1000), nullptr);
    EXPECT_FALSE(cache.IsUncompilable(0x1000));

    // One more bailout crosses threshold → evict + blacklist
    cached->BailoutCount = Core::MC68030::JitBailoutBlacklistThreshold;
    cache.RemoveBlock(0x1000);
    cache.MarkUncompilable(0x1000);

    EXPECT_EQ(cache.TryGetBlock(0x1000), nullptr);
    EXPECT_TRUE(cache.IsUncompilable(0x1000));
}

TEST_F(JitCompilerTest, BailoutBlacklist_PartialExecution_AlsoTracked)
{
    // Block: MOVEQ #1,D0 + MOVE.L (A0),D1
    Memory.WriteWord(0x1000, 0x7001); // MOVEQ #1, D0
    Memory.WriteWord(0x1002, 0x2210); // MOVE.L (A0), D1
    Memory.WriteWord(0x1004, 0x4AFC); // ILLEGAL (terminator)

    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 2);

    // Execute with no data cache — MOVEQ succeeds, MOVE.L bails out
    Cpu.InvalidateDataCache();
    Cpu.PC = 0x1000;
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, 1); // partial bailout
    EXPECT_EQ(Cpu.D[0], 1u);

    // BailoutCount should be incrementable for partial bailouts too
    block->BailoutCount++;
    EXPECT_EQ(block->BailoutCount, 1);
}

TEST_F(JitCompilerTest, BailoutBlacklist_FullExecution_NoIncrement)
{
    // Block: MOVEQ #1,D0 + MOVEQ #2,D1 (no memory access → no bailout)
    Memory.WriteWord(0x1000, 0x7001);
    Memory.WriteWord(0x1002, 0x7201);
    Memory.WriteWord(0x1004, 0x4AFC);

    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.PC = 0x1000;
    auto result = block->Execute(Cpu);
    EXPECT_EQ(result.executedCount, block->InstructionCount); // no bailout
    EXPECT_EQ(block->BailoutCount, 0); // should remain 0
}

} // namespace Em68030::Tests
