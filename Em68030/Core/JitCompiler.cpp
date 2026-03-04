#include "pch.h"

#include "JitCompiler.h"
#include "MC68030.h"
#include "Memory.h"
#include "Alu.h"

#include <cstring>

namespace Em68030::Core {

// ============================================================================
// CompiledBlock::Execute — switch-based dispatch over pre-decoded JitOp array
// ============================================================================

uint32_t CompiledBlock::Execute(MC68030& cpu) const
{
    uint8_t ccr = cpu.GetCCR();

    for (int i = 0; i < static_cast<int>(Ops.size()); i++)
    {
        const auto& op = Ops[i];
        switch (op.type)
        {
            case JitOpType::Moveq:
            {
                uint32_t val = static_cast<uint32_t>(op.immediate);
                cpu.D[op.dstReg] = val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::MoveLDnDm:
            {
                uint32_t val = cpu.D[op.srcReg];
                cpu.D[op.dstReg] = val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::AddLDnDm:
            {
                auto r = Alu::AddLong(cpu.D[op.dstReg], cpu.D[op.srcReg], ccr);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::SubLDnDm:
            {
                auto r = Alu::SubLong(cpu.D[op.dstReg], cpu.D[op.srcReg], ccr);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::CmpLDnDm:
            {
                // CMP updates NZVC but NOT X
                auto r = Alu::SubLong(cpu.D[op.dstReg], cpu.D[op.srcReg], ccr);
                ccr = static_cast<uint8_t>((ccr & 0x10) | (r.ccr & 0x0F));
                break;
            }

            case JitOpType::AndLDnDm:
            {
                uint32_t val = cpu.D[op.dstReg] & cpu.D[op.srcReg];
                cpu.D[op.dstReg] = val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::OrLDnDm:
            {
                uint32_t val = cpu.D[op.dstReg] | cpu.D[op.srcReg];
                cpu.D[op.dstReg] = val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::EorLDnDm:
            {
                uint32_t val = cpu.D[op.dstReg] ^ cpu.D[op.srcReg];
                cpu.D[op.dstReg] = val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::BccB:
            {
                // Write back CCR before evaluating condition
                cpu.SetCCRByte(ccr);
                if (cpu.EvaluateCondition(op.condition))
                    return op.branchTarget;
                return op.fallthroughPC;
            }

            case JitOpType::BraB:
            {
                cpu.SetCCRByte(ccr);
                return op.branchTarget;
            }

            case JitOpType::Nop:
                break;
        }
    }

    // Write back CCR at end of block
    cpu.SetCCRByte(ccr);
    return FallthroughPC;
}

// ============================================================================
// JitCache
// ============================================================================

CompiledBlock* JitCache::TryGetBlock(uint32_t physAddr)
{
    int idx = static_cast<int>((physAddr >> 1) & BlockCacheMask);
    auto* block = m_blockCache[idx];
    if (block && block->PhysicalAddress == physAddr)
        return block;
    return nullptr;
}

void JitCache::AddBlock(uint32_t physAddr, std::unique_ptr<CompiledBlock> block)
{
    int idx = static_cast<int>((physAddr >> 1) & BlockCacheMask);
    m_blockCache[idx] = block.get();
    m_blocks.push_back(std::move(block));
    m_blockCount++;
}

void JitCache::InvalidateAll()
{
    std::memset(m_blockCache, 0, sizeof(m_blockCache));
    std::memset(m_uncompilable, 0, sizeof(m_uncompilable));
    m_blocks.clear();
    m_blockCount = 0;
    // Preserve execution counts (same as C# version)
}

uint8_t JitCache::IncrementAndGetCount(uint32_t physAddr)
{
    int idx = static_cast<int>((physAddr >> 1) & CountCacheMask);
    if (m_counts[idx] < 255)
        m_counts[idx]++;
    return m_counts[idx];
}

bool JitCache::IsUncompilable(uint32_t physAddr) const
{
    int idx = static_cast<int>((physAddr >> 1) & CountCacheMask);
    return m_uncompilable[idx];
}

void JitCache::MarkUncompilable(uint32_t physAddr)
{
    int idx = static_cast<int>((physAddr >> 1) & CountCacheMask);
    m_uncompilable[idx] = true;
}

// ============================================================================
// JitCompiler::Classify — instruction classification (same as C# version)
// ============================================================================

JitCompiler::InsnKind JitCompiler::Classify(uint16_t opcode)
{
    // NOP: 0x4E71
    if (opcode == 0x4E71)
        return InsnKind::Nop;

    int group = (opcode >> 12) & 0xF;

    switch (group)
    {
        case 0x7: // MOVEQ: 0111 rrr 0 iiiiiiii
            if ((opcode & 0x0100) == 0)
                return InsnKind::Moveq;
            break;

        case 0x2: // MOVE.L — check Dn→Dm: dstMode=000, srcMode=000
        {
            int srcMode = (opcode >> 3) & 7;
            int dstMode = (opcode >> 6) & 7;
            if (srcMode == 0 && dstMode == 0)
                return InsnKind::MoveLDnDm;
            break;
        }

        case 0xD: // ADD: 1101 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (opMode == 2 && eaMode == 0)
                return InsnKind::AddLDnDm;
            break;
        }

        case 0x9: // SUB: 1001 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (opMode == 2 && eaMode == 0)
                return InsnKind::SubLDnDm;
            break;
        }

        case 0xB: // CMP/EOR: 1011 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (opMode == 2 && eaMode == 0)
                return InsnKind::CmpLDnDm;
            if (opMode == 6 && eaMode == 0)
                return InsnKind::EorLDnDm;
            break;
        }

        case 0xC: // AND: 1100 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (opMode == 2 && eaMode == 0)
                return InsnKind::AndLDnDm;
            break;
        }

        case 0x8: // OR: 1000 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (opMode == 2 && eaMode == 0)
                return InsnKind::OrLDnDm;
            break;
        }

        case 0x6: // Bcc / BRA / BSR
        {
            int cond = (opcode >> 8) & 0xF;
            int disp8 = opcode & 0xFF;
            // Only 8-bit displacement (not 0x00=16-bit ext, not 0xFF=32-bit ext)
            if (disp8 == 0x00 || disp8 == 0xFF)
                break;
            if (cond == 1) // BSR — not supported (pushes return address)
                break;
            if (cond == 0)
                return InsnKind::BranchAlways;
            return InsnKind::Branch;
        }
    }

    return InsnKind::Unsupported;
}

// ============================================================================
// JitCompiler::TryCompile — scan + dead flag elimination + JitOp construction
// ============================================================================

std::unique_ptr<CompiledBlock> JitCompiler::TryCompile(MC68030& cpu, uint32_t startPC, uint32_t startPhysAddr)
{
    // Phase 1: Scan forward to find the extent of the basic block
    std::vector<uint16_t> opcodes;
    uint32_t scanPA = startPhysAddr;

    for (int i = 0; i < MaxBlockLength; i++)
    {
        uint16_t opcode;
        try { opcode = cpu.GetMemory().ReadWord(scanPA); }
        catch (...) { break; }

        auto kind = Classify(opcode);
        if (kind == InsnKind::Unsupported)
            break;

        // Backward branches must NOT be included in JIT blocks.
        if (kind == InsnKind::Branch || kind == InsnKind::BranchAlways)
        {
            int disp8 = static_cast<int8_t>(opcode & 0xFF);
            uint32_t pcAfterFetch = startPC + static_cast<uint32_t>(opcodes.size() * 2) + 2;
            uint32_t target = static_cast<uint32_t>(static_cast<int32_t>(pcAfterFetch) + disp8);
            if (target <= startPC)
                break; // Backward branch — terminate block before it

            opcodes.push_back(opcode);
            scanPA += 2;
            break; // Forward branch — include and terminate
        }

        opcodes.push_back(opcode);
        scanPA += 2;
    }

    if (opcodes.empty())
        return nullptr;

    // Phase 2: Dead flag elimination
    std::vector<bool> needsFlags(opcodes.size(), false);
    needsFlags[opcodes.size() - 1] = true;
    bool flagsLive = true;
    for (int i = static_cast<int>(opcodes.size()) - 1; i >= 0; i--)
    {
        auto kind = Classify(opcodes[i]);
        if (kind == InsnKind::Branch || kind == InsnKind::BranchAlways)
        {
            needsFlags[i] = false;
            flagsLive = (kind == InsnKind::Branch); // Bcc reads flags
        }
        else if (kind == InsnKind::Nop)
        {
            needsFlags[i] = false;
        }
        else
        {
            needsFlags[i] = flagsLive;
            flagsLive = false;
        }
    }

    // Phase 3: Build JitOp array
    auto block = std::make_unique<CompiledBlock>();
    block->PhysicalAddress = startPhysAddr;
    block->InstructionCount = static_cast<int>(opcodes.size());
    block->ByteLength = static_cast<int>(opcodes.size()) * 2;
    block->Ops.reserve(opcodes.size());

    uint32_t pc = startPC;
    for (size_t i = 0; i < opcodes.size(); i++)
    {
        pc += 2; // PC advances past opcode
        uint16_t opcode = opcodes[i];
        auto kind = Classify(opcode);

        JitOp op{};
        op.needsFlags = needsFlags[i];

        switch (kind)
        {
            case InsnKind::Moveq:
            {
                op.type = JitOpType::Moveq;
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.immediate = static_cast<int8_t>(opcode & 0xFF); // sign-extend
                break;
            }

            case InsnKind::MoveLDnDm:
            {
                op.type = JitOpType::MoveLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::AddLDnDm:
            {
                op.type = JitOpType::AddLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::SubLDnDm:
            {
                op.type = JitOpType::SubLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::CmpLDnDm:
            {
                op.type = JitOpType::CmpLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::AndLDnDm:
            {
                op.type = JitOpType::AndLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::OrLDnDm:
            {
                op.type = JitOpType::OrLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::EorLDnDm:
            {
                // EOR.L Dn,EA: opMode=6, Dn is source, EA reg is dest
                op.type = JitOpType::EorLDnDm;
                op.srcReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::BranchAlways:
            {
                int disp8 = static_cast<int8_t>(opcode & 0xFF);
                op.type = JitOpType::BraB;
                op.branchTarget = static_cast<uint32_t>(static_cast<int32_t>(pc) + disp8);
                break;
            }

            case InsnKind::Branch:
            {
                int cond = (opcode >> 8) & 0xF;
                int disp8 = static_cast<int8_t>(opcode & 0xFF);
                op.type = JitOpType::BccB;
                op.condition = static_cast<uint8_t>(cond);
                op.branchTarget = static_cast<uint32_t>(static_cast<int32_t>(pc) + disp8);
                op.fallthroughPC = pc;
                break;
            }

            case InsnKind::Nop:
            {
                op.type = JitOpType::Nop;
                break;
            }

            default:
                break;
        }

        block->Ops.push_back(op);
    }

    block->FallthroughPC = pc;
    return block;
}

} // namespace Em68030::Core
