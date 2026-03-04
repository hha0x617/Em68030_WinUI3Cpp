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

// ============================================================================
// ADDQ.L #imm, Dn tests
// ============================================================================

TEST_F(JitCompilerTest, AddqLDn_BasicAdd)
{
    // ADDQ.L #3, D2: 0101 011 0 10 000 010 = 0x5682
    Memory.WriteWord(0x1000, 0x5682);
    Memory.WriteWord(0x1002, 0x4E75); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[2] = 100;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
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

TEST_F(JitCompilerTest, AddqDn_ByteSize_Unsupported)
{
    // ADDQ.B #1, D0: 0101 001 0 00 000 000 = 0x5200 (size=0=byte)
    Memory.WriteWord(0x1000, 0x5200);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr); // .B not supported
}

TEST_F(JitCompilerTest, AddqDn_WordSize_Unsupported)
{
    // ADDQ.W #1, D0: 0101 001 0 01 000 000 = 0x5240 (size=1=word)
    Memory.WriteWord(0x1000, 0x5240);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr); // .W not supported
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
    uint32_t nextPC = block->Execute(Cpu);
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
    Memory.WriteWord(0x1002, 0x4E75); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[3] = 0xDEADBEEF;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
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

TEST_F(JitCompilerTest, ClrLDn_ByteSize_Unsupported)
{
    // CLR.B D0: 0x4200
    Memory.WriteWord(0x1000, 0x4200);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
}

TEST_F(JitCompilerTest, ClrLDn_WordSize_Unsupported)
{
    // CLR.W D0: 0x4240
    Memory.WriteWord(0x1000, 0x4240);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
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
    Memory.WriteWord(0x1002, 0x4E75); // terminates block
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

TEST_F(JitCompilerTest, TstLDn_ByteSize_Unsupported)
{
    // TST.B D0: 0x4A00
    Memory.WriteWord(0x1000, 0x4A00);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
}

TEST_F(JitCompilerTest, TstLDn_WordSize_Unsupported)
{
    // TST.W D0: 0x4A40
    Memory.WriteWord(0x1000, 0x4A40);
    auto block = CompileAt(0x1000, 0x1000);
    EXPECT_EQ(block, nullptr);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    uint32_t nextPC = block->Execute(Cpu);
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
    Memory.WriteWord(0x1002, 0x4E75); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[0] = 1;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
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
    Memory.WriteWord(0x1002, 0x4E75); // RTS (terminates block)
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->InstructionCount, 1);

    Cpu.D[0] = 0x11111111;
    Cpu.D[1] = 0x22222222;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
    auto block = CompileAt(0x1000, 0x1000);
    ASSERT_NE(block, nullptr);

    Cpu.D[0] = 0x12340000;
    Cpu.SetCCRByte(0);
    uint32_t nextPC = block->Execute(Cpu);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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
    Memory.WriteWord(0x1002, 0x4E75);
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

} // namespace Em68030::Tests
