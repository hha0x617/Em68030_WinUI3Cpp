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

JitExecResult CompiledBlock::Execute(MC68030& cpu) const
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
            case JitOpType::BccW:
            {
                cpu.SetCCRByte(ccr);
                uint32_t target = cpu.EvaluateCondition(op.condition) ? op.branchTarget : op.fallthroughPC;
                return { target, InstructionCount, TotalCycles };
            }

            case JitOpType::BraB:
            case JitOpType::BraW:
            {
                cpu.SetCCRByte(ccr);
                return { op.branchTarget, InstructionCount, TotalCycles };
            }

            // Phase 1A: LEA
            case JitOpType::LeaAnAr:
            {
                cpu.A[op.dstReg] = cpu.A[op.srcReg];
                break;
            }

            case JitOpType::LeaD16AnAr:
            {
                cpu.A[op.dstReg] = static_cast<uint32_t>(
                    static_cast<int32_t>(cpu.A[op.srcReg]) + op.immediate);
                break;
            }

            case JitOpType::LeaD8AnXnAr:
            {
                uint32_t base = cpu.A[op.srcReg];
                // Get index register value
                uint32_t idx;
                if (op.auxFlags & 0x01) // indexIsAddr
                    idx = cpu.A[op.indexReg];
                else
                    idx = cpu.D[op.indexReg];
                // W/L: if not long (bit3=0), sign-extend from word
                if (!(op.auxFlags & 0x08))
                    idx = static_cast<uint32_t>(static_cast<int16_t>(static_cast<uint16_t>(idx)));
                // Apply scale
                int scale = (op.auxFlags >> 1) & 3;
                idx <<= scale;
                cpu.A[op.dstReg] = static_cast<uint32_t>(
                    static_cast<int32_t>(base) + static_cast<int32_t>(idx) + op.immediate);
                break;
            }

            // Phase 1C: MULU/MULS
            case JitOpType::MuluWDnDm:
            {
                uint32_t result = static_cast<uint32_t>(
                    static_cast<uint16_t>(cpu.D[op.dstReg])) *
                    static_cast<uint32_t>(static_cast<uint16_t>(cpu.D[op.srcReg]));
                cpu.D[op.dstReg] = result;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (result == 0) nz = 0x04;
                    else if (result & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz); // V=0, C=0
                }
                break;
            }

            case JitOpType::MulsWDnDm:
            {
                int32_t sresult = static_cast<int32_t>(static_cast<int16_t>(
                    static_cast<uint16_t>(cpu.D[op.dstReg]))) *
                    static_cast<int32_t>(static_cast<int16_t>(
                    static_cast<uint16_t>(cpu.D[op.srcReg])));
                uint32_t result = static_cast<uint32_t>(sresult);
                cpu.D[op.dstReg] = result;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (result == 0) nz = 0x04;
                    else if (result & 0x80000000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            // Phase 1D: BTST
            case JitOpType::BtstDnDm:
            {
                if (op.needsFlags)
                {
                    int bitNum = cpu.D[op.srcReg] & 31;
                    bool bitSet = (cpu.D[op.dstReg] >> bitNum) & 1;
                    // Z = !bitSet; only Z changes, preserve N,V,C,X
                    if (bitSet)
                        ccr &= ~0x04u; // clear Z
                    else
                        ccr |= 0x04u;  // set Z
                }
                break;
            }

            // Phase 1E: Byte/Word ADD
            case JitOpType::AddBDnDm:
            {
                auto r = Alu::AddByte(
                    static_cast<uint8_t>(cpu.D[op.dstReg]),
                    static_cast<uint8_t>(cpu.D[op.srcReg]), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }
            case JitOpType::AddWDnDm:
            {
                auto r = Alu::AddWord(
                    static_cast<uint16_t>(cpu.D[op.dstReg]),
                    static_cast<uint16_t>(cpu.D[op.srcReg]), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            // Phase 1E: Byte/Word SUB
            case JitOpType::SubBDnDm:
            {
                auto r = Alu::SubByte(
                    static_cast<uint8_t>(cpu.D[op.dstReg]),
                    static_cast<uint8_t>(cpu.D[op.srcReg]), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }
            case JitOpType::SubWDnDm:
            {
                auto r = Alu::SubWord(
                    static_cast<uint16_t>(cpu.D[op.dstReg]),
                    static_cast<uint16_t>(cpu.D[op.srcReg]), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            // Phase 1E: Byte/Word CMP
            case JitOpType::CmpBDnDm:
            {
                auto r = Alu::SubByte(
                    static_cast<uint8_t>(cpu.D[op.dstReg]),
                    static_cast<uint8_t>(cpu.D[op.srcReg]), ccr);
                ccr = static_cast<uint8_t>((ccr & 0x10) | (r.ccr & 0x0F));
                break;
            }
            case JitOpType::CmpWDnDm:
            {
                auto r = Alu::SubWord(
                    static_cast<uint16_t>(cpu.D[op.dstReg]),
                    static_cast<uint16_t>(cpu.D[op.srcReg]), ccr);
                ccr = static_cast<uint8_t>((ccr & 0x10) | (r.ccr & 0x0F));
                break;
            }

            // Phase 1E: Byte/Word AND
            case JitOpType::AndBDnDm:
            {
                uint8_t val = static_cast<uint8_t>(cpu.D[op.dstReg]) & static_cast<uint8_t>(cpu.D[op.srcReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }
            case JitOpType::AndWDnDm:
            {
                uint16_t val = static_cast<uint16_t>(cpu.D[op.dstReg]) & static_cast<uint16_t>(cpu.D[op.srcReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x8000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            // Phase 1E: Byte/Word OR
            case JitOpType::OrBDnDm:
            {
                uint8_t val = static_cast<uint8_t>(cpu.D[op.dstReg]) | static_cast<uint8_t>(cpu.D[op.srcReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }
            case JitOpType::OrWDnDm:
            {
                uint16_t val = static_cast<uint16_t>(cpu.D[op.dstReg]) | static_cast<uint16_t>(cpu.D[op.srcReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x8000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            // Phase 1E: Byte/Word EOR
            case JitOpType::EorBDnDm:
            {
                uint8_t val = static_cast<uint8_t>(cpu.D[op.dstReg]) ^ static_cast<uint8_t>(cpu.D[op.srcReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }
            case JitOpType::EorWDnDm:
            {
                uint16_t val = static_cast<uint16_t>(cpu.D[op.dstReg]) ^ static_cast<uint16_t>(cpu.D[op.srcReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x8000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            // Phase 1E: Byte/Word ADDQ
            case JitOpType::AddqBDn:
            {
                auto r = Alu::AddByte(
                    static_cast<uint8_t>(cpu.D[op.dstReg]),
                    static_cast<uint8_t>(op.immediate), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }
            case JitOpType::AddqWDn:
            {
                auto r = Alu::AddWord(
                    static_cast<uint16_t>(cpu.D[op.dstReg]),
                    static_cast<uint16_t>(op.immediate), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            // Phase 1E: Byte/Word SUBQ
            case JitOpType::SubqBDn:
            {
                auto r = Alu::SubByte(
                    static_cast<uint8_t>(cpu.D[op.dstReg]),
                    static_cast<uint8_t>(op.immediate), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }
            case JitOpType::SubqWDn:
            {
                auto r = Alu::SubWord(
                    static_cast<uint16_t>(cpu.D[op.dstReg]),
                    static_cast<uint16_t>(op.immediate), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            // Phase 1E: Byte/Word CLR
            case JitOpType::ClrBDn:
            {
                cpu.D[op.dstReg] = cpu.D[op.dstReg] & 0xFFFFFF00u;
                if (op.needsFlags)
                    ccr = static_cast<uint8_t>((ccr & 0x10) | 0x04);
                break;
            }
            case JitOpType::ClrWDn:
            {
                cpu.D[op.dstReg] = cpu.D[op.dstReg] & 0xFFFF0000u;
                if (op.needsFlags)
                    ccr = static_cast<uint8_t>((ccr & 0x10) | 0x04);
                break;
            }

            // Phase 1E: Byte/Word TST
            case JitOpType::TstBDn:
            {
                if (op.needsFlags)
                {
                    uint8_t val = static_cast<uint8_t>(cpu.D[op.dstReg]);
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }
            case JitOpType::TstWDn:
            {
                if (op.needsFlags)
                {
                    uint16_t val = static_cast<uint16_t>(cpu.D[op.dstReg]);
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x8000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            // Phase 1E: Byte/Word NEG
            case JitOpType::NegBDn:
            {
                auto r = Alu::SubByte(0, static_cast<uint8_t>(cpu.D[op.dstReg]), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }
            case JitOpType::NegWDn:
            {
                auto r = Alu::SubWord(0, static_cast<uint16_t>(cpu.D[op.dstReg]), ccr);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | r.result;
                if (op.needsFlags) ccr = r.ccr;
                break;
            }

            // Phase 1E: Byte/Word NOT
            case JitOpType::NotBDn:
            {
                uint8_t val = ~static_cast<uint8_t>(cpu.D[op.dstReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFFFF00u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x80) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }
            case JitOpType::NotWDn:
            {
                uint16_t val = ~static_cast<uint16_t>(cpu.D[op.dstReg]);
                cpu.D[op.dstReg] = (cpu.D[op.dstReg] & 0xFFFF0000u) | val;
                if (op.needsFlags)
                {
                    uint8_t nz = 0;
                    if (val == 0) nz = 0x04;
                    else if (val & 0x8000) nz = 0x08;
                    ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                }
                break;
            }

            case JitOpType::Nop:
                break;

            // Phase 2: Memory access instructions
            case JitOpType::MoveLIndAnDm:
            {
                uint32_t addr = cpu.A[op.srcReg];
                if (cpu._dataCacheValid
                    && (addr & ~cpu._dataPageMask) == cpu._dataPageVA
                    && (addr & cpu._dataPageMask) + 3 <= cpu._dataPageMask)
                {
                    uint32_t pa = cpu._dataPagePA + (addr & cpu._dataPageMask);
                    uint32_t val = cpu.GetMemory().ReadLong(pa);
                    cpu.D[op.dstReg] = val;
                    if (op.needsFlags) {
                        uint8_t nz = 0;
                        if (val == 0) nz = 0x04;
                        else if (val & 0x80000000) nz = 0x08;
                        ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                    }
                }
                else
                {
                    cpu.SetCCRByte(ccr);
                    return { op.instrPC, i, CumulativeCycles[i] };
                }
                break;
            }

            case JitOpType::MoveLPostIncAnDm:
            {
                uint32_t addr = cpu.A[op.srcReg];
                if (cpu._dataCacheValid
                    && (addr & ~cpu._dataPageMask) == cpu._dataPageVA
                    && (addr & cpu._dataPageMask) + 3 <= cpu._dataPageMask)
                {
                    uint32_t pa = cpu._dataPagePA + (addr & cpu._dataPageMask);
                    uint32_t val = cpu.GetMemory().ReadLong(pa);
                    cpu.D[op.dstReg] = val;
                    cpu.A[op.srcReg] = addr + 4;
                    if (op.needsFlags) {
                        uint8_t nz = 0;
                        if (val == 0) nz = 0x04;
                        else if (val & 0x80000000) nz = 0x08;
                        ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                    }
                }
                else
                {
                    cpu.SetCCRByte(ccr);
                    return { op.instrPC, i, CumulativeCycles[i] };
                }
                break;
            }

            case JitOpType::MoveLDmIndAn:
            {
                // Write always bails out (no write page cache — M-bit management needed)
                cpu.SetCCRByte(ccr);
                return { op.instrPC, i, CumulativeCycles[i] };
            }

            case JitOpType::MoveLD16AnDm:
            {
                uint32_t addr = static_cast<uint32_t>(
                    static_cast<int32_t>(cpu.A[op.srcReg]) + op.immediate);
                if (cpu._dataCacheValid
                    && (addr & ~cpu._dataPageMask) == cpu._dataPageVA
                    && (addr & cpu._dataPageMask) + 3 <= cpu._dataPageMask)
                {
                    uint32_t pa = cpu._dataPagePA + (addr & cpu._dataPageMask);
                    uint32_t val = cpu.GetMemory().ReadLong(pa);
                    cpu.D[op.dstReg] = val;
                    if (op.needsFlags) {
                        uint8_t nz = 0;
                        if (val == 0) nz = 0x04;
                        else if (val & 0x80000000) nz = 0x08;
                        ccr = static_cast<uint8_t>((ccr & 0x10) | nz);
                    }
                }
                else
                {
                    cpu.SetCCRByte(ccr);
                    return { op.instrPC, i, CumulativeCycles[i] };
                }
                break;
            }

            case JitOpType::MoveLDmD16An:
            {
                // Write always bails out
                cpu.SetCCRByte(ccr);
                return { op.instrPC, i, CumulativeCycles[i] };
            }

            case JitOpType::Rts:
            {
                // RTS: PC = ReadLong(A7); A7 += 4
                uint32_t sp = cpu.A[7];
                if (cpu._dataCacheValid
                    && (sp & ~cpu._dataPageMask) == cpu._dataPageVA
                    && (sp & cpu._dataPageMask) + 3 <= cpu._dataPageMask)
                {
                    uint32_t pa = cpu._dataPagePA + (sp & cpu._dataPageMask);
                    uint32_t retAddr = cpu.GetMemory().ReadLong(pa);
                    cpu.A[7] = sp + 4;
                    cpu.SetCCRByte(ccr);
                    return { retAddr, i + 1, CumulativeCycles[i + 1] };
                }
                else
                {
                    cpu.SetCCRByte(ccr);
                    return { op.instrPC, i, CumulativeCycles[i] };
                }
            }
        }
    }

    // Write back CCR at end of block
    cpu.SetCCRByte(ccr);
    return { FallthroughPC, InstructionCount, TotalCycles };
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

void JitCache::RemoveBlock(uint32_t physAddr)
{
    int idx = static_cast<int>((physAddr >> 1) & BlockCacheMask);
    if (m_blockCache[idx] && m_blockCache[idx]->PhysicalAddress == physAddr)
        m_blockCache[idx] = nullptr;
    // Block remains in m_blocks vector (ownership) but is no longer reachable via cache lookup.
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

    // RTS: 0x4E75
    if (opcode == 0x4E75)
        return InsnKind::Rts;

    int group = (opcode >> 12) & 0xF;

    switch (group)
    {
        case 0x7: // MOVEQ: 0111 rrr 0 iiiiiiii
            if ((opcode & 0x0100) == 0)
                return InsnKind::Moveq;
            break;

        case 0x2: // MOVE.L
        {
            int srcMode = (opcode >> 3) & 7;
            int dstMode = (opcode >> 6) & 7;
            // Register-only variants
            if (srcMode == 0 && dstMode == 0)
                return InsnKind::MoveLDnDm;
            if (srcMode == 1 && dstMode == 0)
                return InsnKind::MoveLAnDn;
            if (srcMode == 0 && dstMode == 1)
                return InsnKind::MoveaLDnAn;
            if (srcMode == 1 && dstMode == 1)
                return InsnKind::MoveaLAnAm;
            // Phase 2: Memory access variants (MOVE.L only)
            // MOVE.L (An), Dm — srcMode=2, dstMode=0
            if (srcMode == 2 && dstMode == 0)
                return InsnKind::MoveLIndAnDm;
            // MOVE.L (An)+, Dm — srcMode=3, dstMode=0
            if (srcMode == 3 && dstMode == 0)
                return InsnKind::MoveLPostIncAnDm;
            // MOVE.L Dm, (An) — srcMode=0, dstMode=2
            if (srcMode == 0 && dstMode == 2)
                return InsnKind::MoveLDmIndAn;
            // MOVE.L d16(An), Dm — srcMode=5, dstMode=0
            if (srcMode == 5 && dstMode == 0)
                return InsnKind::MoveLD16AnDm;
            // MOVE.L Dm, d16(An) — srcMode=0, dstMode=5
            if (srcMode == 0 && dstMode == 5)
                return InsnKind::MoveLDmD16An;
            break;
        }

        case 0x0: // BTST Dn,Dm
        {
            int eaMode = (opcode >> 3) & 7;
            // BTST Dn,Dm: 0000 sss 100 000 ddd
            if ((opcode & 0x01C0) == 0x0100 && eaMode == 0)
                return InsnKind::BtstDnDm;
            break;
        }

        case 0x4: // CLR Dn / TST Dn / SWAP / EXT / NEG / NOT / LEA
        {
            int eaMode = (opcode >> 3) & 7;
            // LEA: 0100 rrr 111 mmm rrr
            if ((opcode & 0xF1C0) == 0x41C0)
            {
                if (eaMode == 2) return InsnKind::LeaAnAr;
                if (eaMode == 5) return InsnKind::LeaD16AnAr;
                if (eaMode == 6) return InsnKind::LeaD8AnXnAr;
                // Don't break — eaMode=0 overlaps with EXTB.L etc.
            }
            // CLR: size in bits 7-6: 00=.B, 01=.W, 10=.L
            if ((opcode & 0xFF00) == 0x4200 && eaMode == 0)
            {
                int sz = (opcode >> 6) & 3;
                if (sz == 0) return InsnKind::ClrBDn;
                if (sz == 1) return InsnKind::ClrWDn;
                if (sz == 2) return InsnKind::ClrLDn;
            }
            // TST: size in bits 7-6
            if ((opcode & 0xFF00) == 0x4A00 && eaMode == 0)
            {
                int sz = (opcode >> 6) & 3;
                if (sz == 0) return InsnKind::TstBDn;
                if (sz == 1) return InsnKind::TstWDn;
                if (sz == 2) return InsnKind::TstLDn;
            }
            if ((opcode & 0xFFF8) == 0x4840)
                return InsnKind::SwapDn;
            if ((opcode & 0xFFF8) == 0x4880)
                return InsnKind::ExtWDn;
            if ((opcode & 0xFFF8) == 0x48C0)
                return InsnKind::ExtLDn;
            if ((opcode & 0xFFF8) == 0x49C0)
                return InsnKind::ExtbLDn;
            // NEG: size in bits 7-6
            if ((opcode & 0xFF00) == 0x4400 && eaMode == 0)
            {
                int sz = (opcode >> 6) & 3;
                if (sz == 0) return InsnKind::NegBDn;
                if (sz == 1) return InsnKind::NegWDn;
                if (sz == 2) return InsnKind::NegLDn;
            }
            // NOT: size in bits 7-6
            if ((opcode & 0xFF00) == 0x4600 && eaMode == 0)
            {
                int sz = (opcode >> 6) & 3;
                if (sz == 0) return InsnKind::NotBDn;
                if (sz == 1) return InsnKind::NotWDn;
                if (sz == 2) return InsnKind::NotLDn;
            }
            break;
        }

        case 0xD: // ADD: 1101 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            // ADD EA,Dn: opMode 0=.B, 1=.W, 2=.L
            if (eaMode == 0)
            {
                if (opMode == 0) return InsnKind::AddBDnDm;
                if (opMode == 1) return InsnKind::AddWDnDm;
                if (opMode == 2) return InsnKind::AddLDnDm;
            }
            break;
        }

        case 0x9: // SUB: 1001 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (eaMode == 0)
            {
                if (opMode == 0) return InsnKind::SubBDnDm;
                if (opMode == 1) return InsnKind::SubWDnDm;
                if (opMode == 2) return InsnKind::SubLDnDm;
            }
            break;
        }

        case 0xB: // CMP/EOR: 1011 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            if (eaMode == 0)
            {
                if (opMode == 0) return InsnKind::CmpBDnDm;
                if (opMode == 1) return InsnKind::CmpWDnDm;
                if (opMode == 2) return InsnKind::CmpLDnDm;
                // EOR: opMode 4=.B, 5=.W, 6=.L
                if (opMode == 4) return InsnKind::EorBDnDm;
                if (opMode == 5) return InsnKind::EorWDnDm;
                if (opMode == 6) return InsnKind::EorLDnDm;
            }
            break;
        }

        case 0xC: // AND / EXG / MULU / MULS: 1100 ddd ooo mmm rrr
        {
            int opMode = (opcode >> 6) & 7;
            int eaMode = (opcode >> 3) & 7;
            // AND EA,Dn: opMode 0=.B, 1=.W, 2=.L
            if (eaMode == 0)
            {
                if (opMode == 0) return InsnKind::AndBDnDm;
                if (opMode == 1) return InsnKind::AndWDnDm;
                if (opMode == 2) return InsnKind::AndLDnDm;
            }
            // MULU.W Dn,Dm: opMode=3, eaMode=0
            if (opMode == 3 && eaMode == 0) return InsnKind::MuluWDnDm;
            // MULS.W Dn,Dm: opMode=7, eaMode=0
            if (opMode == 7 && eaMode == 0) return InsnKind::MulsWDnDm;
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
            if (eaMode == 0)
            {
                if (opMode == 0) return InsnKind::OrBDnDm;
                if (opMode == 1) return InsnKind::OrWDnDm;
                if (opMode == 2) return InsnKind::OrLDnDm;
            }
            break;
        }

        case 0x5: // ADDQ / SUBQ: 0101 qqq 0 ss mmm rrr / 0101 qqq 1 ss mmm rrr
        {
            int size = (opcode >> 6) & 3;
            if (size == 3) break; // size==3 is Scc/DBcc, not ADDQ/SUBQ
            int eaMode = (opcode >> 3) & 7;
            bool isSub = (opcode & 0x0100) != 0;
            if (eaMode == 0) // Dn
            {
                if (size == 0) return isSub ? InsnKind::SubqBDn : InsnKind::AddqBDn;
                if (size == 1) return isSub ? InsnKind::SubqWDn : InsnKind::AddqWDn;
                if (size == 2) return isSub ? InsnKind::SubqLDn : InsnKind::AddqLDn;
            }
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
            if (cond == 1) // BSR — not supported (pushes return address)
                break;
            if (disp8 == 0xFF) // 32-bit extension — not supported
                break;
            if (disp8 == 0x00) // 16-bit displacement
            {
                if (cond == 0)
                    return InsnKind::BranchAlwaysW;
                return InsnKind::BranchW;
            }
            // 8-bit displacement
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
    // Scanned instruction data (supports multi-word instructions)
    struct ScannedInsn {
        uint16_t opcode;
        uint16_t extWord;    // extension word (for 2-word instructions)
        InsnKind kind;
        uint8_t byteLength;  // 2 or 4
    };

    // Phase 1: Scan forward to find the extent of the basic block
    std::vector<ScannedInsn> insns;
    uint32_t scanPA = startPhysAddr;
    int totalBytes = 0;

    for (int i = 0; i < MaxBlockLength; i++)
    {
        uint16_t opcode;
        try { opcode = cpu.GetMemory().ReadWord(scanPA); }
        catch (...) { break; }

        auto kind = Classify(opcode);
        if (kind == InsnKind::Unsupported)
            break;

        // Determine instruction size and read extension word if needed
        uint16_t extWord = 0;
        uint8_t byteLen = 2;
        bool isMultiWord = (kind == InsnKind::LeaD16AnAr || kind == InsnKind::LeaD8AnXnAr
                         || kind == InsnKind::BranchW || kind == InsnKind::BranchAlwaysW
                         || kind == InsnKind::MoveLD16AnDm || kind == InsnKind::MoveLDmD16An);
        if (isMultiWord)
        {
            try { extWord = cpu.GetMemory().ReadWord(scanPA + 2); }
            catch (...) { break; }
            byteLen = 4;

            // LEA d8(An,Xn): reject full extension word (bit 8 set)
            if (kind == InsnKind::LeaD8AnXnAr && (extWord & 0x0100))
                break;
        }

        // RTS terminates block (include it)
        if (kind == InsnKind::Rts)
        {
            insns.push_back({opcode, extWord, kind, byteLen});
            totalBytes += byteLen;
            scanPA += byteLen;
            break;
        }

        // Backward branch check
        bool isBranch = (kind == InsnKind::Branch || kind == InsnKind::BranchAlways
                      || kind == InsnKind::BranchW || kind == InsnKind::BranchAlwaysW);
        if (isBranch)
        {
            uint32_t pcAfterFetch = startPC + static_cast<uint32_t>(totalBytes) + 2;
            int32_t disp;
            if (kind == InsnKind::BranchW || kind == InsnKind::BranchAlwaysW)
                disp = static_cast<int16_t>(extWord);
            else
                disp = static_cast<int8_t>(opcode & 0xFF);
            uint32_t target = static_cast<uint32_t>(static_cast<int32_t>(pcAfterFetch) + disp);
            if (target <= startPC)
                break; // Backward branch — terminate block before it

            insns.push_back({opcode, extWord, kind, byteLen});
            totalBytes += byteLen;
            scanPA += byteLen;
            break; // Forward branch — include and terminate
        }

        insns.push_back({opcode, extWord, kind, byteLen});
        totalBytes += byteLen;
        scanPA += byteLen;
    }

    if (insns.empty())
        return nullptr;

    // Phase 2: Dead flag elimination
    std::vector<bool> needsFlags(insns.size(), false);
    needsFlags[insns.size() - 1] = true;
    bool flagsLive = true;
    for (int i = static_cast<int>(insns.size()) - 1; i >= 0; i--)
    {
        auto kind = insns[i].kind;
        if (kind == InsnKind::Branch || kind == InsnKind::BranchAlways
            || kind == InsnKind::BranchW || kind == InsnKind::BranchAlwaysW)
        {
            needsFlags[i] = false;
            flagsLive = (kind == InsnKind::Branch || kind == InsnKind::BranchW);
        }
        else if (kind == InsnKind::Nop || kind == InsnKind::AddqAn || kind == InsnKind::SubqAn
                 || kind == InsnKind::MoveaLDnAn || kind == InsnKind::MoveaLAnAm
                 || kind == InsnKind::ExgDnDm || kind == InsnKind::ExgAnAm || kind == InsnKind::ExgDnAn
                 || kind == InsnKind::LeaAnAr || kind == InsnKind::LeaD16AnAr || kind == InsnKind::LeaD8AnXnAr
                 || kind == InsnKind::MoveLDmIndAn || kind == InsnKind::MoveLDmD16An
                 || kind == InsnKind::Rts)
        {
            needsFlags[i] = false; // These don't touch flags
        }
        else if (kind == InsnKind::BtstDnDm)
        {
            // BTST only changes Z, doesn't overwrite NVC — treat as partial flag modifier
            needsFlags[i] = flagsLive;
            // Do NOT set flagsLive = false — earlier flags are still needed for NVC
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
    block->InstructionCount = static_cast<int>(insns.size());
    block->ByteLength = totalBytes;

    // Compute total cycles and cumulative cycles from cycle table
    int totalCycles = 0;
    block->CumulativeCycles.resize(insns.size() + 1);
    for (size_t idx = 0; idx < insns.size(); idx++)
    {
        block->CumulativeCycles[idx] = totalCycles;
        totalCycles += InstructionDecoder::GetCycles(insns[idx].opcode);
    }
    block->CumulativeCycles[insns.size()] = totalCycles;
    block->TotalCycles = totalCycles;

    block->Ops.reserve(insns.size());

    uint32_t pc = startPC;
    for (size_t i = 0; i < insns.size(); i++)
    {
        uint32_t instrPC = pc; // PC before advancing past this instruction
        pc += insns[i].byteLength; // PC advances past entire instruction
        uint16_t opcode = insns[i].opcode;
        uint16_t extWord = insns[i].extWord;
        auto kind = insns[i].kind;

        JitOp op{};
        op.needsFlags = needsFlags[i];
        op.instrPC = instrPC;

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
            case InsnKind::AddBDnDm:
            case InsnKind::AddWDnDm:
            {
                if (kind == InsnKind::AddBDnDm) op.type = JitOpType::AddBDnDm;
                else if (kind == InsnKind::AddWDnDm) op.type = JitOpType::AddWDnDm;
                else op.type = JitOpType::AddLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::SubLDnDm:
            case InsnKind::SubBDnDm:
            case InsnKind::SubWDnDm:
            {
                if (kind == InsnKind::SubBDnDm) op.type = JitOpType::SubBDnDm;
                else if (kind == InsnKind::SubWDnDm) op.type = JitOpType::SubWDnDm;
                else op.type = JitOpType::SubLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::CmpLDnDm:
            case InsnKind::CmpBDnDm:
            case InsnKind::CmpWDnDm:
            {
                if (kind == InsnKind::CmpBDnDm) op.type = JitOpType::CmpBDnDm;
                else if (kind == InsnKind::CmpWDnDm) op.type = JitOpType::CmpWDnDm;
                else op.type = JitOpType::CmpLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::AndLDnDm:
            case InsnKind::AndBDnDm:
            case InsnKind::AndWDnDm:
            {
                if (kind == InsnKind::AndBDnDm) op.type = JitOpType::AndBDnDm;
                else if (kind == InsnKind::AndWDnDm) op.type = JitOpType::AndWDnDm;
                else op.type = JitOpType::AndLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::OrLDnDm:
            case InsnKind::OrBDnDm:
            case InsnKind::OrWDnDm:
            {
                if (kind == InsnKind::OrBDnDm) op.type = JitOpType::OrBDnDm;
                else if (kind == InsnKind::OrWDnDm) op.type = JitOpType::OrWDnDm;
                else op.type = JitOpType::OrLDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::EorLDnDm:
            case InsnKind::EorBDnDm:
            case InsnKind::EorWDnDm:
            {
                // EOR: Dn is source (bits 11-9), EA reg is dest (bits 2-0)
                if (kind == InsnKind::EorBDnDm) op.type = JitOpType::EorBDnDm;
                else if (kind == InsnKind::EorWDnDm) op.type = JitOpType::EorWDnDm;
                else op.type = JitOpType::EorLDnDm;
                op.srcReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::AddqLDn:
            case InsnKind::AddqBDn:
            case InsnKind::AddqWDn:
            {
                if (kind == InsnKind::AddqBDn) op.type = JitOpType::AddqBDn;
                else if (kind == InsnKind::AddqWDn) op.type = JitOpType::AddqWDn;
                else op.type = JitOpType::AddqLDn;
                int data = (opcode >> 9) & 7;
                if (data == 0) data = 8;
                op.immediate = data;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::SubqLDn:
            case InsnKind::SubqBDn:
            case InsnKind::SubqWDn:
            {
                if (kind == InsnKind::SubqBDn) op.type = JitOpType::SubqBDn;
                else if (kind == InsnKind::SubqWDn) op.type = JitOpType::SubqWDn;
                else op.type = JitOpType::SubqLDn;
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
            case InsnKind::ClrBDn:
            case InsnKind::ClrWDn:
            {
                if (kind == InsnKind::ClrBDn) op.type = JitOpType::ClrBDn;
                else if (kind == InsnKind::ClrWDn) op.type = JitOpType::ClrWDn;
                else op.type = JitOpType::ClrLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::TstLDn:
            case InsnKind::TstBDn:
            case InsnKind::TstWDn:
            {
                if (kind == InsnKind::TstBDn) op.type = JitOpType::TstBDn;
                else if (kind == InsnKind::TstWDn) op.type = JitOpType::TstWDn;
                else op.type = JitOpType::TstLDn;
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
            case InsnKind::NegBDn:
            case InsnKind::NegWDn:
            {
                if (kind == InsnKind::NegBDn) op.type = JitOpType::NegBDn;
                else if (kind == InsnKind::NegWDn) op.type = JitOpType::NegWDn;
                else op.type = JitOpType::NegLDn;
                op.dstReg = static_cast<uint8_t>(opcode & 7);
                break;
            }

            case InsnKind::NotLDn:
            case InsnKind::NotBDn:
            case InsnKind::NotWDn:
            {
                if (kind == InsnKind::NotBDn) op.type = JitOpType::NotBDn;
                else if (kind == InsnKind::NotWDn) op.type = JitOpType::NotWDn;
                else op.type = JitOpType::NotLDn;
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

            case InsnKind::BranchAlwaysW:
            {
                int16_t disp16 = static_cast<int16_t>(extWord);
                // For Bcc.W/BRA.W, displacement is relative to PC after opcode fetch (pc - 2)
                uint32_t pcAfterOpcode = pc - 2;  // PC after fetching the opcode word
                op.type = JitOpType::BraW;
                op.branchTarget = static_cast<uint32_t>(static_cast<int32_t>(pcAfterOpcode) + disp16);
                break;
            }

            case InsnKind::BranchW:
            {
                int cond = (opcode >> 8) & 0xF;
                int16_t disp16 = static_cast<int16_t>(extWord);
                uint32_t pcAfterOpcode = pc - 2;
                op.type = JitOpType::BccW;
                op.condition = static_cast<uint8_t>(cond);
                op.branchTarget = static_cast<uint32_t>(static_cast<int32_t>(pcAfterOpcode) + disp16);
                op.fallthroughPC = pc;
                break;
            }

            // Phase 1A: LEA
            case InsnKind::LeaAnAr:
            {
                op.type = JitOpType::LeaAnAr;
                op.srcReg = static_cast<uint8_t>(opcode & 7);       // An
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7); // Ar
                break;
            }

            case InsnKind::LeaD16AnAr:
            {
                op.type = JitOpType::LeaD16AnAr;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                op.immediate = static_cast<int16_t>(extWord);
                break;
            }

            case InsnKind::LeaD8AnXnAr:
            {
                op.type = JitOpType::LeaD8AnXnAr;
                op.srcReg = static_cast<uint8_t>(opcode & 7);        // base An
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7); // dest Ar
                op.immediate = static_cast<int8_t>(extWord & 0xFF);  // d8
                op.indexReg = static_cast<uint8_t>((extWord >> 12) & 7);
                // auxFlags: bit0=indexIsAddr, bit1-2=scale, bit3=indexIsLong
                uint8_t flags = 0;
                if (extWord & 0x8000) flags |= 0x01; // D/A bit
                flags |= static_cast<uint8_t>(((extWord >> 9) & 3) << 1); // scale
                if (extWord & 0x0800) flags |= 0x08; // W/L bit
                op.auxFlags = flags;
                break;
            }

            // Phase 1C: MULU/MULS
            case InsnKind::MuluWDnDm:
            {
                op.type = JitOpType::MuluWDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::MulsWDnDm:
            {
                op.type = JitOpType::MulsWDnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            // Phase 1D: BTST
            case InsnKind::BtstDnDm:
            {
                op.type = JitOpType::BtstDnDm;
                op.srcReg = static_cast<uint8_t>((opcode >> 9) & 7); // bit number in Dn
                op.dstReg = static_cast<uint8_t>(opcode & 7);        // test target Dm
                break;
            }

            case InsnKind::Nop:
            {
                op.type = JitOpType::Nop;
                break;
            }

            // Phase 2: Memory access instructions
            case InsnKind::MoveLIndAnDm:
            {
                op.type = JitOpType::MoveLIndAnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);       // An
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7); // Dm
                break;
            }

            case InsnKind::MoveLPostIncAnDm:
            {
                op.type = JitOpType::MoveLPostIncAnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7);
                break;
            }

            case InsnKind::MoveLDmIndAn:
            {
                op.type = JitOpType::MoveLDmIndAn;
                op.srcReg = static_cast<uint8_t>(opcode & 7);       // Dm (source data)
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7); // An (address)
                break;
            }

            case InsnKind::MoveLD16AnDm:
            {
                op.type = JitOpType::MoveLD16AnDm;
                op.srcReg = static_cast<uint8_t>(opcode & 7);       // An
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7); // Dm
                op.immediate = static_cast<int16_t>(extWord);        // d16
                break;
            }

            case InsnKind::MoveLDmD16An:
            {
                op.type = JitOpType::MoveLDmD16An;
                op.srcReg = static_cast<uint8_t>(opcode & 7);       // Dm (source data)
                op.dstReg = static_cast<uint8_t>((opcode >> 9) & 7); // An (address)
                op.immediate = static_cast<int16_t>(extWord);        // d16
                break;
            }

            case InsnKind::Rts:
            {
                op.type = JitOpType::Rts;
                break;
            }

            default:
                break;
        }

        block->Ops.push_back(op);
    }

    block->FallthroughPC = pc;

    // Determine if block is register-only (no memory access → no bailout possible → no snapshot needed)
    block->RegisterOnly = true;
    for (const auto& op : block->Ops) {
        switch (op.type) {
            case JitOpType::MoveLIndAnDm:
            case JitOpType::MoveLPostIncAnDm:
            case JitOpType::MoveLDmIndAn:
            case JitOpType::MoveLD16AnDm:
            case JitOpType::MoveLDmD16An:
            case JitOpType::Rts:
                block->RegisterOnly = false;
                break;
            default:
                break;
        }
        if (!block->RegisterOnly) break;
    }

    return block;
}

} // namespace Em68030::Core
