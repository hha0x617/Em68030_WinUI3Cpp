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

#include "InstructionDecoder.h"
#include "MC68030.h"
#include "Alu.h"
#include "AddressingModes.h"
#include "Fpu.h"
#include "Mmu.h"

#include <format>
#include <sstream>
#include <vector>

#include "FpuInstructionDecoder.h"

namespace Em68030::Core {

// ========================================================================
// Constructor / Destructor
// ========================================================================

InstructionDecoder::InstructionDecoder(MC68030& cpu)
    : _cpu(cpu)
    , _fpuDecoder(std::make_unique<FpuInstructionDecoder>(cpu, cpu.GetFpu()))
{
    InitOpcodeTable();
}

InstructionDecoder::~InstructionDecoder() = default;

// ========================================================================
// Opcode dispatch table (65536 entries)
// ========================================================================

InstructionDecoder::OpcodeHandler InstructionDecoder::s_opcodeTable[65536] = {};
bool InstructionDecoder::s_tableInitialized = false;

void InstructionDecoder::InitOpcodeTable()
{
    if (s_tableInitialized) return;

    // Fill group-level defaults based on bits 15-12
    for (int op = 0; op < 65536; op++)
    {
        int group = (op >> 12) & 0xF;
        switch (group)
        {
            case 0x0: s_opcodeTable[op] = &InstructionDecoder::DecodeGroup0; break;
            case 0x1: s_opcodeTable[op] = &InstructionDecoder::DecodeMoveB; break;
            case 0x2: s_opcodeTable[op] = &InstructionDecoder::DecodeMoveL; break;
            case 0x3: s_opcodeTable[op] = &InstructionDecoder::DecodeMoveW; break;
            case 0x4: s_opcodeTable[op] = &InstructionDecoder::DecodeGroup4; break;
            case 0x5: s_opcodeTable[op] = &InstructionDecoder::DecodeGroup5; break;
            case 0x6: s_opcodeTable[op] = &InstructionDecoder::DecodeGroup6; break;
            case 0x7: s_opcodeTable[op] = &InstructionDecoder::DecodeMOVEQ; break;
            case 0x8: s_opcodeTable[op] = &InstructionDecoder::DecodeGroup8; break;
            case 0x9: s_opcodeTable[op] = &InstructionDecoder::DecodeGroup9; break;
            case 0xA: s_opcodeTable[op] = &InstructionDecoder::DecodeLineA; break;
            case 0xB: s_opcodeTable[op] = &InstructionDecoder::DecodeGroupB; break;
            case 0xC: s_opcodeTable[op] = &InstructionDecoder::DecodeGroupC; break;
            case 0xD: s_opcodeTable[op] = &InstructionDecoder::DecodeGroupD; break;
            case 0xE: s_opcodeTable[op] = &InstructionDecoder::DecodeGroupE; break;
            case 0xF: s_opcodeTable[op] = &InstructionDecoder::DecodeLineF; break;
        }
    }

    // ---- Phase 3.2: Specialized fast handlers for frequent opcodes ----

    // MOVEQ #imm8, Dn — 0x7000-0x7E00 (bit 8 = 0)
    for (int reg = 0; reg < 8; reg++)
        for (int imm = 0; imm < 256; imm++)
        {
            uint16_t op = static_cast<uint16_t>(0x7000 | (reg << 9) | imm);
            if ((op & 0x0100) == 0)
                s_opcodeTable[op] = &InstructionDecoder::FastMOVEQ;
        }

    // RTS — 0x4E75
    s_opcodeTable[0x4E75] = &InstructionDecoder::FastRTS;

    // Bcc.B (8-bit displacement, conditions 2-F, disp != 0 and != -1)
    for (int cond = 2; cond <= 0xF; cond++)
        for (int disp = 1; disp <= 254; disp++)  // 1-254 (not 0 or 255)
        {
            uint16_t op = static_cast<uint16_t>(0x6000 | (cond << 8) | disp);
            s_opcodeTable[op] = &InstructionDecoder::FastBcc8;
        }

    // MOVE.L Dn, Dm — opcode 0x2000 | (dstReg<<9) | srcReg, dstMode=0, srcMode=0
    for (int dst = 0; dst < 8; dst++)
        for (int src = 0; src < 8; src++)
        {
            uint16_t op = static_cast<uint16_t>(0x2000 | (dst << 9) | src);
            s_opcodeTable[op] = &InstructionDecoder::FastMoveLDnDm;
        }

    // ADD.L Dn, Dm — group 0xD, opMode=2 (size=long), mode=0 (Dn), toEa=0
    // Encoding: 1101 DDD 010 000 SSS  = 0xD080 | (dst<<9) | src
    for (int dst = 0; dst < 8; dst++)
        for (int src = 0; src < 8; src++)
        {
            uint16_t op = static_cast<uint16_t>(0xD080 | (dst << 9) | src);
            s_opcodeTable[op] = &InstructionDecoder::FastAddLDnDm;
        }

    // SUB.L Dn, Dm — group 0x9, opMode=2 (size=long), mode=0 (Dn), toEa=0
    // Encoding: 1001 DDD 010 000 SSS  = 0x9080 | (dst<<9) | src
    for (int dst = 0; dst < 8; dst++)
        for (int src = 0; src < 8; src++)
        {
            uint16_t op = static_cast<uint16_t>(0x9080 | (dst << 9) | src);
            s_opcodeTable[op] = &InstructionDecoder::FastSubLDnDm;
        }

    // CMP.L Dn, Dm — group 0xB, opMode=2, mode=0 (Dn)
    // Encoding: 1011 DDD 010 000 SSS  = 0xB080 | (dst<<9) | src
    for (int dst = 0; dst < 8; dst++)
        for (int src = 0; src < 8; src++)
        {
            uint16_t op = static_cast<uint16_t>(0xB080 | (dst << 9) | src);
            s_opcodeTable[op] = &InstructionDecoder::FastCmpLDnDm;
        }

    InitCycleTable();

    s_tableInitialized = true;
}

// ========================================================================
// ExecuteNext — single table lookup dispatch
// ========================================================================

uint16_t InstructionDecoder::ExecuteNext()
{
    uint16_t opcode = _cpu.FetchWord();
    (this->*s_opcodeTable[opcode])(opcode);
    return opcode;
}

// ========================================================================
// Cycle table — approximate MC68030 cycle counts per opcode
// ========================================================================

uint8_t InstructionDecoder::s_cycleTable[65536] = {};

static uint8_t EaReadCost(int mode, int reg)
{
    switch (mode)
    {
        case 0: return 0;  // Dn
        case 1: return 0;  // An
        case 2: return 4;  // (An)
        case 3: return 4;  // (An)+
        case 4: return 4;  // -(An)
        case 5: return 4;  // d16(An)
        case 6: return 6;  // d8(An,Xn)
        case 7:
            switch (reg)
            {
                case 0: return 4;  // abs.W
                case 1: return 8;  // abs.L
                case 2: return 4;  // d16(PC)
                case 3: return 6;  // d8(PC,Xn)
                case 4: return 4;  // #imm
                default: return 4;
            }
        default: return 4;
    }
}

static uint8_t EaWriteCost(int mode, int reg)
{
    switch (mode)
    {
        case 0: return 0;  // Dn
        case 1: return 0;  // An
        case 2: return 4;  // (An)
        case 3: return 4;  // (An)+
        case 4: return 4;  // -(An)
        case 5: return 4;  // d16(An)
        case 6: return 6;  // d8(An,Xn)
        case 7:
            switch (reg)
            {
                case 0: return 4;  // abs.W
                case 1: return 8;  // abs.L
                default: return 4;
            }
        default: return 4;
    }
}

static uint8_t ClampCycles(int cycles)
{
    return static_cast<uint8_t>(cycles > 255 ? 255 : cycles);
}

void InstructionDecoder::InitCycleTable()
{
    for (int op = 0; op < 65536; op++)
    {
        int group = (op >> 12) & 0xF;
        int srcMode = (op >> 3) & 7;
        int srcReg = op & 7;
        int cycles = 4; // default

        switch (group)
        {
            case 0x0: // ORI/ANDI/SUBI/ADDI/CMPI/EORI/Bit ops
            {
                int eaM = srcMode;
                int eaR = srcReg;
                cycles = 4 + EaReadCost(eaM, eaR);
                if (eaM != 0 && eaM != 1) // dest is memory
                    cycles += EaWriteCost(eaM, eaR);
                break;
            }

            case 0x1: // MOVE.B
            case 0x2: // MOVE.L
            case 0x3: // MOVE.W
            {
                int smMode = srcMode;
                int smReg = srcReg;
                // MOVE dst encoding: bits[11:9]=dstReg, bits[8:6]=dstMode (note: mode/reg reversed)
                int dmMode = (op >> 6) & 7;
                int dmReg = (op >> 9) & 7;
                cycles = 2 + EaReadCost(smMode, smReg) + EaWriteCost(dmMode, dmReg);
                break;
            }

            case 0x4: // Misc group
            {
                uint16_t opcode = static_cast<uint16_t>(op);
                if (opcode == 0x4E71) { cycles = 2; break; } // NOP
                if (opcode == 0x4E75) { cycles = 10; break; } // RTS
                if (opcode == 0x4E73) { cycles = 14; break; } // RTE
                if (opcode == 0x4E70) { cycles = 255; break; } // RESET (clamp)

                // TRAP #n: 0x4E40-0x4E4F
                if ((opcode & 0xFFF0) == 0x4E40) { cycles = 20; break; }

                // LINK: 0x4E50-0x4E57 (word), 0x4808-0x480F (long)
                if ((opcode & 0xFFF8) == 0x4E50) { cycles = 6; break; }
                if ((opcode & 0xFFF8) == 0x4808) { cycles = 6; break; }

                // UNLK: 0x4E58-0x4E5F
                if ((opcode & 0xFFF8) == 0x4E58) { cycles = 6; break; }

                // SWAP: 0x4840-0x4847
                if ((opcode & 0xFFF8) == 0x4840) { cycles = 2; break; }

                // EXT.W: 0x4880-0x4887, EXT.L: 0x48C0-0x48C7, EXTB.L: 0x49C0-0x49C7
                if ((opcode & 0xFFF8) == 0x4880) { cycles = 2; break; }
                if ((opcode & 0xFFF8) == 0x48C0) { cycles = 2; break; }
                if ((opcode & 0xFFF8) == 0x49C0) { cycles = 2; break; }

                // JSR: 0x4E80-0x4EBF (bits 5:0 = EA)
                if ((opcode & 0xFFC0) == 0x4E80)
                {
                    cycles = 8 + EaReadCost(srcMode, srcReg);
                    break;
                }

                // JMP: 0x4EC0-0x4EFF
                if ((opcode & 0xFFC0) == 0x4EC0)
                {
                    cycles = 4 + EaReadCost(srcMode, srcReg);
                    break;
                }

                // LEA: 0x41C0 pattern — 0100 rrr 111 mmm rrr
                if ((opcode & 0xF1C0) == 0x41C0)
                {
                    cycles = 2 + EaReadCost(srcMode, srcReg);
                    break;
                }

                // PEA: 0x4840 pattern — but 0x4840-0x4847 is SWAP, PEA is 0100 1000 01 mmm rrr
                if ((opcode & 0xFFC0) == 0x4840 && srcMode != 0)
                {
                    cycles = 6 + EaReadCost(srcMode, srcReg);
                    break;
                }

                // MOVEM: 0x4880-0x48BF (reg-to-mem) / 0x4C80-0x4CBF (mem-to-reg)
                if ((opcode & 0xFB80) == 0x4880)
                {
                    cycles = 20; // average estimate
                    break;
                }

                // MULU.L / MULS.L: 0x4C00
                if ((opcode & 0xFFC0) == 0x4C00)
                {
                    cycles = 44;
                    break;
                }

                // DIVU.L / DIVS.L: 0x4C40
                if ((opcode & 0xFFC0) == 0x4C40)
                {
                    cycles = 78;
                    break;
                }

                // CHK: 0100 rrr ss 0 mmm rrr (size=11 for .W, size=10 for .L)
                if ((opcode & 0xF040) == 0x4000 && ((opcode >> 7) & 3) >= 2)
                {
                    cycles = 8;
                    break;
                }

                // CLR/NEG/NOT/TST (.L Dn): opcode & 0xFFC0 patterns
                // CLR: 0x4200/.B, 0x4240/.W, 0x4280/.L
                // NEG: 0x4400/.B, 0x4440/.W, 0x4480/.L
                // NOT: 0x4600/.B, 0x4640/.W, 0x4680/.L
                // NEGX:0x4000/.B, 0x4040/.W, 0x4080/.L
                // TST: 0x4A00/.B, 0x4A40/.W, 0x4A80/.L
                {
                    int subOp = (opcode >> 8) & 0xF;
                    if (subOp == 0x2 || subOp == 0x4 || subOp == 0x6 ||
                        subOp == 0x0 || subOp == 0xA)
                    {
                        if (srcMode == 0) { cycles = 2; break; }
                        cycles = 2 + EaReadCost(srcMode, srcReg) + EaWriteCost(srcMode, srcReg);
                        break;
                    }
                }

                // Default for group 4
                cycles = 4 + EaReadCost(srcMode, srcReg);
                break;
            }

            case 0x5: // ADDQ/SUBQ/Scc/DBcc
            {
                int sizeField = (op >> 6) & 3;
                if (sizeField == 3)
                {
                    // DBcc or Scc
                    if (srcMode == 1) { cycles = 6; break; } // DBcc
                    cycles = 2 + EaWriteCost(srcMode, srcReg); // Scc
                    break;
                }
                cycles = 2 + EaReadCost(srcMode, srcReg);
                if (srcMode != 0 && srcMode != 1)
                    cycles += EaWriteCost(srcMode, srcReg);
                break;
            }

            case 0x6: // Bcc/BRA/BSR
            {
                int cond = (op >> 8) & 0xF;
                cycles = (cond == 1) ? 8 : 6; // BSR=8, others=6
                break;
            }

            case 0x7: // MOVEQ
                cycles = 2;
                break;

            case 0x8: // OR/DIVU/DIVS/SBCD
            {
                int opMode = (op >> 6) & 7;
                // DIVU.W: opMode=3
                if (opMode == 3) { cycles = 38; break; }
                // DIVS.W: opMode=7
                if (opMode == 7) { cycles = 38; break; }
                // SBCD: opMode=4, eaMode=0 or 1
                if (opMode == 4 && (srcMode == 0 || srcMode == 1)) { cycles = 4; break; }
                // OR
                cycles = 2 + EaReadCost(srcMode, srcReg);
                if (opMode >= 4 && opMode <= 6 && srcMode != 0) // OR Dn,<ea>
                    cycles += EaWriteCost(srcMode, srcReg);
                break;
            }

            case 0x9: // SUB/SUBA/SUBX
            {
                int opMode = (op >> 6) & 7;
                // SUBX: opMode 4(byte), 5(word), 6(long) with eaMode 0 or 1
                if ((opMode == 4 || opMode == 5 || opMode == 6) &&
                    (srcMode == 0 || srcMode == 1))
                {
                    cycles = 4; break;
                }
                cycles = 2 + EaReadCost(srcMode, srcReg);
                break;
            }

            case 0xA: // Line-A
                cycles = 34;
                break;

            case 0xB: // CMP/CMPA/EOR/CMPM
            {
                int opMode = (op >> 6) & 7;
                // CMPM: opMode 4/5/6 with eaMode=1 (postincrement)
                if ((opMode == 4 || opMode == 5 || opMode == 6) && srcMode == 1)
                {
                    cycles = 12; break;
                }
                // EOR: opMode 4/5/6 with non-An dest
                if (opMode == 4 || opMode == 5 || opMode == 6)
                {
                    cycles = 2 + EaReadCost(srcMode, srcReg) + EaWriteCost(srcMode, srcReg);
                    break;
                }
                // CMP/CMPA
                cycles = 2 + EaReadCost(srcMode, srcReg);
                break;
            }

            case 0xC: // AND/MULU/MULS/EXG/ABCD
            {
                int opMode = (op >> 6) & 7;
                // MULU.W: opMode=3
                if (opMode == 3) { cycles = 28; break; }
                // MULS.W: opMode=7
                if (opMode == 7) { cycles = 28; break; }
                // ABCD: opMode=4, eaMode=0 or 1
                if (opMode == 4 && (srcMode == 0 || srcMode == 1)) { cycles = 4; break; }
                // EXG: opMode=5 (Dn↔Dn or An↔An) or opMode=6 (Dn↔An)  — actually bits[7:3]
                if (opMode == 5 && (srcMode == 0 || srcMode == 1)) { cycles = 4; break; } // EXG
                if (opMode == 6 && srcMode == 1) { cycles = 4; break; } // EXG Dn↔An
                // AND
                cycles = 2 + EaReadCost(srcMode, srcReg);
                if (opMode >= 4 && opMode <= 6 && srcMode != 0)
                    cycles += EaWriteCost(srcMode, srcReg);
                break;
            }

            case 0xD: // ADD/ADDA/ADDX
            {
                int opMode = (op >> 6) & 7;
                // ADDX: opMode 4(byte), 5(word), 6(long) with eaMode 0 or 1
                if ((opMode == 4 || opMode == 5 || opMode == 6) &&
                    (srcMode == 0 || srcMode == 1))
                {
                    cycles = 4; break;
                }
                cycles = 2 + EaReadCost(srcMode, srcReg);
                break;
            }

            case 0xE: // Shifts/Rotates
                cycles = 4;
                break;

            case 0xF: // FPU / coprocessor
                cycles = 40;
                break;
        }

        s_cycleTable[op] = ClampCycles(cycles);
    }
}

// ========================================================================
// Group 0: Bit operations, MOVEP, Immediate operations
// ========================================================================

void InstructionDecoder::DecodeGroup0(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int reg = (opcode >> 9) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if ((opcode & 0x0100) != 0)
    {
        // Bit operations with register
        if (mode == 1)
        {
            DecodeMOVEP(opcode);
            return;
        }
        int bitNum = static_cast<int>(_cpu.D[reg] % 32);
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        int size = (eaMode == AddressingMode::DataRegDirect) ? 4 : 1;

        switch ((opcode >> 6) & 3)
        {
            case 0: // BTST
            {
                uint32_t val = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
                if (size == 1) bitNum %= 8;
                _cpu.SetFlagZ((val & (1u << bitNum)) == 0);
            }
            break;
            case 1: // BCHG
            {
                uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
                if (size == 1) bitNum %= 8;
                _cpu.SetFlagZ((val & (1u << bitNum)) == 0);
                val ^= (1u << bitNum);
                EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, val);
            }
            break;
            case 2: // BCLR
            {
                uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
                if (size == 1) bitNum %= 8;
                _cpu.SetFlagZ((val & (1u << bitNum)) == 0);
                val &= ~(1u << bitNum);
                EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, val);
            }
            break;
            case 3: // BSET
            {
                uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
                if (size == 1) bitNum %= 8;
                _cpu.SetFlagZ((val & (1u << bitNum)) == 0);
                val |= (1u << bitNum);
                EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, val);
            }
            break;
        }
    }
    else
    {
        // Immediate operations or static bit operations
        switch (reg)
        {
            case 0: // ORI or CMP2/CHK2.B
                if (((opcode >> 6) & 3) == 3) DecodeCMP2_CHK2(opcode);
                else DecodeORI(opcode);
                break;
            case 1: // ANDI or CMP2/CHK2.W
                if (((opcode >> 6) & 3) == 3) DecodeCMP2_CHK2(opcode);
                else DecodeANDI(opcode);
                break;
            case 2: // SUBI or CMP2/CHK2.L
                if (((opcode >> 6) & 3) == 3) DecodeCMP2_CHK2(opcode);
                else DecodeSUBI(opcode);
                break;
            case 3: // ADDI
                DecodeADDI(opcode);
                break;
            case 4: // BTST/BCHG/BCLR/BSET #imm
                DecodeStaticBit(opcode, (opcode >> 6) & 3);
                break;
            case 5: // EORI or CAS.B
                if (((opcode >> 6) & 3) == 3)
                    DecodeCAS(opcode);
                else
                    DecodeEORI(opcode);
                break;
            case 6: // CMPI or CAS.W/CAS2.W
                if (((opcode >> 6) & 3) == 3)
                    DecodeCAS(opcode);
                else
                    DecodeCMPI(opcode);
                break;
            case 7: // MOVES or CAS.L/CAS2.L
                if (((opcode >> 6) & 3) == 3)
                    DecodeCAS(opcode);
                else
                    DecodeMOVES(opcode);
                break;
        }
    }
}

// ========================================================================
// MOVEP
// ========================================================================

void InstructionDecoder::DecodeMOVEP(uint16_t opcode)
{
    int dataReg = (opcode >> 9) & 7;
    int addrReg = opcode & 7;
    int16_t disp = static_cast<int16_t>(_cpu.FetchWord());
    uint32_t addr = static_cast<uint32_t>(_cpu.A[addrReg] + disp);
    int opMode = (opcode >> 6) & 7;

    switch (opMode)
    {
        case 4: // MOVEP.W (d,An),Dn - memory to register word
        {
            uint8_t hi = _cpu.ReadByte(addr);
            uint8_t lo = _cpu.ReadByte(addr + 2);
            _cpu.D[dataReg] = (_cpu.D[dataReg] & 0xFFFF0000u) | (static_cast<uint32_t>(hi) << 8) | lo;
        }
        break;
        case 5: // MOVEP.L (d,An),Dn - memory to register long
        {
            uint8_t b3 = _cpu.ReadByte(addr);
            uint8_t b2 = _cpu.ReadByte(addr + 2);
            uint8_t b1 = _cpu.ReadByte(addr + 4);
            uint8_t b0 = _cpu.ReadByte(addr + 6);
            _cpu.D[dataReg] = (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16)
                            | (static_cast<uint32_t>(b1) << 8) | b0;
        }
        break;
        case 6: // MOVEP.W Dn,(d,An) - register to memory word
        {
            _cpu.WriteByte(addr, static_cast<uint8_t>(_cpu.D[dataReg] >> 8));
            _cpu.WriteByte(addr + 2, static_cast<uint8_t>(_cpu.D[dataReg]));
        }
        break;
        case 7: // MOVEP.L Dn,(d,An) - register to memory long
        {
            _cpu.WriteByte(addr, static_cast<uint8_t>(_cpu.D[dataReg] >> 24));
            _cpu.WriteByte(addr + 2, static_cast<uint8_t>(_cpu.D[dataReg] >> 16));
            _cpu.WriteByte(addr + 4, static_cast<uint8_t>(_cpu.D[dataReg] >> 8));
            _cpu.WriteByte(addr + 6, static_cast<uint8_t>(_cpu.D[dataReg]));
        }
        break;
    }
}

// ========================================================================
// ORI
// ========================================================================

void InstructionDecoder::DecodeORI(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    // ORI: destination must be data alterable — An (mode 1) is invalid
    if (mode == 1 || (mode == 7 && reg >= 2 && reg != 4)) {
        _cpu.PC = _cpu._lastPC;
        _cpu.RaiseException(4);
        return;
    }
    int size = GetSize2(opcode);

    if (mode == 7 && reg == 4)
    {
        // ORI to CCR/SR
        uint16_t imm = _cpu.FetchWord();
        if (size == 1)
            _cpu.SetCCRByte(_cpu.GetCCR() | static_cast<uint8_t>(imm & 0xFF));
        else if (size == 2 && _cpu.GetSupervisorMode())
            _cpu.SetSR(static_cast<uint16_t>(_cpu.SR | imm));
        return;
    }

    uint32_t immVal = ReadImmediate(size);
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
    uint32_t result = val | immVal;
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    SetLogicFlags(result, size);
}

// ========================================================================
// ANDI
// ========================================================================

void InstructionDecoder::DecodeANDI(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    // ANDI: destination must be data alterable — An (mode 1) is invalid
    if (mode == 1 || (mode == 7 && reg >= 2 && reg != 4)) {
        _cpu.PC = _cpu._lastPC;
        _cpu.RaiseException(4);
        return;
    }
    int size = GetSize2(opcode);

    if (mode == 7 && reg == 4)
    {
        uint16_t imm = _cpu.FetchWord();
        if (size == 1)
            _cpu.SetCCRByte(_cpu.GetCCR() & static_cast<uint8_t>(imm & 0xFF));
        else if (size == 2 && _cpu.GetSupervisorMode())
            _cpu.SetSR(static_cast<uint16_t>(_cpu.SR & imm));
        return;
    }

    uint32_t immVal = ReadImmediate(size);
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
    uint32_t result = val & immVal;
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    SetLogicFlags(result, size);
}

// ========================================================================
// EORI
// ========================================================================

void InstructionDecoder::DecodeEORI(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    // EORI: destination must be data alterable — An (mode 1) is invalid
    if (mode == 1 || (mode == 7 && reg >= 2 && reg != 4)) {
        _cpu.PC = _cpu._lastPC;
        _cpu.RaiseException(4);
        return;
    }
    int size = GetSize2(opcode);

    if (mode == 7 && reg == 4)
    {
        uint16_t imm = _cpu.FetchWord();
        if (size == 1)
            _cpu.SetCCRByte(_cpu.GetCCR() ^ static_cast<uint8_t>(imm & 0xFF));
        else if (size == 2 && _cpu.GetSupervisorMode())
            _cpu.SetSR(static_cast<uint16_t>(_cpu.SR ^ imm));
        return;
    }

    uint32_t immVal = ReadImmediate(size);
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
    uint32_t result = val ^ immVal;
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    SetLogicFlags(result, size);
}

// ========================================================================
// SUBI
// ========================================================================

void InstructionDecoder::DecodeSUBI(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    // SUBI: destination must be data alterable — An (mode 1) is invalid
    if (mode == 1 || (mode == 7 && reg >= 2)) {
        _cpu.PC = _cpu._lastPC;
        _cpu.RaiseException(4);
        return;
    }
    int size = GetSize2(opcode);
    uint32_t immVal = ReadImmediate(size);
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);

    uint8_t ccr;
    uint32_t result;
    switch (size)
    {
        case 1:
        {
            auto r = Alu::SubByte(static_cast<uint8_t>(val), static_cast<uint8_t>(immVal), _cpu.GetCCR());
            result = r.result; ccr = r.ccr;
        }
        break;
        case 2:
        {
            auto r = Alu::SubWord(static_cast<uint16_t>(val), static_cast<uint16_t>(immVal), _cpu.GetCCR());
            result = r.result; ccr = r.ccr;
        }
        break;
        default:
        {
            auto r = Alu::SubLong(val, immVal, _cpu.GetCCR());
            result = r.result; ccr = r.ccr;
        }
        break;
    }
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    _cpu.SetCCR(ccr);
}

// ========================================================================
// ADDI
// ========================================================================

void InstructionDecoder::DecodeADDI(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    // ADDI: destination must be data alterable — An (mode 1) is invalid
    if (mode == 1 || (mode == 7 && reg >= 2)) {
        _cpu.PC = _cpu._lastPC;
        _cpu.RaiseException(4);
        return;
    }
    int size = GetSize2(opcode);
    uint32_t immVal = ReadImmediate(size);
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);

    uint8_t ccr;
    uint32_t result;
    switch (size)
    {
        case 1:
        {
            auto r = Alu::AddByte(static_cast<uint8_t>(val), static_cast<uint8_t>(immVal), _cpu.GetCCR());
            result = r.result; ccr = r.ccr;
        }
        break;
        case 2:
        {
            auto r = Alu::AddWord(static_cast<uint16_t>(val), static_cast<uint16_t>(immVal), _cpu.GetCCR());
            result = r.result; ccr = r.ccr;
        }
        break;
        default:
        {
            auto r = Alu::AddLong(val, immVal, _cpu.GetCCR());
            result = r.result; ccr = r.ccr;
        }
        break;
    }
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    _cpu.SetCCR(ccr);
}

// ========================================================================
// CMPI
// ========================================================================

void InstructionDecoder::DecodeCMPI(uint16_t opcode)
{
    int size = GetSize2(opcode);
    uint32_t immVal = ReadImmediate(size);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);

    uint8_t ccr;
    switch (size)
    {
        case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(val), static_cast<uint8_t>(immVal), _cpu.GetCCR()); ccr = r.ccr; } break;
        case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(val), static_cast<uint16_t>(immVal), _cpu.GetCCR()); ccr = r.ccr; } break;
        default: { auto r = Alu::SubLong(val, immVal, _cpu.GetCCR()); ccr = r.ccr; } break;
    }
    _cpu.UpdateCCR(ccr, 0x0F); // Don't update X
}

// ========================================================================
// Static Bit operations (BTST/BCHG/BCLR/BSET #imm)
// ========================================================================

void InstructionDecoder::DecodeStaticBit(uint16_t opcode, int op)
{
    int bitNum = _cpu.FetchWord() & 0xFF;
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    int size = (eaMode == AddressingMode::DataRegDirect) ? 4 : 1;
    if (size == 1) bitNum %= 8;
    else bitNum %= 32;

    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
    _cpu.SetFlagZ((val & (1u << bitNum)) == 0);

    switch (op)
    {
        case 1: val ^= (1u << bitNum); EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, val); break;
        case 2: val &= ~(1u << bitNum); EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, val); break;
        case 3: val |= (1u << bitNum); EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, val); break;
    }
}

// ========================================================================
// CMP2/CHK2
// ========================================================================

void InstructionDecoder::DecodeCMP2_CHK2(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t ext = _cpu.FetchWord();
    int size = GetSize2(opcode);
    int rn = (ext >> 12) & 0xF;
    bool isChk = (ext & 0x0800) != 0;

    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, size);
    uint32_t lower, upper;
    if (size == 1)
    {
        lower = _cpu.ReadByte(addr);
        upper = _cpu.ReadByte(addr + 1);
    }
    else if (size == 2)
    {
        lower = _cpu.ReadWord(addr);
        upper = _cpu.ReadWord(addr + 2);
    }
    else
    {
        lower = _cpu.ReadLong(addr);
        upper = _cpu.ReadLong(addr + 4);
    }

    uint32_t val = rn < 8 ? _cpu.D[rn] : _cpu.A[rn - 8];
    if (size == 1) val &= 0xFF;
    else if (size == 2) val &= 0xFFFF;

    _cpu.SetFlagZ(val == lower || val == upper);
    _cpu.SetFlagC(val < lower || val > upper);

    if (isChk && _cpu.GetFlagC())
        _cpu.RaiseException(6); // CHK exception
}

// ========================================================================
// MOVES
// ========================================================================

void InstructionDecoder::DecodeMOVES(uint16_t opcode)
{
    if (!_cpu.GetSupervisorMode())
    {
        _cpu.RaiseException(8); // Privilege violation
        return;
    }
    int size = GetSize2(opcode);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t ext = _cpu.FetchWord();
    int rn = (ext >> 12) & 0xF;
    bool toMem = (ext & 0x0800) != 0;

    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);

    // MOVES uses DFC for writes to memory and SFC for reads from memory.
    // Invalidate data page cache before FC override (cache entry is for current FC)
    _cpu.InvalidateDataCache();
    if (toMem)
    {
        _cpu.FunctionCodeOverride = static_cast<int>(_cpu.DFC & 7);
        uint32_t val = rn < 8 ? _cpu.D[rn] : _cpu.A[rn - 8];
        EffectiveAddress::WriteValue(_cpu, eaMode, eaR, size, val);
    }
    else
    {
        _cpu.FunctionCodeOverride = static_cast<int>(_cpu.SFC & 7);
        uint32_t val = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
        // Skip destination register commit if the read itself faulted.
        if (!_cpu.BusErrorPending)
        {
            if (rn < 8)
            {
                switch (size)
                {
                    case 1: _cpu.D[rn] = (_cpu.D[rn] & 0xFFFFFF00u) | (val & 0xFF); break;
                    case 2: _cpu.D[rn] = (_cpu.D[rn] & 0xFFFF0000u) | (val & 0xFFFF); break;
                    default: _cpu.D[rn] = val; break;
                }
            }
            else
            {
                // Word-size loads to address registers must be sign-extended
                _cpu.A[rn - 8] = (size == 2) ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(val)))) : val;
            }
        }
    }
    // Always reset — this path replaces the prior try/catch-as-finally.
    _cpu.FunctionCodeOverride = -1;
}

// ========================================================================
// MOVE size wrappers (opcode table dispatch)
// ========================================================================

void InstructionDecoder::DecodeMoveB(uint16_t opcode) { _cpu.EnsureRegSnapshot(); DecodeMOVE(opcode, 1); }
void InstructionDecoder::DecodeMoveL(uint16_t opcode) { _cpu.EnsureRegSnapshot(); DecodeMOVE(opcode, 4); }
void InstructionDecoder::DecodeMoveW(uint16_t opcode) { _cpu.EnsureRegSnapshot(); DecodeMOVE(opcode, 2); }

// ========================================================================
// MOVE (Groups 1, 2, 3)
// ========================================================================

void InstructionDecoder::DecodeMOVE(uint16_t opcode, int size)
{
    int srcMode = (opcode >> 3) & 7;
    int srcReg = opcode & 7;
    int dstReg = (opcode >> 9) & 7;
    int dstMode = (opcode >> 6) & 7;

    auto [srcEaMode, srcEaR] = EffectiveAddress::Decode(srcMode, srcReg);
    uint32_t value = EffectiveAddress::ReadValue(_cpu, srcEaMode, srcEaR, size);

    // Check for MOVEA
    if (dstMode == 1)
    {
        // MOVEA - no flags affected
        _cpu.A[dstReg] = size == 2
            ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(value))))
            : value;
        return;
    }

    auto [dstEaMode, dstEaR] = EffectiveAddress::Decode(dstMode, dstReg);
    EffectiveAddress::WriteValue(_cpu, dstEaMode, dstEaR, size, value);
    SetLogicFlags(value, size);
}

// ========================================================================
// Group 4: Miscellaneous
// ========================================================================

void InstructionDecoder::DecodeGroup4(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    // MOVE from SR
    if ((opcode & 0xFFC0) == 0x40C0)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        EffectiveAddress::WriteValue(_cpu, eaMode, eaR, 2, _cpu.SR);
        return;
    }

    // MOVE to CCR
    if ((opcode & 0xFFC0) == 0x44C0)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t val = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2);
        _cpu.SetCCRByte(static_cast<uint8_t>(val & 0xFF));
        return;
    }

    // MOVE to SR
    if ((opcode & 0xFFC0) == 0x46C0)
    {
        if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t val = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2);
        _cpu.SetSR(static_cast<uint16_t>(val));
        return;
    }

    // NEGX
    if ((opcode & 0xFF00) == 0x4000 && ((opcode >> 6) & 3) != 3)
    { DecodeNEGX(opcode); return; }

    // CLR
    if ((opcode & 0xFF00) == 0x4200 && ((opcode >> 6) & 3) != 3)
    { DecodeCLR(opcode); return; }

    // NEG
    if ((opcode & 0xFF00) == 0x4400 && ((opcode >> 6) & 3) != 3)
    { DecodeNEG(opcode); return; }

    // NOT
    if ((opcode & 0xFF00) == 0x4600 && ((opcode >> 6) & 3) != 3)
    { DecodeNOT(opcode); return; }

    // EXT / EXTB
    if ((opcode & 0xFFF8) == 0x4880) { ExtWord(opcode & 7); return; }
    if ((opcode & 0xFFF8) == 0x48C0) { ExtLong(opcode & 7); return; }
    if ((opcode & 0xFFF8) == 0x49C0) { ExtByteLong(opcode & 7); return; }

    // SWAP
    if ((opcode & 0xFFF8) == 0x4840)
    {
        int reg = opcode & 7;
        _cpu.D[reg] = (_cpu.D[reg] >> 16) | (_cpu.D[reg] << 16);
        SetLogicFlags(_cpu.D[reg], 4);
        return;
    }

    // PEA
    if ((opcode & 0xFFC0) == 0x4840)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
        _cpu.PushLong(addr);
        return;
    }

    // TST
    if ((opcode & 0xFF00) == 0x4A00 && ((opcode >> 6) & 3) != 3)
    {
        int size = GetSize2(opcode);
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t val = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
        SetLogicFlags(val, size);
        return;
    }

    // TAS
    if ((opcode & 0xFFC0) == 0x4AC0)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, 1);
        SetLogicFlags(val, 1);
        val |= 0x80;
        EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, 1, val);
        return;
    }

    // ILLEGAL
    if (opcode == 0x4AFC) { _cpu.RaiseException(4); return; }

    // MOVEM
    if ((opcode & 0xFB80) == 0x4880) { DecodeMOVEM(opcode); return; }

    // LEA
    if ((opcode & 0xF1C0) == 0x41C0)
    {
        int areg = (opcode >> 9) & 7;
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
        _cpu.A[areg] = addr;
        return;
    }

    // CHK
    if ((opcode & 0xF1C0) == 0x4180)
    {
        int dreg = (opcode >> 9) & 7;
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t bound = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2);
        int16_t val = static_cast<int16_t>(static_cast<uint16_t>(_cpu.D[dreg]));
        if (val < 0 || val > static_cast<int16_t>(static_cast<uint16_t>(bound)))
        {
            _cpu.SetFlagN(val < 0);
            _cpu.RaiseException(6);
        }
        return;
    }

    // LINK / UNLK
    if ((opcode & 0xFFF8) == 0x4E50) { DecodeLINK(opcode, false); return; }
    if ((opcode & 0xFFF8) == 0x4808) { DecodeLINK(opcode, true); return; }
    if ((opcode & 0xFFF8) == 0x4E58) { DecodeUNLK(opcode); return; }

    // MOVE USP
    if ((opcode & 0xFFF0) == 0x4E60)
    {
        if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
        int areg = opcode & 7;
        if ((opcode & 0x0008) != 0) _cpu.A[areg] = _cpu.USP;
        else _cpu.USP = _cpu.A[areg];
        return;
    }

    // RESET — asserts RSTO to reset external devices; CPU state is NOT modified
    if (opcode == 0x4E70) { if (_cpu.GetSupervisorMode()) { if (_cpu.DiagnosticOutput) _cpu.DiagnosticOutput(std::format("\n[EMU] RESET instruction at PC=${:08X}\n", _cpu.PC - 2)); if (_cpu.OnResetInstruction) _cpu.OnResetInstruction(); } else _cpu.RaiseException(8); return; }

    // NOP
    if (opcode == 0x4E71) return;

    // STOP
    if (opcode == 0x4E72)
    {
        if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
        uint16_t imm = _cpu.FetchWord();
        _cpu.SetSR(imm);
        _cpu.Stopped = true;
        _cpu.StopReason = "STOP instruction";
        _cpu._stopEnteredTime = std::chrono::steady_clock::now();
        _cpu._stopTimingActive = true;
        return;
    }

    // RTE
    if (opcode == 0x4E73)
    {
        if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
        uint16_t newSR = _cpu.PopWord();
        uint32_t newPC = _cpu.PopLong();
        uint16_t frameWord = _cpu.PopWord();
        int format = (frameWord >> 12) & 0xF;
        switch (format)
        {
            case 0x0: break;                    // Format 0: 4-word frame
            case 0x1: break;                    // Format 1: throwaway (68010)
            case 0x2: _cpu.A[7] += 4; break;   // Format 2: 6-word + instruction address
            case 0x9: _cpu.A[7] += 12; break;  // Format 9: coprocessor mid-instruction
            case 0xA: _cpu.A[7] += 24; break;  // Format A: short bus fault (16 words total)
            case 0xB: _cpu.A[7] += 84; break;  // Format B: long bus fault
            default:
                _cpu.RaiseException(14);
                return;
        }
        _cpu.SetSR(newSR);
        _cpu.PC = newPC;
        _cpu.ShadowPop();
        return;
    }

    // RTD
    if (opcode == 0x4E74)
    {
        int16_t disp = static_cast<int16_t>(_cpu.FetchWord());
        _cpu.PC = _cpu.PopLong();
        _cpu.A[7] += static_cast<uint32_t>(disp);
        _cpu.ShadowPop();
        return;
    }

    // RTS
    if (opcode == 0x4E75) { _cpu.PC = _cpu.PopLong(); _cpu.ShadowPop(); return; }

    // RTR
    if (opcode == 0x4E77)
    {
        _cpu.SetCCRByte(static_cast<uint8_t>(_cpu.PopWord()));
        _cpu.PC = _cpu.PopLong();
        _cpu.ShadowPop();
        return;
    }

    // MOVEC
    if ((opcode & 0xFFFE) == 0x4E7A) { DecodeMOVEC(opcode); return; }

    // TRAP
    if ((opcode & 0xFFF0) == 0x4E40) { _cpu.RaiseTrap(opcode & 0xF); return; }

    // JSR
    if ((opcode & 0xFFC0) == 0x4E80)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
        _cpu.PushLong(_cpu.PC);
        _cpu.ShadowPush(_cpu._lastPC, addr, _cpu.PC);
        _cpu.PC = addr;
        return;
    }

    // JMP
    if ((opcode & 0xFFC0) == 0x4EC0)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
        _cpu.PC = addr;
        return;
    }

    // NBCD
    if ((opcode & 0xFFC0) == 0x4800)
    {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint8_t val = static_cast<uint8_t>(EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, 1));
        auto [result, ccr] = Alu::SubBcd(val, 0, _cpu.GetCCR());
        EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, 1, result);
        _cpu.SetCCR(ccr);
        return;
    }

    // MULS.L / MULU.L
    if ((opcode & 0xFFC0) == 0x4C00) { DecodeLongMul(opcode); return; }

    // DIVS.L / DIVU.L
    if ((opcode & 0xFFC0) == 0x4C40) { DecodeLongDiv(opcode); return; }

    // Unimplemented
    _cpu.RaiseException(4);
}

void InstructionDecoder::DecodeNEGX(uint16_t opcode)
{
    int size = GetSize2(opcode);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);

    uint8_t ccr;
    uint32_t result;
    switch (size)
    {
        case 1: { auto r = Alu::SubByte(0, static_cast<uint8_t>(val), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
        case 2: { auto r = Alu::SubWord(0, static_cast<uint16_t>(val), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
        default: { auto r = Alu::SubLong(0, val, _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
    }
    if ((ccr & 0x04) != 0) ccr = static_cast<uint8_t>((ccr & ~0x04) | (_cpu.GetCCR() & 0x04));
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    _cpu.SetCCR(ccr);
}

void InstructionDecoder::DecodeCLR(uint16_t opcode)
{
    int size = GetSize2(opcode);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    EffectiveAddress::WriteValue(_cpu, eaMode, eaR, size, 0);
    _cpu.SetCCRByte(static_cast<uint8_t>((_cpu.GetCCR() & 0x10) | 0x04));
}

void InstructionDecoder::DecodeNEG(uint16_t opcode)
{
    int size = GetSize2(opcode);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);

    uint8_t ccr;
    uint32_t result;
    switch (size)
    {
        case 1: { auto r = Alu::SubByte(0, static_cast<uint8_t>(val), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
        case 2: { auto r = Alu::SubWord(0, static_cast<uint16_t>(val), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
        default: { auto r = Alu::SubLong(0, val, _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
    }
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    _cpu.SetCCR(ccr);
}

void InstructionDecoder::DecodeNOT(uint16_t opcode)
{
    int size = GetSize2(opcode);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
    uint32_t result = ~val;
    EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
    SetLogicFlags(result, size);
}

void InstructionDecoder::ExtWord(int reg)
{
    int16_t val = static_cast<int16_t>(static_cast<int8_t>(static_cast<uint8_t>(_cpu.D[reg])));
    _cpu.D[reg] = (_cpu.D[reg] & 0xFFFF0000u) | static_cast<uint32_t>(static_cast<uint16_t>(val));
    SetLogicFlags(static_cast<uint32_t>(static_cast<uint16_t>(val)), 2);
}

void InstructionDecoder::ExtLong(int reg)
{
    int32_t val = static_cast<int16_t>(static_cast<uint16_t>(_cpu.D[reg]));
    _cpu.D[reg] = static_cast<uint32_t>(val);
    SetLogicFlags(_cpu.D[reg], 4);
}

void InstructionDecoder::ExtByteLong(int reg)
{
    int32_t val = static_cast<int8_t>(static_cast<uint8_t>(_cpu.D[reg]));
    _cpu.D[reg] = static_cast<uint32_t>(val);
    SetLogicFlags(_cpu.D[reg], 4);
}

void InstructionDecoder::DecodeMOVEM(uint16_t opcode)
{
    bool isLong = (opcode & 0x0040) != 0;
    int size = isLong ? 4 : 2;
    bool toRegs = (opcode & 0x0400) != 0;
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t mask = _cpu.FetchWord();

    if (toRegs)
    {
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr;
        if (eaMode == AddressingMode::AddrRegPostInc)
            addr = _cpu.A[eaR];
        else
            addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, size);

        for (int i = 0; i < 16; i++)
        {
            if ((mask & (1 << i)) != 0)
            {
                if (i < 8)
                    _cpu.D[i] = isLong ? _cpu.ReadLong(addr)
                        : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(_cpu.ReadWord(addr))));
                else
                    _cpu.A[i - 8] = isLong ? _cpu.ReadLong(addr)
                        : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(_cpu.ReadWord(addr))));
                addr += static_cast<uint32_t>(size);
            }
        }
        if (eaMode == AddressingMode::AddrRegPostInc)
            _cpu.A[eaR] = addr;
    }
    else
    {
        if (mode == 4) // -(An) predecrement
        {
            uint32_t addr = _cpu.A[reg];
            for (int i = 0; i < 16; i++)
            {
                if ((mask & (1 << i)) != 0)
                {
                    addr -= static_cast<uint32_t>(size);
                    int realReg = 15 - i;
                    uint32_t val;
                    if (realReg < 8)
                        val = _cpu.D[realReg];
                    else if (realReg - 8 == reg)
                        // MC68020/30/40: the base register value saved is
                        // the initial value minus the operand size
                        val = _cpu.A[reg] - static_cast<uint32_t>(size);
                    else
                        val = _cpu.A[realReg - 8];
                    if (isLong) _cpu.WriteLong(addr, val);
                    else _cpu.WriteWord(addr, static_cast<uint16_t>(val));
                }
            }
            _cpu.A[reg] = addr;
        }
        else
        {
            auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
            uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, size);

            for (int i = 0; i < 16; i++)
            {
                if ((mask & (1 << i)) != 0)
                {
                    uint32_t val = (i < 8) ? _cpu.D[i] : _cpu.A[i - 8];
                    if (isLong) _cpu.WriteLong(addr, val);
                    else _cpu.WriteWord(addr, static_cast<uint16_t>(val));
                    addr += static_cast<uint32_t>(size);
                }
            }
        }
    }
}

void InstructionDecoder::DecodeLINK(uint16_t opcode, bool longDisp)
{
    int reg = opcode & 7;
    _cpu.PushLong(_cpu.A[reg]);
    _cpu.A[reg] = _cpu.A[7];
    if (longDisp)
    {
        int32_t disp = static_cast<int32_t>(_cpu.FetchLong());
        _cpu.A[7] = static_cast<uint32_t>(_cpu.A[7] + disp);
    }
    else
    {
        int16_t disp = static_cast<int16_t>(_cpu.FetchWord());
        _cpu.A[7] = static_cast<uint32_t>(_cpu.A[7] + disp);
    }
}

void InstructionDecoder::DecodeUNLK(uint16_t opcode)
{
    int reg = opcode & 7;
    _cpu.A[7] = _cpu.A[reg];
    _cpu.A[reg] = _cpu.PopLong();
}

void InstructionDecoder::DecodeMOVEC(uint16_t opcode)
{
    if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
    uint16_t ext = _cpu.FetchWord();
    int rn = (ext >> 12) & 0xF;
    int creg = ext & 0xFFF;
    bool toCtrl = (opcode & 1) != 0;

    if (toCtrl)
    {
        uint32_t val = rn < 8 ? _cpu.D[rn] : _cpu.A[rn - 8];
        switch (creg)
        {
            case 0x000: _cpu.SFC = val & 7; break;
            case 0x001: _cpu.DFC = val & 7; break;
            case 0x002: _cpu.CACR = val; break;
            case 0x800: _cpu.USP = val; break;
            case 0x801: _cpu.VBR = val; break;
            case 0x802: _cpu.CAAR = val; break;
            case 0x803: /* MSP */ break;
            case 0x804: /* ISP */ break;
        }
    }
    else
    {
        uint32_t val;
        switch (creg)
        {
            case 0x000: val = _cpu.SFC; break;
            case 0x001: val = _cpu.DFC; break;
            case 0x002: val = _cpu.CACR; break;
            case 0x800: val = _cpu.USP; break;
            case 0x801: val = _cpu.VBR; break;
            case 0x802: val = _cpu.CAAR; break;
            default: val = 0; break;
        }
        if (rn < 8) _cpu.D[rn] = val;
        else _cpu.A[rn - 8] = val;
    }
}

// ========================================================================
// Group 5: ADDQ/SUBQ/Scc/DBcc/TRAPcc
// ========================================================================

void InstructionDecoder::DecodeGroup5(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int sizeField = (opcode >> 6) & 3;

    if (sizeField == 3)
    {
        int cond = (opcode >> 8) & 0xF;
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;

        if (mode == 1)
        {
            // DBcc
            bool cc = _cpu.EvaluateCondition(cond);
            int16_t disp = static_cast<int16_t>(_cpu.FetchWord());
            if (!cc)
            {
                int16_t count = static_cast<int16_t>(static_cast<uint16_t>(_cpu.D[reg] & 0xFFFF));
                count--;
                _cpu.D[reg] = (_cpu.D[reg] & 0xFFFF0000u) | static_cast<uint32_t>(static_cast<uint16_t>(count));
                if (count != -1)
                    _cpu.PC = static_cast<uint32_t>(_cpu.PC - 2 + disp);
            }
            return;
        }

        if (mode == 7 && reg >= 2 && reg <= 4)
        {
            // TRAPcc
            bool cc = _cpu.EvaluateCondition(cond);
            if (reg == 2) _cpu.FetchWord();
            else if (reg == 3) _cpu.FetchLong();
            if (cc) _cpu.RaiseException(7);
            return;
        }

        // Scc
        {
            bool cc = _cpu.EvaluateCondition(cond);
            auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
            EffectiveAddress::WriteValue(_cpu, eaMode, eaR, 1, cc ? 0xFFu : 0x00u);
        }
        return;
    }

    // ADDQ / SUBQ
    int data = (opcode >> 9) & 7;
    if (data == 0) data = 8;
    int size;
    switch (sizeField) { case 0: size = 1; break; case 1: size = 2; break; default: size = 4; break; }
    int eaMode2 = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if ((opcode & 0x0100) == 0)
    {
        // ADDQ
        if (eaMode2 == 1) { _cpu.A[eaReg] += static_cast<uint32_t>(data); return; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(eaMode2, eaReg);
        uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
        uint8_t ccr;
        uint32_t result;
        switch (size)
        {
            case 1: { auto r = Alu::AddByte(static_cast<uint8_t>(val), static_cast<uint8_t>(data), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            case 2: { auto r = Alu::AddWord(static_cast<uint16_t>(val), static_cast<uint16_t>(data), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            default: { auto r = Alu::AddLong(val, static_cast<uint32_t>(data), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
        }
        EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
        _cpu.SetCCR(ccr);
    }
    else
    {
        // SUBQ
        if (eaMode2 == 1) { _cpu.A[eaReg] -= static_cast<uint32_t>(data); return; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(eaMode2, eaReg);
        uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
        uint8_t ccr;
        uint32_t result;
        switch (size)
        {
            case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(val), static_cast<uint8_t>(data), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(val), static_cast<uint16_t>(data), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            default: { auto r = Alu::SubLong(val, static_cast<uint32_t>(data), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
        }
        EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
        _cpu.SetCCR(ccr);
    }
}

// ========================================================================
// Group 6: Bcc/BSR/BRA
// ========================================================================

void InstructionDecoder::DecodeGroup6(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int cond = (opcode >> 8) & 0xF;
    int disp8 = static_cast<int8_t>(opcode & 0xFF);
    uint32_t savedPC = _cpu.PC;

    int displacement;
    if (disp8 == 0)
        displacement = static_cast<int16_t>(_cpu.FetchWord());
    else if (disp8 == -1)
        displacement = static_cast<int32_t>(_cpu.FetchLong());
    else
        displacement = disp8;

    uint32_t targetPC = static_cast<uint32_t>(savedPC + displacement);

    switch (cond)
    {
        case 0: // BRA
            _cpu.PC = targetPC;
            break;
        case 1: // BSR
            _cpu.PushLong(_cpu.PC);
            _cpu.ShadowPush(_cpu._lastPC, targetPC, _cpu.PC);
            _cpu.PC = targetPC;
            break;
        default: // Bcc
            if (_cpu.EvaluateCondition(cond))
                _cpu.PC = targetPC;
            break;
    }
}

// ========================================================================
// Group 7: MOVEQ
// ========================================================================

void InstructionDecoder::DecodeMOVEQ(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    if ((opcode & 0x0100) != 0) { _cpu.RaiseException(4); return; }
    int reg = (opcode >> 9) & 7;
    int data = static_cast<int8_t>(opcode & 0xFF);
    _cpu.D[reg] = static_cast<uint32_t>(data);
    SetLogicFlags(_cpu.D[reg], 4);
}

// ========================================================================
// Group 8: OR/DIV/SBCD
// ========================================================================

void InstructionDecoder::DecodeGroup8(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    // DIVU.W
    if (opMode == 3)
    {
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2) & 0xFFFF;
        if (src == 0) { _cpu.RaiseException(5); return; }
        auto [quot, rem, ccr, overflow] = Alu::DivUnsigned(_cpu.D[reg], static_cast<uint32_t>(static_cast<uint16_t>(src)));
        if (overflow) { _cpu.SetFlagV(true); return; }
        _cpu.D[reg] = (rem << 16) | (quot & 0xFFFF);
        _cpu.UpdateCCR(ccr, 0x0F);
        return;
    }

    // DIVS.W
    if (opMode == 7)
    {
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2);
        int16_t divisor = static_cast<int16_t>(static_cast<uint16_t>(src));
        if (divisor == 0) { _cpu.RaiseException(5); return; }
        auto [quot, rem, ccr, overflow] = Alu::DivSigned(static_cast<int32_t>(_cpu.D[reg]), divisor);
        if (overflow) { _cpu.SetFlagV(true); return; }
        _cpu.D[reg] = (rem << 16) | (quot & 0xFFFF);
        _cpu.UpdateCCR(ccr, 0x0F);
        return;
    }

    // SBCD
    if (opMode == 4 && (mode == 0 || mode == 1))
    {
        if (mode == 0)
        {
            uint8_t src = static_cast<uint8_t>(_cpu.D[eaReg]);
            uint8_t dst = static_cast<uint8_t>(_cpu.D[reg]);
            auto [result, ccr] = Alu::SubBcd(src, dst, _cpu.GetCCR());
            _cpu.D[reg] = (_cpu.D[reg] & 0xFFFFFF00u) | result;
            _cpu.SetCCR(ccr);
        }
        else
        {
            _cpu.A[eaReg]--;
            _cpu.A[reg]--;
            uint8_t src = _cpu.ReadByte(_cpu.A[eaReg]);
            uint8_t dst = _cpu.ReadByte(_cpu.A[reg]);
            auto [result, ccr] = Alu::SubBcd(src, dst, _cpu.GetCCR());
            _cpu.WriteByte(_cpu.A[reg], result);
            _cpu.SetCCR(ccr);
        }
        return;
    }

    // PACK (68020+)
    if (opMode == 5 && (mode == 0 || mode == 1))
    {
        uint16_t adj = _cpu.FetchWord();
        uint32_t src;
        if (mode == 0)
            src = _cpu.D[eaReg];
        else
        {
            _cpu.A[eaReg] -= 2;
            src = _cpu.ReadWord(_cpu.A[eaReg]);
        }
        uint32_t result = static_cast<uint32_t>(((src + adj) >> 4) & 0xF0) | static_cast<uint32_t>((src + adj) & 0x0F);
        if (mode == 0)
            _cpu.D[reg] = (_cpu.D[reg] & 0xFFFFFF00u) | (result & 0xFF);
        else
        {
            _cpu.A[reg]--;
            _cpu.WriteByte(_cpu.A[reg], static_cast<uint8_t>(result));
        }
        return;
    }

    // UNPK (68020+)
    if (opMode == 6 && (mode == 0 || mode == 1))
    {
        uint16_t adj = _cpu.FetchWord();
        uint32_t src;
        if (mode == 0)
            src = _cpu.D[eaReg] & 0xFF;
        else
        {
            _cpu.A[eaReg]--;
            src = _cpu.ReadByte(_cpu.A[eaReg]);
        }
        uint32_t result = static_cast<uint32_t>(((src & 0xF0) << 4) | (src & 0x0F)) + adj;
        if (mode == 0)
            _cpu.D[reg] = (_cpu.D[reg] & 0xFFFF0000u) | (result & 0xFFFF);
        else
        {
            _cpu.A[reg] -= 2;
            _cpu.WriteWord(_cpu.A[reg], static_cast<uint16_t>(result));
        }
        return;
    }

    // OR
    {
        int size;
        switch (opMode) { case 0: case 4: size = 1; break; case 1: case 5: size = 2; break; case 2: case 6: size = 4; break; default: size = 2; break; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        bool toEa = (opMode & 4) != 0;

        if (toEa)
        {
            uint32_t src = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
            uint32_t result = src | _cpu.D[reg];
            EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
            SetLogicFlags(result, size);
        }
        else
        {
            uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
            uint32_t result = _cpu.D[reg] | src;
            switch (size)
            {
                case 1: _cpu.D[reg] = (_cpu.D[reg] & 0xFFFFFF00u) | (result & 0xFF); break;
                case 2: _cpu.D[reg] = (_cpu.D[reg] & 0xFFFF0000u) | (result & 0xFFFF); break;
                default: _cpu.D[reg] = result; break;
            }
            SetLogicFlags(result, size);
        }
    }
}

// ========================================================================
// Group 9: SUB/SUBA/SUBX
// ========================================================================

void InstructionDecoder::DecodeGroup9(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    // SUBA
    if (opMode == 3 || opMode == 7)
    {
        int size = opMode == 3 ? 2 : 4;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
        if (size == 2) src = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(src))));
        _cpu.A[reg] -= src;
        return;
    }

    // SUBX
    if ((opMode == 4 || opMode == 5 || opMode == 6) && (mode == 0 || mode == 1))
    {
        int size;
        switch (opMode) { case 4: size = 1; break; case 5: size = 2; break; default: size = 4; break; }
        if (mode == 0)
        {
            uint32_t a = ReadRegValue(_cpu.D[reg], size);
            uint32_t b = ReadRegValue(_cpu.D[eaReg], size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(a), static_cast<uint8_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(a), static_cast<uint16_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::SubLong(a, b, _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
            }
            if ((ccr & 0x04) != 0) ccr = static_cast<uint8_t>((ccr & ~0x04) | (_cpu.GetCCR() & 0x04));
            WriteRegValue(_cpu.D[reg], result, size);
            _cpu.SetCCR(ccr);
        }
        else
        {
            // -(An) mode
            int s = size; if (eaReg == 7 && size == 1) s = 2;
            _cpu.A[eaReg] -= static_cast<uint32_t>(s);
            _cpu.A[reg] -= static_cast<uint32_t>(s);
            uint32_t b = ReadMemValue(_cpu.A[eaReg], size);
            uint32_t a = ReadMemValue(_cpu.A[reg], size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(a), static_cast<uint8_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(a), static_cast<uint16_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::SubLong(a, b, _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
            }
            if ((ccr & 0x04) != 0) ccr = static_cast<uint8_t>((ccr & ~0x04) | (_cpu.GetCCR() & 0x04));
            WriteMemValue(_cpu.A[reg], result, size);
            _cpu.SetCCR(ccr);
        }
        return;
    }

    // SUB
    {
        int size;
        switch (opMode) { case 0: case 4: size = 1; break; case 1: case 5: size = 2; break; case 2: case 6: size = 4; break; default: size = 2; break; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        bool toEa = (opMode & 4) != 0;

        if (toEa)
        {
            uint32_t dst = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(dst), static_cast<uint8_t>(_cpu.D[reg]), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(dst), static_cast<uint16_t>(_cpu.D[reg]), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::SubLong(dst, _cpu.D[reg], _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            }
            EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
            _cpu.SetCCR(ccr);
        }
        else
        {
            uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
            uint32_t dst = ReadRegValue(_cpu.D[reg], size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(dst), static_cast<uint8_t>(src), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(dst), static_cast<uint16_t>(src), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::SubLong(dst, src, _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            }
            WriteRegValue(_cpu.D[reg], result, size);
            _cpu.SetCCR(ccr);
        }
    }
}

// ========================================================================
// Group B: CMP/EOR/CMPA/CMPM
// ========================================================================

void InstructionDecoder::DecodeGroupB(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    // CMPA
    if (opMode == 3 || opMode == 7)
    {
        int size = opMode == 3 ? 2 : 4;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
        if (size == 2) src = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(src))));
        auto r = Alu::SubLong(_cpu.A[reg], src, _cpu.GetCCR());
        _cpu.UpdateCCR(r.ccr, 0x0F);
        return;
    }

    // CMPM
    if ((opMode == 4 || opMode == 5 || opMode == 6) && mode == 1)
    {
        int size;
        switch (opMode) { case 4: size = 1; break; case 5: size = 2; break; default: size = 4; break; }
        int inc = size; if (eaReg == 7 && size == 1) inc = 2;
        uint32_t src = ReadMemValue(_cpu.A[eaReg], size);
        _cpu.A[eaReg] += static_cast<uint32_t>(inc);
        inc = size; if (reg == 7 && size == 1) inc = 2;
        uint32_t dst = ReadMemValue(_cpu.A[reg], size);
        _cpu.A[reg] += static_cast<uint32_t>(inc);

        uint8_t ccr;
        switch (size)
        {
            case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(dst), static_cast<uint8_t>(src), _cpu.GetCCR()); ccr = r.ccr; } break;
            case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(dst), static_cast<uint16_t>(src), _cpu.GetCCR()); ccr = r.ccr; } break;
            default: { auto r = Alu::SubLong(dst, src, _cpu.GetCCR()); ccr = r.ccr; } break;
        }
        _cpu.UpdateCCR(ccr, 0x0F);
        return;
    }

    // EOR
    if (opMode >= 4 && opMode <= 6)
    {
        int size;
        switch (opMode) { case 4: size = 1; break; case 5: size = 2; break; default: size = 4; break; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t dst = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
        uint32_t result = dst ^ _cpu.D[reg];
        EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
        SetLogicFlags(result, size);
        return;
    }

    // CMP
    {
        int size;
        switch (opMode) { case 0: size = 1; break; case 1: size = 2; break; default: size = 4; break; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
        uint32_t dst = ReadRegValue(_cpu.D[reg], size);

        uint8_t ccr;
        switch (size)
        {
            case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(dst), static_cast<uint8_t>(src), _cpu.GetCCR()); ccr = r.ccr; } break;
            case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(dst), static_cast<uint16_t>(src), _cpu.GetCCR()); ccr = r.ccr; } break;
            default: { auto r = Alu::SubLong(dst, src, _cpu.GetCCR()); ccr = r.ccr; } break;
        }
        _cpu.UpdateCCR(ccr, 0x0F);
    }
}

// ========================================================================
// Group C: AND/MUL/ABCD/EXG
// ========================================================================

void InstructionDecoder::DecodeGroupC(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    // MULU.W
    if (opMode == 3)
    {
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2) & 0xFFFF;
        auto [result, ccr] = Alu::MulUnsigned(_cpu.D[reg] & 0xFFFF, src);
        _cpu.D[reg] = result;
        _cpu.UpdateCCR(ccr, 0x0F);
        return;
    }

    // MULS.W
    if (opMode == 7)
    {
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2);
        auto [result, ccr] = Alu::MulSigned(
            static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(_cpu.D[reg] & 0xFFFF))),
            static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(src))));
        _cpu.D[reg] = result;
        _cpu.UpdateCCR(ccr, 0x0F);
        return;
    }

    // ABCD
    if (opMode == 4 && (mode == 0 || mode == 1))
    {
        if (mode == 0)
        {
            uint8_t src = static_cast<uint8_t>(_cpu.D[eaReg]);
            uint8_t dst = static_cast<uint8_t>(_cpu.D[reg]);
            auto [result, ccr] = Alu::AddBcd(src, dst, _cpu.GetCCR());
            _cpu.D[reg] = (_cpu.D[reg] & 0xFFFFFF00u) | result;
            _cpu.SetCCR(ccr);
        }
        else
        {
            _cpu.A[eaReg]--;
            _cpu.A[reg]--;
            uint8_t src = _cpu.ReadByte(_cpu.A[eaReg]);
            uint8_t dst = _cpu.ReadByte(_cpu.A[reg]);
            auto [result, ccr] = Alu::AddBcd(src, dst, _cpu.GetCCR());
            _cpu.WriteByte(_cpu.A[reg], result);
            _cpu.SetCCR(ccr);
        }
        return;
    }

    // EXG
    if (opMode == 5 && mode == 0)
    {
        // EXG Dn,Dn
        uint32_t tmp = _cpu.D[reg];
        _cpu.D[reg] = _cpu.D[eaReg];
        _cpu.D[eaReg] = tmp;
        return;
    }
    if (opMode == 5 && mode == 1)
    {
        // EXG An,An
        uint32_t tmp = _cpu.A[reg];
        _cpu.A[reg] = _cpu.A[eaReg];
        _cpu.A[eaReg] = tmp;
        return;
    }
    if (opMode == 6 && mode == 1)
    {
        // EXG Dn,An
        uint32_t tmp = _cpu.D[reg];
        _cpu.D[reg] = _cpu.A[eaReg];
        _cpu.A[eaReg] = tmp;
        return;
    }

    // AND
    {
        int size;
        switch (opMode) { case 0: case 4: size = 1; break; case 1: case 5: size = 2; break; case 2: case 6: size = 4; break; default: size = 2; break; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        bool toEa = (opMode & 4) != 0;

        if (toEa)
        {
            uint32_t dst = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
            uint32_t result = dst & _cpu.D[reg];
            EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
            SetLogicFlags(result, size);
        }
        else
        {
            uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
            uint32_t result = _cpu.D[reg] & src;
            WriteRegValue(_cpu.D[reg], result, size);
            SetLogicFlags(result, size);
        }
    }
}

// ========================================================================
// Group D: ADD/ADDA/ADDX
// ========================================================================

void InstructionDecoder::DecodeGroupD(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    // ADDA
    if (opMode == 3 || opMode == 7)
    {
        int size = opMode == 3 ? 2 : 4;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
        if (size == 2) src = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(src))));
        _cpu.A[reg] += src;
        return;
    }

    // ADDX
    if ((opMode == 4 || opMode == 5 || opMode == 6) && (mode == 0 || mode == 1))
    {
        int size;
        switch (opMode) { case 4: size = 1; break; case 5: size = 2; break; default: size = 4; break; }
        if (mode == 0)
        {
            uint32_t a = ReadRegValue(_cpu.D[reg], size);
            uint32_t b = ReadRegValue(_cpu.D[eaReg], size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::AddByte(static_cast<uint8_t>(a), static_cast<uint8_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::AddWord(static_cast<uint16_t>(a), static_cast<uint16_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::AddLong(a, b, _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
            }
            if ((ccr & 0x04) != 0) ccr = static_cast<uint8_t>((ccr & ~0x04) | (_cpu.GetCCR() & 0x04));
            WriteRegValue(_cpu.D[reg], result, size);
            _cpu.SetCCR(ccr);
        }
        else
        {
            int s = size; if (eaReg == 7 && size == 1) s = 2;
            _cpu.A[eaReg] -= static_cast<uint32_t>(s);
            _cpu.A[reg] -= static_cast<uint32_t>(s);
            uint32_t b = ReadMemValue(_cpu.A[eaReg], size);
            uint32_t a = ReadMemValue(_cpu.A[reg], size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::AddByte(static_cast<uint8_t>(a), static_cast<uint8_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::AddWord(static_cast<uint16_t>(a), static_cast<uint16_t>(b), _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::AddLong(a, b, _cpu.GetCCR(), true); result = r.result; ccr = r.ccr; } break;
            }
            if ((ccr & 0x04) != 0) ccr = static_cast<uint8_t>((ccr & ~0x04) | (_cpu.GetCCR() & 0x04));
            WriteMemValue(_cpu.A[reg], result, size);
            _cpu.SetCCR(ccr);
        }
        return;
    }

    // ADD
    {
        int size;
        switch (opMode) { case 0: case 4: size = 1; break; case 1: case 5: size = 2; break; case 2: case 6: size = 4; break; default: size = 2; break; }
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, eaReg);
        bool toEa = (opMode & 4) != 0;

        if (toEa)
        {
            uint32_t dst = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::AddByte(static_cast<uint8_t>(dst), static_cast<uint8_t>(_cpu.D[reg]), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::AddWord(static_cast<uint16_t>(dst), static_cast<uint16_t>(_cpu.D[reg]), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::AddLong(dst, _cpu.D[reg], _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            }
            EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, size, result);
            _cpu.SetCCR(ccr);
        }
        else
        {
            uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, size);
            uint32_t dst = ReadRegValue(_cpu.D[reg], size);
            uint8_t ccr;
            uint32_t result;
            switch (size)
            {
                case 1: { auto r = Alu::AddByte(static_cast<uint8_t>(dst), static_cast<uint8_t>(src), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                case 2: { auto r = Alu::AddWord(static_cast<uint16_t>(dst), static_cast<uint16_t>(src), _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
                default: { auto r = Alu::AddLong(dst, src, _cpu.GetCCR()); result = r.result; ccr = r.ccr; } break;
            }
            WriteRegValue(_cpu.D[reg], result, size);
            _cpu.SetCCR(ccr);
        }
    }
}

// ========================================================================
// Group E: Shift/Rotate
// ========================================================================

void InstructionDecoder::DecodeGroupE(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int sizeField = (opcode >> 6) & 3;

    if (sizeField == 3)
    {
        // Check bit 11 to distinguish memory shift vs bit field
        if ((opcode & 0x0800) != 0)
        {
            DecodeBitField(opcode);
            return;
        }

        // Memory shift/rotate (always word size, shift by 1)
        int type = (opcode >> 9) & 3;
        bool left = (opcode & 0x0100) != 0;
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t val = EffectiveAddress::ReadValueForModify(_cpu, eaMode, eaR, 2);

        uint8_t ccr;
        uint32_t result;
        switch (type)
        {
            case 0: // ASL/ASR
                if (left)
                {
                    auto r = Alu::ShiftLeft(static_cast<uint16_t>(val), 1, _cpu.GetCCR());
                    result = r.result; ccr = r.ccr;
                }
                else
                {
                    auto r = Alu::ArithShiftRight(static_cast<uint16_t>(val), 1);
                    result = r.result; ccr = r.ccr;
                }
                break;
            case 1: // LSL/LSR
                if (left)
                {
                    auto r = Alu::ShiftLeft(static_cast<uint16_t>(val), 1, _cpu.GetCCR());
                    result = r.result; ccr = r.ccr;
                }
                else
                {
                    auto r = Alu::LogicalShiftRight(static_cast<uint16_t>(val), 1);
                    result = r.result; ccr = r.ccr;
                }
                break;
            case 2: // ROXL/ROXR memory
                if (left)
                {
                    auto r = Alu::RotateLeftX(val, 1, 2, _cpu.GetCCR());
                    result = r.result; ccr = r.ccr;
                }
                else
                {
                    auto r = Alu::RotateRightX(val, 1, 2, _cpu.GetCCR());
                    result = r.result; ccr = r.ccr;
                }
                break;
            default: // ROL/ROR
                if (left)
                {
                    auto r = Alu::RotateLeft(val, 1, 2);
                    result = r.result; ccr = r.ccr;
                }
                else
                {
                    auto r = Alu::RotateRight(val, 1, 2);
                    result = r.result; ccr = r.ccr;
                }
                break;
        }
        EffectiveAddress::WriteValueFromModify(_cpu, eaMode, eaR, 2, result);
        _cpu.SetCCR(ccr);
        return;
    }

    // Register shift/rotate
    int count;
    int countReg = (opcode >> 9) & 7;
    bool ir = (opcode & 0x0020) != 0; // i/r bit: 0=immediate count, 1=register count
    if (ir)
        count = static_cast<int>(_cpu.D[countReg] % 64);
    else
    {
        count = countReg;
        if (count == 0) count = 8;
    }

    int dreg = opcode & 7;
    int size;
    switch (sizeField) { case 0: size = 1; break; case 1: size = 2; break; default: size = 4; break; }
    bool isLeft = (opcode & 0x0100) != 0;
    int shiftType = (opcode >> 3) & 3;

    uint32_t val2 = ReadRegValue(_cpu.D[dreg], size);
    uint32_t result2;
    uint8_t ccr2;

    switch (shiftType)
    {
        case 0: // ASL/ASR
            if (isLeft)
            {
                switch (size)
                {
                    case 1: { auto r = Alu::ShiftLeft(static_cast<uint8_t>(val2), count, _cpu.GetCCR()); result2 = r.result; ccr2 = r.ccr; } break;
                    case 2: { auto r = Alu::ShiftLeft(static_cast<uint16_t>(val2), count, _cpu.GetCCR()); result2 = r.result; ccr2 = r.ccr; } break;
                    default: { auto r = Alu::ShiftLeft(val2, count, _cpu.GetCCR()); result2 = r.result; ccr2 = r.ccr; } break;
                }
            }
            else
            {
                switch (size)
                {
                    case 1: { auto r = Alu::ArithShiftRight(static_cast<uint8_t>(val2), count); result2 = r.result; ccr2 = r.ccr; } break;
                    case 2: { auto r = Alu::ArithShiftRight(static_cast<uint16_t>(val2), count); result2 = r.result; ccr2 = r.ccr; } break;
                    default: { auto r = Alu::ArithShiftRight(val2, count); result2 = r.result; ccr2 = r.ccr; } break;
                }
            }
            break;
        case 1: // LSL/LSR
            if (isLeft)
            {
                switch (size)
                {
                    case 1: { auto r = Alu::ShiftLeft(static_cast<uint8_t>(val2), count, _cpu.GetCCR()); result2 = r.result; ccr2 = r.ccr; } break;
                    case 2: { auto r = Alu::ShiftLeft(static_cast<uint16_t>(val2), count, _cpu.GetCCR()); result2 = r.result; ccr2 = r.ccr; } break;
                    default: { auto r = Alu::ShiftLeft(val2, count, _cpu.GetCCR()); result2 = r.result; ccr2 = r.ccr; } break;
                }
            }
            else
            {
                switch (size)
                {
                    case 1: { auto r = Alu::LogicalShiftRight(static_cast<uint8_t>(val2), count); result2 = r.result; ccr2 = r.ccr; } break;
                    case 2: { auto r = Alu::LogicalShiftRight(static_cast<uint16_t>(val2), count); result2 = r.result; ccr2 = r.ccr; } break;
                    default: { auto r = Alu::LogicalShiftRight(val2, count); result2 = r.result; ccr2 = r.ccr; } break;
                }
            }
            break;
        case 2: // ROXL/ROXR
            if (isLeft)
            {
                auto r = Alu::RotateLeftX(val2, count, size, _cpu.GetCCR());
                result2 = r.result; ccr2 = r.ccr;
            }
            else
            {
                auto r = Alu::RotateRightX(val2, count, size, _cpu.GetCCR());
                result2 = r.result; ccr2 = r.ccr;
            }
            break;
        case 3: // ROL/ROR
            if (isLeft)
            {
                auto r = Alu::RotateLeft(val2, count, size);
                result2 = r.result; ccr2 = r.ccr;
            }
            else
            {
                auto r = Alu::RotateRight(val2, count, size);
                result2 = r.result; ccr2 = r.ccr;
            }
            break;
        default:
            result2 = val2; ccr2 = _cpu.GetCCR();
            break;
    }

    WriteRegValue(_cpu.D[dreg], result2, size);
    _cpu.SetCCR(ccr2);
}

// ========================================================================
// Line-A / Line-F
// ========================================================================

void InstructionDecoder::DecodeLineA(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    _cpu.RaiseException(10); // Line-A emulator
}

void InstructionDecoder::DecodeLineF(uint16_t opcode)
{
    _cpu.EnsureRegSnapshot();
    int cpId = (opcode >> 9) & 7;

    if (cpId == 0) // MMU (coprocessor ID = 0)
    {
        DecodeMMUInstruction(opcode);
        return;
    }

    if (cpId == 1) // FPU (coprocessor ID = 1)
    {
        _fpuDecoder->Execute(opcode);
        return;
    }

    // Log the unhandled line-F instruction for diagnostics
    _cpu.LogException(std::format("F-line: opcode=${:04X} cpId={} at PC=${:08X}", opcode, cpId, _cpu.PC - 2));
    _cpu.RaiseException(11); // Line-F emulator / F-line
}

// ========================================================================
// MMU Instructions
// ========================================================================

void InstructionDecoder::DecodeMMUInstruction(uint16_t opcode)
{
    if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }

    uint16_t ext = _cpu.FetchWord();
    int mmuOp = (ext >> 13) & 7;

    switch (mmuOp)
    {
        case 0: // PMOVE to/from TT0, TT1
            DecodePMOVE_TT(opcode, ext);
            break;

        case 1: // PFLUSH / PFLUSHA / PLOAD
            DecodePFLUSH_PLOAD(opcode, ext);
            break;

        case 2: // PMOVE to/from TC, SRP, CRP
            DecodePMOVE_TC_SRP_CRP(opcode, ext);
            break;

        case 3: // PMOVE to/from MMUSR
            DecodePMOVE_MMUSR(opcode, ext);
            break;

        case 4: // PTEST
            DecodePTEST(opcode, ext);
            break;

        default:
            _cpu.RaiseException(11); // Illegal
            break;
    }
}

uint8_t InstructionDecoder::ResolveFunctionCode(uint16_t ext)
{
    // FC source encoding in MC68030 PMMU extension word (bits 4-0):
    //   bit 4 = 1: Immediate FC value (bits 2-0 = value)
    //   bit 4 = 0, bit 3 = 1: Data register Dn (bits 2-0 = register number)
    //   bit 4 = 0, bit 3 = 0: SFC (bit 0=0) or DFC (bit 0=1)
    if ((ext & 0x10) != 0)
        return static_cast<uint8_t>(ext & 7);                          // Immediate FC value
    if ((ext & 0x08) != 0)
        return static_cast<uint8_t>(_cpu.D[ext & 7] & 7);             // Data register Dn
    return (ext & 1) != 0
        ? static_cast<uint8_t>(_cpu.DFC & 7)                           // DFC register
        : static_cast<uint8_t>(_cpu.SFC & 7);                          // SFC register
}

void InstructionDecoder::DecodePMOVE_TT(uint16_t opcode, uint16_t ext)
{
    // mmuOp=0: PMOVE to/from TT0, TT1
    int pmReg = (ext >> 10) & 7;
    bool toMem = (ext & 0x0200) != 0; // bit 9: 1=reg->EA (write to memory)
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);

    switch (pmReg)
    {
        case 2: // TT0
            if (toMem)
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                _cpu.WriteLong(addr, _cpu.GetMmu().GetTT0());
            }
            else
            {
                _cpu.GetMmu().SetTT0(EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 4));
                _cpu.GetMmu().FlushAll();
            }
            break;
        case 3: // TT1
            if (toMem)
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                _cpu.WriteLong(addr, _cpu.GetMmu().GetTT1());
            }
            else
            {
                _cpu.GetMmu().SetTT1(EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 4));
                _cpu.GetMmu().FlushAll();
            }
            break;
    }
}

void InstructionDecoder::DecodePFLUSH_PLOAD(uint16_t opcode, uint16_t ext)
{
    // mmuOp=1: PFLUSH / PFLUSHA / PLOAD

    // PFLUSHA: special pattern ext=0x2400
    if (ext == 0x2400)
    {
        _cpu.GetMmu().FlushAll();
        return;
    }

    // Distinguish PLOAD from PFLUSH: bit 12 = 0 for PLOAD, 1 for PFLUSH
    if ((ext & 0x1000) == 0)
    {
        // PLOAD
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
        bool isRead = (ext & 0x0200) != 0; // bit 9: 1=PLOADR, 0=PLOADW
        uint8_t fc = ResolveFunctionCode(ext);
        _cpu.GetMmu().PLoad(addr, _cpu.GetSupervisorMode(), !isRead, fc);
        return;
    }

    // PFLUSH FC,#mask or PFLUSH FC,#mask,(ea)
    uint8_t flushFC = ResolveFunctionCode(ext);
    uint8_t mask = static_cast<uint8_t>((ext >> 5) & 7); // bits 7-5: 3-bit FC mask
    bool hasEA = (ext & 0x0800) != 0;   // bit 11: 1=has EA

    if (hasEA)
    {
        // PFLUSH FC,#mask,(ea)
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
        _cpu.GetMmu().FlushByFCAndAddress(flushFC, mask, addr);
    }
    else
    {
        // PFLUSH FC,#mask
        _cpu.GetMmu().FlushByFC(flushFC, mask);
    }
}

void InstructionDecoder::DecodePMOVE_TC_SRP_CRP(uint16_t opcode, uint16_t ext)
{
    // mmuOp=2: PMOVE to/from TC, SRP, CRP
    int pmReg = (ext >> 10) & 7;
    bool toMem = (ext & 0x0200) != 0;
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);

    switch (pmReg)
    {
        case 0: // TC (32-bit)
            if (toMem)
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                _cpu.WriteLong(addr, _cpu.GetMmu().GetTC());
            }
            else
            {
                uint32_t tcVal = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 4);
                _cpu.GetMmu().SetTC(tcVal);
                _cpu.GetMmu().FlushAll();
            }
            break;
        case 2: // SRP (64-bit)
            if (toMem)
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                _cpu.WriteLong(addr, static_cast<uint32_t>(_cpu.GetMmu().SRP >> 32));
                _cpu.WriteLong(addr + 4, static_cast<uint32_t>(_cpu.GetMmu().SRP));
            }
            else
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                uint64_t hi = _cpu.ReadLong(addr);
                uint64_t lo = _cpu.ReadLong(addr + 4);
                _cpu.GetMmu().SRP = (hi << 32) | lo;
                _cpu.GetMmu().FlushAll();
            }
            break;
        case 3: // CRP (64-bit)
            if (toMem)
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                _cpu.WriteLong(addr, static_cast<uint32_t>(_cpu.GetMmu().CRP >> 32));
                _cpu.WriteLong(addr + 4, static_cast<uint32_t>(_cpu.GetMmu().CRP));
            }
            else
            {
                uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);
                uint64_t hi = _cpu.ReadLong(addr);
                uint64_t lo = _cpu.ReadLong(addr + 4);
                _cpu.GetMmu().CRP = (hi << 32) | lo;
                _cpu.GetMmu().FlushAll();
            }
            break;
    }
}

void InstructionDecoder::DecodePMOVE_MMUSR(uint16_t opcode, uint16_t ext)
{
    // mmuOp=3: PMOVE to/from MMUSR (16-bit)
    bool toMem = (ext & 0x0200) != 0;
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);

    if (toMem)
    {
        EffectiveAddress::WriteValue(_cpu, eaMode, eaR, 2, _cpu.GetMmu().MMUSR);
    }
    else
    {
        _cpu.GetMmu().MMUSR = static_cast<uint16_t>(EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 2));
    }
}

void InstructionDecoder::DecodePTEST(uint16_t opcode, uint16_t ext)
{
    // mmuOp=4: PTEST
    int level = (ext >> 10) & 7;        // bits 12-10: level (max levels to search)
    bool isRead = (ext & 0x0200) != 0;  // bit 9: 1=read, 0=write
    bool hasAReg = (ext & 0x0100) != 0; // bit 8: 1=store result in A-reg
    int aReg = (ext >> 5) & 7;          // bits 7-5: A-register for result

    uint8_t fc = ResolveFunctionCode(ext);

    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 4);

    _cpu.GetMmu().PTest(addr, _cpu.GetSupervisorMode(), !isRead, fc, level);

    // If A-register specified, store the last descriptor address
    if (hasAReg)
    {
        _cpu.A[aReg] = _cpu.GetMmu().GetLastDescriptorAddress();
    }
}

// ========================================================================
// CAS / CAS2 (68020+)
// ========================================================================

void InstructionDecoder::DecodeCAS(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    int ssBits = (opcode >> 9) & 3; // size: 01=B, 10=W, 11=L
    int size;
    switch (ssBits) { case 1: size = 1; break; case 2: size = 2; break; default: size = 4; break; }

    // CAS2 check: mode=7, reg=4
    if (mode == 7 && reg == 4)
    {
        DecodeCAS2(opcode, size);
        return;
    }

    uint16_t ext = _cpu.FetchWord();
    int dc = ext & 7;           // compare register
    int du = (ext >> 6) & 7;    // update register

    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, size);
    uint32_t memVal = ReadMemValue(addr, size);
    uint32_t cmpVal = ReadRegValue(_cpu.D[dc], size);

    // Compare Dc with <ea>
    uint8_t ccr;
    switch (size)
    {
        case 1: { auto r = Alu::SubByte(static_cast<uint8_t>(memVal), static_cast<uint8_t>(cmpVal), _cpu.GetCCR()); ccr = r.ccr; } break;
        case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(memVal), static_cast<uint16_t>(cmpVal), _cpu.GetCCR()); ccr = r.ccr; } break;
        default: { auto r = Alu::SubLong(memVal, cmpVal, _cpu.GetCCR()); ccr = r.ccr; } break;
    }
    _cpu.SetCCR(ccr);

    if (_cpu.GetFlagZ())
    {
        // Equal: write Du to <ea>
        WriteMemValue(addr, ReadRegValue(_cpu.D[du], size), size);
    }
    else
    {
        // Not equal: write <ea> to Dc
        WriteRegValue(_cpu.D[dc], memVal, size);
    }
}

void InstructionDecoder::DecodeCAS2(uint16_t opcode, int size)
{
    uint16_t ext1 = _cpu.FetchWord();
    uint16_t ext2 = _cpu.FetchWord();

    int dc1 = ext1 & 7;
    int du1 = (ext1 >> 6) & 7;
    int rn1 = (ext1 >> 12) & 0xF;

    int dc2 = ext2 & 7;
    int du2 = (ext2 >> 6) & 7;
    int rn2 = (ext2 >> 12) & 0xF;

    uint32_t addr1 = rn1 < 8 ? _cpu.D[rn1] : _cpu.A[rn1 - 8];
    uint32_t addr2 = rn2 < 8 ? _cpu.D[rn2] : _cpu.A[rn2 - 8];

    uint32_t mem1 = ReadMemValue(addr1, size);
    uint32_t mem2 = ReadMemValue(addr2, size);

    // Compare Dc1 with (Rn1)
    uint8_t ccr;
    switch (size)
    {
        case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(mem1), static_cast<uint16_t>(ReadRegValue(_cpu.D[dc1], size)), _cpu.GetCCR()); ccr = r.ccr; } break;
        default: { auto r = Alu::SubLong(mem1, ReadRegValue(_cpu.D[dc1], size), _cpu.GetCCR()); ccr = r.ccr; } break;
    }
    _cpu.SetCCR(ccr);

    if (_cpu.GetFlagZ())
    {
        // First compare equal, compare Dc2 with (Rn2)
        switch (size)
        {
            case 2: { auto r = Alu::SubWord(static_cast<uint16_t>(mem2), static_cast<uint16_t>(ReadRegValue(_cpu.D[dc2], size)), _cpu.GetCCR()); ccr = r.ccr; } break;
            default: { auto r = Alu::SubLong(mem2, ReadRegValue(_cpu.D[dc2], size), _cpu.GetCCR()); ccr = r.ccr; } break;
        }
        _cpu.SetCCR(ccr);

        if (_cpu.GetFlagZ())
        {
            // Both equal: write Du1 and Du2
            WriteMemValue(addr1, ReadRegValue(_cpu.D[du1], size), size);
            WriteMemValue(addr2, ReadRegValue(_cpu.D[du2], size), size);
        }
        else
        {
            WriteRegValue(_cpu.D[dc1], mem1, size);
            WriteRegValue(_cpu.D[dc2], mem2, size);
        }
    }
    else
    {
        WriteRegValue(_cpu.D[dc1], mem1, size);
        WriteRegValue(_cpu.D[dc2], mem2, size);
    }
}

// ========================================================================
// Long Multiply (68020+)
// ========================================================================

void InstructionDecoder::DecodeLongMul(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t ext = _cpu.FetchWord();

    int dl = (ext >> 12) & 7;   // destination low register
    int dh = ext & 7;           // destination high register
    bool isSigned = (ext & 0x0800) != 0;
    bool quad = (ext & 0x0400) != 0; // 64-bit result

    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 4);

    if (isSigned)
    {
        auto [lo, hi, ccr] = Alu::MulSignedLong(static_cast<int32_t>(_cpu.D[dl]), static_cast<int32_t>(src));
        _cpu.D[dl] = lo;
        uint8_t newCcr = ccr;
        if (quad)
        {
            _cpu.D[dh] = hi;
            // For 64-bit result: N=bit63, Z=(full 64-bit result == 0)
            newCcr = 0;
            if ((hi & 0x80000000u) != 0) newCcr |= 0x08; // N from bit 63
            if (lo == 0 && hi == 0) newCcr |= 0x04;       // Z from full 64-bit
        }
        else
        {
            if (hi != 0 && hi != 0xFFFFFFFF) newCcr |= 0x02; // V for 32-bit overflow
            else if (hi == 0xFFFFFFFF && (lo & 0x80000000u) == 0) newCcr |= 0x02;
        }
        _cpu.UpdateCCR(newCcr, 0x0F);
    }
    else
    {
        auto [lo, hi, ccr] = Alu::MulUnsignedLong(_cpu.D[dl], src);
        _cpu.D[dl] = lo;
        uint8_t newCcr = ccr;
        if (quad)
        {
            _cpu.D[dh] = hi;
            // For 64-bit result: N=bit63, Z=(full 64-bit result == 0)
            newCcr = 0;
            if ((hi & 0x80000000u) != 0) newCcr |= 0x08; // N from bit 63
            if (lo == 0 && hi == 0) newCcr |= 0x04;       // Z from full 64-bit
        }
        else
        {
            if (hi != 0) newCcr |= 0x02; // V for 32-bit overflow
        }
        _cpu.UpdateCCR(newCcr, 0x0F);
    }
}

// ========================================================================
// Long Division (68020+)
// ========================================================================

void InstructionDecoder::DecodeLongDiv(uint16_t opcode)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t ext = _cpu.FetchWord();

    int dq = (ext >> 12) & 7;   // quotient register
    int dr = ext & 7;           // remainder register
    bool isSigned = (ext & 0x0800) != 0;
    bool quad = (ext & 0x0400) != 0; // 64-bit dividend

    auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
    uint32_t src = EffectiveAddress::ReadValue(_cpu, eaMode, eaR, 4);

    if (src == 0) { _cpu.RaiseException(5); return; } // Division by zero

    if (isSigned)
    {
        int64_t dividend;
        if (quad)
            dividend = (static_cast<int64_t>(static_cast<int32_t>(_cpu.D[dr])) << 32) | _cpu.D[dq];
        else
            dividend = static_cast<int32_t>(_cpu.D[dq]);

        auto [quot, rem, ccr, overflow] = Alu::DivSignedLong(dividend, static_cast<int32_t>(src));
        if (overflow) { _cpu.SetFlagV(true); return; }
        _cpu.D[dq] = quot;
        if (dr != dq) _cpu.D[dr] = rem;
        _cpu.UpdateCCR(ccr, 0x0F);
    }
    else
    {
        uint64_t dividend;
        if (quad)
            dividend = (static_cast<uint64_t>(_cpu.D[dr]) << 32) | _cpu.D[dq];
        else
            dividend = _cpu.D[dq];

        auto [quot, rem, ccr, overflow] = Alu::DivUnsignedLong(dividend, src);
        if (overflow) { _cpu.SetFlagV(true); return; }
        _cpu.D[dq] = quot;
        if (dr != dq) _cpu.D[dr] = rem;
        _cpu.UpdateCCR(ccr, 0x0F);
    }
}

// ========================================================================
// Bit Field Instructions (68020+)
// ========================================================================

void InstructionDecoder::DecodeBitField(uint16_t opcode)
{
    int bfOp = (opcode >> 8) & 7;
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t ext = _cpu.FetchWord();

    int dnReg = (ext >> 12) & 7; // data register for BFEXTU/BFEXTS/BFFFO/BFINS
    bool doReg = (ext & 0x0800) != 0;
    int offset = doReg ? static_cast<int>(_cpu.D[(ext >> 6) & 7] & 0x1F) : ((ext >> 6) & 0x1F);
    bool dwReg = (ext & 0x0020) != 0;
    int width = dwReg ? static_cast<int>(_cpu.D[ext & 7] % 32) : (ext & 0x1F);
    if (width == 0) width = 32;

    // For register operand
    if (mode == 0)
    {
        uint32_t data = _cpu.D[reg];
        uint32_t field = ExtractBitFieldReg(data, offset, width);

        switch (bfOp)
        {
            case 0: // BFTST
                SetBitFieldFlags(field, width);
                break;
            case 1: // BFEXTU
                SetBitFieldFlags(field, width);
                _cpu.D[dnReg] = field;
                break;
            case 2: // BFCHG
                SetBitFieldFlags(field, width);
                _cpu.D[reg] = InsertBitFieldReg(data, ~field, offset, width);
                break;
            case 3: // BFEXTS
                SetBitFieldFlags(field, width);
                // Sign extend
                if (width < 32 && (field & (1u << (width - 1))) != 0)
                    field |= 0xFFFFFFFFu << width;
                _cpu.D[dnReg] = field;
                break;
            case 4: // BFCLR
                SetBitFieldFlags(field, width);
                _cpu.D[reg] = InsertBitFieldReg(data, 0, offset, width);
                break;
            case 5: // BFFFO
                SetBitFieldFlags(field, width);
                {
                    int ffo = 0;
                    for (int i = width - 1; i >= 0; i--)
                    {
                        if ((field & (1u << i)) != 0) break;
                        ffo++;
                    }
                    _cpu.D[dnReg] = static_cast<uint32_t>(offset + ffo);
                }
                break;
            case 6: // BFSET
                SetBitFieldFlags(field, width);
                {
                    uint32_t ones = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
                    _cpu.D[reg] = InsertBitFieldReg(data, ones, offset, width);
                }
                break;
            case 7: // BFINS
                {
                    uint32_t ins = _cpu.D[dnReg];
                    if (width < 32) ins &= (1u << width) - 1;
                    _cpu.D[reg] = InsertBitFieldReg(data, ins, offset, width);
                    SetBitFieldFlags(ins, width);
                }
                break;
        }
    }
    else
    {
        // Memory operand
        auto [eaMode, eaR] = EffectiveAddress::Decode(mode, reg);
        uint32_t baseAddr = EffectiveAddress::ResolveAddress(_cpu, eaMode, eaR, 1);

        // Adjust for offset (can be > 7 for register offset)
        if (doReg) offset = static_cast<int>(_cpu.D[(ext >> 6) & 7]);
        int byteOff = offset >> 3;
        int bitOff = offset & 7;
        baseAddr = static_cast<uint32_t>(baseAddr + byteOff);

        // Read enough bytes to cover the field
        int totalBits = bitOff + width;
        int bytesNeeded = (totalBits + 7) / 8;
        uint64_t data = 0;
        for (int i = 0; i < bytesNeeded && i < 5; i++)
            data = (data << 8) | _cpu.ReadByte(baseAddr + static_cast<uint32_t>(i));

        // Extract field
        int shift = (bytesNeeded * 8) - bitOff - width;
        uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
        uint32_t field = static_cast<uint32_t>((data >> shift) & mask);

        switch (bfOp)
        {
            case 0: // BFTST
                SetBitFieldFlags(field, width);
                break;
            case 1: // BFEXTU
                SetBitFieldFlags(field, width);
                _cpu.D[dnReg] = field;
                break;
            case 2: // BFCHG
                SetBitFieldFlags(field, width);
                data ^= static_cast<uint64_t>(mask) << shift;
                WriteBitFieldMem(baseAddr, data, bytesNeeded);
                break;
            case 3: // BFEXTS
                SetBitFieldFlags(field, width);
                if (width < 32 && (field & (1u << (width - 1))) != 0)
                    field |= 0xFFFFFFFFu << width;
                _cpu.D[dnReg] = field;
                break;
            case 4: // BFCLR
                SetBitFieldFlags(field, width);
                data &= ~(static_cast<uint64_t>(mask) << shift);
                WriteBitFieldMem(baseAddr, data, bytesNeeded);
                break;
            case 5: // BFFFO
                SetBitFieldFlags(field, width);
                {
                    int ffo = 0;
                    for (int i = width - 1; i >= 0; i--)
                    {
                        if ((field & (1u << i)) != 0) break;
                        ffo++;
                    }
                    _cpu.D[dnReg] = static_cast<uint32_t>(offset + ffo);
                }
                break;
            case 6: // BFSET
                SetBitFieldFlags(field, width);
                data |= static_cast<uint64_t>(mask) << shift;
                WriteBitFieldMem(baseAddr, data, bytesNeeded);
                break;
            case 7: // BFINS
                {
                    uint32_t ins = _cpu.D[dnReg];
                    if (width < 32) ins &= (1u << width) - 1;
                    data &= ~(static_cast<uint64_t>(mask) << shift);
                    data |= static_cast<uint64_t>(ins) << shift;
                    WriteBitFieldMem(baseAddr, data, bytesNeeded);
                    SetBitFieldFlags(ins, width);
                }
                break;
        }
    }
}

uint32_t InstructionDecoder::ExtractBitFieldReg(uint32_t data, int offset, int width)
{
    // Bit field in register: bit 31 is MSB (offset 0)
    offset %= 32;
    uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
    int shift = 32 - offset - width;
    if (shift >= 0)
        return (data >> shift) & mask;
    else
    {
        // Wraps around
        uint32_t hi = data << (-shift);
        uint32_t lo = data >> (32 + shift);
        return (hi | lo) & mask;
    }
}

uint32_t InstructionDecoder::InsertBitFieldReg(uint32_t data, uint32_t field, int offset, int width)
{
    offset %= 32;
    uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
    field &= mask;
    int shift = 32 - offset - width;
    if (shift >= 0)
    {
        data &= ~(mask << shift);
        data |= field << shift;
    }
    else
    {
        uint32_t hiMask = mask >> (-shift);
        uint32_t loMask = mask << (32 + shift);
        data &= ~hiMask;
        data |= field >> (-shift);
        data &= ~loMask;
        data |= field << (32 + shift);
    }
    return data;
}

void InstructionDecoder::WriteBitFieldMem(uint32_t addr, uint64_t data, int bytes)
{
    for (int i = 0; i < bytes && i < 5; i++)
    {
        uint8_t b = static_cast<uint8_t>(data >> ((bytes - 1 - i) * 8));
        _cpu.WriteByte(addr + static_cast<uint32_t>(i), b);
    }
}

void InstructionDecoder::SetBitFieldFlags(uint32_t field, int width)
{
    _cpu.SetFlagN(width > 0 && (field & (1u << (width - 1))) != 0);
    _cpu.SetFlagZ(field == 0);
    _cpu.SetFlagV(false);
    _cpu.SetFlagC(false);
}

// ========================================================================
// Helpers
// ========================================================================

int InstructionDecoder::GetSize2(uint16_t opcode)
{
    switch ((opcode >> 6) & 3)
    {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        default: return 2;
    }
}

uint32_t InstructionDecoder::ReadImmediate(int size)
{
    switch (size)
    {
        case 1: return static_cast<uint32_t>(_cpu.FetchWord() & 0xFF);
        case 2: return _cpu.FetchWord();
        default: return _cpu.FetchLong();
    }
}

void InstructionDecoder::SetLogicFlags(uint32_t value, int size)
{
    uint8_t ccr;
    switch (size)
    {
        case 1: ccr = Alu::SetNZFlags(static_cast<uint8_t>(value)); break;
        case 2: ccr = Alu::SetNZFlags(static_cast<uint16_t>(value)); break;
        default: ccr = Alu::SetNZFlags(value); break;
    }
    _cpu.UpdateCCR(ccr, 0x0F); // Update N,Z,V,C (V=0, C=0 for logic ops)
}

uint32_t InstructionDecoder::ReadRegValue(uint32_t reg, int size)
{
    switch (size)
    {
        case 1: return reg & 0xFF;
        case 2: return reg & 0xFFFF;
        default: return reg;
    }
}

void InstructionDecoder::WriteRegValue(uint32_t& reg, uint32_t value, int size)
{
    switch (size)
    {
        case 1: reg = (reg & 0xFFFFFF00u) | (value & 0xFF); break;
        case 2: reg = (reg & 0xFFFF0000u) | (value & 0xFFFF); break;
        default: reg = value; break;
    }
}

uint32_t InstructionDecoder::ReadMemValue(uint32_t addr, int size)
{
    switch (size)
    {
        case 1: return _cpu.ReadByte(addr);
        case 2: return _cpu.ReadWord(addr);
        default: return _cpu.ReadLong(addr);
    }
}

void InstructionDecoder::WriteMemValue(uint32_t addr, uint32_t value, int size)
{
    switch (size)
    {
        case 1: _cpu.WriteByte(addr, static_cast<uint8_t>(value)); break;
        case 2: _cpu.WriteWord(addr, static_cast<uint16_t>(value)); break;
        default: _cpu.WriteLong(addr, value); break;
    }
}

// ========================================================================
// Phase 3.2: Specialized fast handlers for frequent instructions
// ========================================================================

void InstructionDecoder::FastMOVEQ(uint16_t opcode)
{
    int reg = (opcode >> 9) & 7;
    int32_t data = static_cast<int8_t>(opcode & 0xFF);
    uint32_t val = static_cast<uint32_t>(data);
    _cpu.D[reg] = val;
    // Set N, Z, clear V, C (X unchanged)
    uint8_t ccr = _cpu.GetCCR() & 0x10; // preserve X
    if (val == 0) ccr |= 0x04;          // Z
    if (val & 0x80000000) ccr |= 0x08;  // N
    _cpu.SetCCRByte(ccr);
}

void InstructionDecoder::FastRTS(uint16_t /*opcode*/)
{
    _cpu.EnsureRegSnapshot();  // PopLong does memory read
    _cpu.PC = _cpu.PopLong();
    _cpu.ShadowPop();
}

void InstructionDecoder::FastBcc8(uint16_t opcode)
{
    int cond = (opcode >> 8) & 0xF;
    int32_t disp8 = static_cast<int8_t>(opcode & 0xFF);
    // disp8 is never 0 or -1 (those are handled by generic DecodeGroup6)
    // PC is already past the opcode word (FetchWord incremented it)
    // Displacement is relative to that point, matching DecodeGroup6 behavior
    if (_cpu.EvaluateCondition(cond))
        _cpu.PC = static_cast<uint32_t>(static_cast<int32_t>(_cpu.PC) + disp8);
}

void InstructionDecoder::FastMoveLDnDm(uint16_t opcode)
{
    int dst = (opcode >> 9) & 7;
    int src = opcode & 7;
    uint32_t val = _cpu.D[src];
    _cpu.D[dst] = val;
    // Set N, Z, clear V, C (X unchanged)
    uint8_t ccr = _cpu.GetCCR() & 0x10;
    if (val == 0) ccr |= 0x04;
    if (val & 0x80000000) ccr |= 0x08;
    _cpu.SetCCRByte(ccr);
}

void InstructionDecoder::FastAddLDnDm(uint16_t opcode)
{
    int dstReg = (opcode >> 9) & 7;
    int srcReg = opcode & 7;
    auto r = Alu::AddLong(_cpu.D[dstReg], _cpu.D[srcReg], _cpu.GetCCR());
    _cpu.D[dstReg] = r.result;
    _cpu.SetCCR(r.ccr);
}

void InstructionDecoder::FastSubLDnDm(uint16_t opcode)
{
    int dstReg = (opcode >> 9) & 7;
    int srcReg = opcode & 7;
    auto r = Alu::SubLong(_cpu.D[dstReg], _cpu.D[srcReg], _cpu.GetCCR());
    _cpu.D[dstReg] = r.result;
    _cpu.SetCCR(r.ccr);
}

void InstructionDecoder::FastCmpLDnDm(uint16_t opcode)
{
    int dstReg = (opcode >> 9) & 7;
    int srcReg = opcode & 7;
    auto r = Alu::SubLong(_cpu.D[dstReg], _cpu.D[srcReg], _cpu.GetCCR());
    _cpu.UpdateCCR(r.ccr, 0x0F); // CMP only updates NZVC, not X
}

} // namespace Em68030::Core
