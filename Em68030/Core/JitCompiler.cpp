#include "pch.h"

#include "JitCompiler.h"
#include "MC68030.h"
#include "Memory.h"
#include "Alu.h"
#include "InstructionDecoder.h"

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

            case JitOpType::AddqLDn:
            {
                auto r = Alu::AddLong(cpu.D[op.dstReg], static_cast<uint32_t>(op.immediate), ccr);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::SubqLDn:
            {
                auto r = Alu::SubLong(cpu.D[op.dstReg], static_cast<uint32_t>(op.immediate), ccr);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::AddqAn:
            {
                cpu.A[op.dstReg] += static_cast<uint32_t>(op.immediate);
                break;
            }

            case JitOpType::SubqAn:
            {
                cpu.A[op.dstReg] -= static_cast<uint32_t>(op.immediate);
                break;
            }

            case JitOpType::ClrLDn:
            {
                cpu.D[op.dstReg] = 0;
                if (op.needsFlags)
                    ccr = static_cast<uint8_t>((ccr & 0x10) | 0x04);
                break;
            }

            case JitOpType::TstLDn:
            {
                uint32_t val = cpu.D[op.dstReg];
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::MoveLAnDn:
            {
                uint32_t val = cpu.A[op.srcReg];
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

            case JitOpType::MoveaLDnAn:
            {
                cpu.A[op.dstReg] = cpu.D[op.srcReg];
                break;
            }

            case JitOpType::MoveaLAnAm:
            {
                cpu.A[op.dstReg] = cpu.A[op.srcReg];
                break;
            }

            case JitOpType::AslImmLDn:
            case JitOpType::LslImmLDn:
            {
                auto r = Alu::ShiftLeft(cpu.D[op.dstReg], op.immediate, ccr);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::AsrImmLDn:
            {
                auto r = Alu::ArithShiftRight(cpu.D[op.dstReg], op.immediate);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::LsrImmLDn:
            {
                auto r = Alu::LogicalShiftRight(cpu.D[op.dstReg], op.immediate);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::ExgDnDm:
                std::swap(cpu.D[op.srcReg], cpu.D[op.dstReg]);
                break;

            case JitOpType::ExgAnAm:
                std::swap(cpu.A[op.srcReg], cpu.A[op.dstReg]);
                break;

            case JitOpType::ExgDnAn:
                std::swap(cpu.D[op.srcReg], cpu.A[op.dstReg]);
                break;

            case JitOpType::SwapDn:
            {
                uint32_t v = cpu.D[op.dstReg];
                cpu.D[op.dstReg] = (v >> 16) | (v << 16);
                if (op.needsFlags)
                {
                    uint8_t nz = Alu::SetNZFlags(cpu.D[op.dstReg]);
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::ExtWDn:
            {
                int16_t val = static_cast<int16_t>(static_cast<int8_t>(cpu.D[op.dstReg]));
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u)
                                  | static_cast<uint32_t>(static_cast<uint16_t>(val));
                if (op.needsFlags)
                {
                    uint8_t nz = Alu::SetNZFlags(static_cast<uint16_t>(val));
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::ExtLDn:
            {
                cpu.D[op.dstReg] = static_cast<uint32_t>(
                    static_cast<int16_t>(static_cast<uint16_t>(cpu.D[op.dstReg])));
                if (op.needsFlags)
                {
                    uint8_t nz = Alu::SetNZFlags(cpu.D[op.dstReg]);
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::ExtbLDn:
            {
                cpu.D[op.dstReg] = static_cast<uint32_t>(
                    static_cast<int8_t>(static_cast<uint8_t>(cpu.D[op.dstReg])));
                if (op.needsFlags)
                {
                    uint8_t nz = Alu::SetNZFlags(cpu.D[op.dstReg]);
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::NegLDn:
            {
                auto r = Alu::SubLong(0, cpu.D[op.dstReg], ccr);
                cpu.D[op.dstReg] = r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            case JitOpType::NotLDn:
            {
                cpu.D[op.dstReg] = ~cpu.D[op.dstReg];
                if (op.needsFlags)
                {
                    uint8_t nz = Alu::SetNZFlags(cpu.D[op.dstReg]);
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

        case 0x2: // MOVE.L — register-only variants
        {
            int srcMode = (opcode >> 3) & 7;
            int dstMode = (opcode >> 6) & 7;
            if (srcMode == 0 && dstMode == 0)
                return InsnKind::MoveLDnDm;
            if (srcMode == 1 && dstMode == 0)
                return InsnKind::MoveLAnDn;
            if (srcMode == 0 && dstMode == 1)
                return InsnKind::MoveaLDnAn;
            if (srcMode == 1 && dstMode == 1)
                return InsnKind::MoveaLAnAm;
            break;
        }

        case 0x4: // CLR.L Dn / TST.L Dn / SWAP / EXT / NEG / NOT
        {
            int eaMode = (opcode >> 3) & 7;
            if ((opcode & 0xFFC0) == 0x4280 && eaMode == 0)
                return InsnKind::ClrLDn;
            if ((opcode & 0xFFC0) == 0x4A80 && eaMode == 0)
                return InsnKind::TstLDn;
            if ((opcode & 0xFFF8) == 0x4840)
                return InsnKind::SwapDn;
            if ((opcode & 0xFFF8) == 0x4880)
                return InsnKind::ExtWDn;
            if ((opcode & 0xFFF8) == 0x48C0)
                return InsnKind::ExtLDn;
            if ((opcode & 0xFFF8) == 0x49C0)
                return InsnKind::ExtbLDn;
            if ((opcode & 0xFFC0) == 0x4480 && eaMode == 0)
                return InsnKind::NegLDn;
            if ((opcode & 0xFFC0) == 0x4680 && eaMode == 0)
                return InsnKind::NotLDn;
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

        case 0xC: // AND / EXG: 1100 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (opMode == 2 && eaMode == 0)
                return InsnKind::AndLDnDm;
            // EXG: opMode 5 = Dn↔Dn or An↔An, opMode 6 = Dn↔An
            int mode = (opcode >> 3) & 7;
            if (opMode == 5 && mode == 0) return InsnKind::ExgDnDm;
            if (opMode == 5 && mode == 1) return InsnKind::ExgAnAm;
            if (opMode == 6 && mode == 1) return InsnKind::ExgDnAn;
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

        case 0x5: // ADDQ / SUBQ: 0101 qqq 0 ss mmm rrr / 0101 qqq 1 ss mmm rrr
        {
            int size = (opcode >> 6) & 3;
            if (size == 3) break; // size==3 is Scc/DBcc, not ADDQ/SUBQ
            int eaMode = (opcode >> 3) & 7;
            bool isSub = (opcode & 0x0100) != 0;
            if (eaMode == 0 && size == 2) // Dn, .L only
                return isSub ? InsnKind::SubqLDn : InsnKind::AddqLDn;
            if (eaMode == 1) // An, any size (always 32-bit)
                return isSub ? InsnKind::SubqAn : InsnKind::AddqAn;
            break;
        }

        case 0xE: // Shift: 1110 ccc d ss ir tt rrr
        {
            int sizeField = (opcode >> 6) & 3;
            if (sizeField != 2) break;        // .L only
            if (opcode & 0x0020) break;       // immediate count only (ir=0)
            int shiftType = (opcode >> 3) & 3;
            if (shiftType > 1) break;         // ASL/ASR, LSL/LSR only (exclude ROX/RO)
            bool isLeft = (opcode & 0x0100) != 0;
            if (shiftType == 0)
                return isLeft ? InsnKind::AslImmLDn : InsnKind::AsrImmLDn;
            else
                return isLeft ? InsnKind::LslImmLDn : InsnKind::LsrImmLDn;
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
        else if (kind == InsnKind::Nop || kind == InsnKind::AddqAn || kind == InsnKind::SubqAn
                 || kind == InsnKind::MoveaLDnAn || kind == InsnKind::MoveaLAnAm
                 || kind == InsnKind::ExgDnDm || kind == InsnKind::ExgAnAm || kind == InsnKind::ExgDnAn)
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

    // Compute total cycles from cycle table
    int totalCycles = 0;
    for (auto op : opcodes)
        totalCycles += InstructionDecoder::GetCycles(op);
    block->TotalCycles = totalCycles;

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

            case InsnKind::AddqLDn:
            {
                op.type = JitOpType::AddqLDn;
                int data = (opcode >> 9) & 7;
                if (data == 0) data = 8;
                op.immediate = data;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::SubqLDn:
            {
                op.type = JitOpType::SubqLDn;
                int data = (opcode >> 9) & 7;
                if (data == 0) data = 8;
                op.immediate = data;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::AddqAn:
            {
                op.type = JitOpType::AddqAn;
                int data = (opcode >> 9) & 7;
                if (data == 0) data = 8;
                op.immediate = data;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::SubqAn:
            {
                op.type = JitOpType::SubqAn;
                int data = (opcode >> 9) & 7;
                if (data == 0) data = 8;
                op.immediate = data;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::ClrLDn:
            {
                op.type = JitOpType::ClrLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::TstLDn:
            {
                op.type = JitOpType::TstLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::MoveLAnDn:
            {
                op.type = JitOpType::MoveLAnDn;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::MoveaLDnAn:
            {
                op.type = JitOpType::MoveaLDnAn;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::MoveaLAnAm:
            {
                op.type = JitOpType::MoveaLAnAm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::AslImmLDn:
            case InsnKind::AsrImmLDn:
            case InsnKind::LslImmLDn:
            case InsnKind::LsrImmLDn:
            {
                if (kind == InsnKind::AslImmLDn) op.type = JitOpType::AslImmLDn;
                else if (kind == InsnKind::AsrImmLDn) op.type = JitOpType::AsrImmLDn;
                else if (kind == InsnKind::LslImmLDn) op.type = JitOpType::LslImmLDn;
                else op.type = JitOpType::LsrImmLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                int count = (opcode >> 9) & 7;
                if (count == 0) count = 8;
                op.immediate = count;
                break;
            }

            case InsnKind::ExgDnDm:
            {
                op.type = JitOpType::ExgDnDm;
                op.srcReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::ExgAnAm:
            {
                op.type = JitOpType::ExgAnAm;
                op.srcReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::ExgDnAn:
            {
                op.type = JitOpType::ExgDnAn;
                op.srcReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::SwapDn:
            {
                op.type = JitOpType::SwapDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::ExtWDn:
            {
                op.type = JitOpType::ExtWDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::ExtLDn:
            {
                op.type = JitOpType::ExtLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::ExtbLDn:
            {
                op.type = JitOpType::ExtbLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::NegLDn:
            {
                op.type = JitOpType::NegLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::NotLDn:
            {
                op.type = JitOpType::NotLDn;
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
