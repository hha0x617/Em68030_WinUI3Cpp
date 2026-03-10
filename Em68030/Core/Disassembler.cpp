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

#include "Disassembler.h"
#include "Memory.h"

#include <format>
#include <cstring>
#include <cmath>
#include <bit>

namespace Em68030::Core {

// ============================================================================
// DisasmLine
// ============================================================================

std::string DisasmLine::ToString() const
{
    std::string ops = Operands.empty() ? "" : std::string(" ") + Operands;
    // Pad RawBytes to 20 chars, Mnemonic to 8 chars
    std::string rawPadded = RawBytes;
    if (rawPadded.size() < 20) rawPadded.resize(20, ' ');
    std::string mnPadded = Mnemonic;
    if (mnPadded.size() < 8) mnPadded.resize(8, ' ');
    return std::format("{:08X}: {} {}{}", Address, rawPadded, mnPadded, ops);
}

// ============================================================================
// Static data
// ============================================================================

const std::string Disassembler::CondCodes[16] = {
    "T", "F", "HI", "LS", "CC", "CS", "NE", "EQ",
    "VC", "VS", "PL", "MI", "GE", "LT", "GT", "LE"
};

const std::string Disassembler::BfOpNames[8] = {
    "BFTST", "BFEXTU", "BFCHG", "BFEXTS", "BFCLR", "BFFFO", "BFSET", "BFINS"
};

const std::string Disassembler::FpuCondNames[32] = {
    "F","EQ","OGT","OGE","OLT","OLE","OGL","OR",
    "UN","UEQ","UGT","UGE","ULT","ULE","NE","T",
    "SF","SEQ","GT","GE","LT","LE","GL","GLE",
    "NGLE","NGL","NLE","NLT","NGE","NGT","SNE","ST"
};

const std::string Disassembler::MmuTTRegNames[4] = { "", "", "TT0", "TT1" };
const std::string Disassembler::MmuTCSRegNames[4] = { "TC", "", "SRP", "CRP" };

std::string Disassembler::FpuOpNames[128];
bool Disassembler::s_fpuOpNamesInitialized = false;

void Disassembler::InitFpuOpNames()
{
    if (s_fpuOpNamesInitialized) return;
    for (int i = 0; i < 128; i++) FpuOpNames[i] = "";
    FpuOpNames[0x00] = "FMOVE";
    FpuOpNames[0x01] = "FINT";
    FpuOpNames[0x02] = "FSINH";
    FpuOpNames[0x03] = "FINTRZ";
    FpuOpNames[0x04] = "FSQRT";
    FpuOpNames[0x06] = "FLOGNP1";
    FpuOpNames[0x08] = "FETOXM1";
    FpuOpNames[0x09] = "FTANH";
    FpuOpNames[0x0A] = "FATAN";
    FpuOpNames[0x0C] = "FASIN";
    FpuOpNames[0x0D] = "FATANH";
    FpuOpNames[0x0E] = "FSIN";
    FpuOpNames[0x0F] = "FTAN";
    FpuOpNames[0x10] = "FETOX";
    FpuOpNames[0x11] = "FTWOTOX";
    FpuOpNames[0x12] = "FTENTOX";
    FpuOpNames[0x14] = "FLOGN";
    FpuOpNames[0x15] = "FLOG10";
    FpuOpNames[0x16] = "FLOG2";
    FpuOpNames[0x18] = "FABS";
    FpuOpNames[0x19] = "FCOSH";
    FpuOpNames[0x1A] = "FNEG";
    FpuOpNames[0x1C] = "FACOS";
    FpuOpNames[0x1D] = "FCOS";
    FpuOpNames[0x1E] = "FGETEXP";
    FpuOpNames[0x1F] = "FGETMAN";
    FpuOpNames[0x20] = "FDIV";
    FpuOpNames[0x21] = "FMOD";
    FpuOpNames[0x22] = "FADD";
    FpuOpNames[0x23] = "FMUL";
    FpuOpNames[0x24] = "FSGLDIV";
    FpuOpNames[0x25] = "FREM";
    FpuOpNames[0x26] = "FSCALE";
    FpuOpNames[0x27] = "FSGLMUL";
    FpuOpNames[0x28] = "FSUB";
    FpuOpNames[0x38] = "FCMP";
    FpuOpNames[0x3A] = "FTST";
    for (int i = 0x30; i <= 0x37; i++) FpuOpNames[i] = "FSINCOS";
    s_fpuOpNamesInitialized = true;
}

// ============================================================================
// Constructor
// ============================================================================

Disassembler::Disassembler(Memory& memory)
    : m_memory(memory)
{
    InitFpuOpNames();
}

// ============================================================================
// Utility helpers
// ============================================================================

std::string Disassembler::SizeSuffix(int size)
{
    switch (size) {
        case 1: return ".B";
        case 2: return ".W";
        case 4: return ".L";
        default: return "";
    }
}

int Disassembler::GetSize2(uint16_t opcode)
{
    switch ((opcode >> 6) & 3) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        default: return 2;
    }
}

uint16_t Disassembler::ReadWord()
{
    uint16_t val = m_memory.PeekWord(m_pc);
    m_pc += 2;
    return val;
}

uint32_t Disassembler::ReadLong()
{
    uint32_t val = m_memory.PeekLong(m_pc);
    m_pc += 4;
    return val;
}

int Disassembler::FpuFormatSize(int format)
{
    switch (format) {
        case 0: return 4;   // Long Integer
        case 1: return 4;   // Single
        case 2: return 12;  // Extended
        case 3: return 12;  // Packed Decimal
        case 4: return 2;   // Word Integer
        case 5: return 8;   // Double
        case 6: return 1;   // Byte Integer
        default: return 4;
    }
}

std::string Disassembler::FpuFormatName(int format)
{
    switch (format) {
        case 0: return ".L";
        case 1: return ".S";
        case 2: return ".X";
        case 3: return ".P";
        case 4: return ".W";
        case 5: return ".D";
        case 6: return ".B";
        default: return "";
    }
}

// ============================================================================
// Main entry points
// ============================================================================

DisasmLine Disassembler::DisassembleOne(uint32_t address)
{
    m_pc = address;
    DisasmLine line;
    line.Address = address;

    uint16_t opcode = ReadWord();
    int group = (opcode >> 12) & 0xF;

    switch (group) {
        case 0x0: DisasmGroup0(opcode, line); break;
        case 0x1: DisasmMOVE(opcode, line, 1); break;
        case 0x2: DisasmMOVE(opcode, line, 4); break;
        case 0x3: DisasmMOVE(opcode, line, 2); break;
        case 0x4: DisasmGroup4(opcode, line); break;
        case 0x5: DisasmGroup5(opcode, line); break;
        case 0x6: DisasmGroup6(opcode, line); break;
        case 0x7: DisasmMOVEQ(opcode, line); break;
        case 0x8: DisasmGroup8(opcode, line); break;
        case 0x9: DisasmGroup9(opcode, line); break;
        case 0xA: line.Mnemonic = "DC.W"; line.Operands = std::format("${:04X}", opcode); break;
        case 0xB: DisasmGroupB(opcode, line); break;
        case 0xC: DisasmGroupC(opcode, line); break;
        case 0xD: DisasmGroupD(opcode, line); break;
        case 0xE: DisasmGroupE(opcode, line); break;
        case 0xF: DisasmGroupF(opcode, line); break;
    }

    line.Length = static_cast<int>(m_pc - address);

    // Build raw bytes
    std::string rawBytes;
    for (uint32_t i = address; i < m_pc; i += 2) {
        if (!rawBytes.empty()) rawBytes += ' ';
        rawBytes += std::format("{:04X}", m_memory.PeekWord(i));
    }
    line.RawBytes = rawBytes;

    return line;
}

std::vector<DisasmLine> Disassembler::Disassemble(uint32_t startAddress, int count)
{
    std::vector<DisasmLine> lines;
    uint32_t addr = startAddress;
    for (int i = 0; i < count; i++) {
        auto line = DisassembleOne(addr);
        lines.push_back(line);
        addr += static_cast<uint32_t>(line.Length);
        if (line.Length == 0) { addr += 2; break; } // Safety
    }
    return lines;
}

std::vector<DisasmLine> Disassembler::DisassembleRange(uint32_t startAddress, uint32_t endAddress, int maxLines)
{
    std::vector<DisasmLine> lines;
    uint32_t addr = startAddress;
    while (addr < endAddress && static_cast<int>(lines.size()) < maxLines) {
        auto line = DisassembleOne(addr);
        lines.push_back(line);
        addr += static_cast<uint32_t>(line.Length);
        if (line.Length == 0) { addr += 2; break; } // Safety
    }
    return lines;
}

// ============================================================================
// FormatEA
// ============================================================================

std::string Disassembler::FormatEA(int mode, int reg, int size)
{
    switch (mode) {
        case 0: return std::format("D{}", reg);
        case 1: return std::format("A{}", reg);
        case 2: return std::format("(A{})", reg);
        case 3: return std::format("(A{})+", reg);
        case 4: return std::format("-(A{})", reg);
        case 5: {
            int16_t disp = static_cast<int16_t>(ReadWord());
            return std::format("({},A{})", disp, reg);
        }
        case 6:
            return FormatIndexed(std::format("A{}", reg));
        case 7:
            switch (reg) {
                case 0: {
                    uint16_t addr = ReadWord();
                    return std::format("(${:04X}).W", addr);
                }
                case 1: {
                    uint32_t addr = ReadLong();
                    return std::format("(${:08X}).L", addr);
                }
                case 2: {
                    uint32_t pcBefore = m_pc;
                    int16_t disp = static_cast<int16_t>(ReadWord());
                    return std::format("(${:08X},PC)", static_cast<uint32_t>(pcBefore + disp));
                }
                case 3:
                    return FormatIndexed("PC");
                case 4: {
                    if (size == 1) {
                        uint16_t val = ReadWord();
                        return std::format("#${:02X}", val & 0xFF);
                    } else if (size == 2) {
                        uint16_t val = ReadWord();
                        return std::format("#${:04X}", val);
                    } else {
                        uint32_t val = ReadLong();
                        return std::format("#${:08X}", val);
                    }
                }
                default:
                    return "???";
            }
        default:
            return "???";
    }
}

// ============================================================================
// FormatIndexed
// ============================================================================

std::string Disassembler::FormatIndexed(const std::string& baseReg)
{
    uint16_t ext = ReadWord();
    bool isLong = (ext & 0x0800) != 0;
    int indexReg = (ext >> 12) & 0xF;
    bool isAddr = (ext & 0x8000) != 0;
    int scale = 1 << ((ext >> 9) & 3);
    std::string idxReg = isAddr ? std::format("A{}", indexReg & 7) : std::format("D{}", indexReg & 7);
    std::string idxSize = isLong ? ".L" : ".W";
    std::string scaleStr = scale > 1 ? std::format("*{}", scale) : "";

    if ((ext & 0x0100) != 0) {
        // Full extension
        int bdSize = (ext >> 4) & 3;
        bool bs = (ext & 0x0080) != 0;
        bool is_ = (ext & 0x0040) != 0;
        std::string bd;
        if (bdSize == 2) bd = std::format("${:X}", static_cast<int16_t>(ReadWord()));
        else if (bdSize == 3) bd = std::format("${:08X}", ReadLong());

        std::string bStr = bs ? "" : baseReg;
        std::string iStr = is_ ? "" : (idxReg + idxSize + scaleStr);

        if ((ext & 0x0004) == 0)
            return std::format("({},{},{})", bd, bStr, iStr);
        else {
            int iis = ext & 3;
            std::string od;
            if (iis == 2) od = std::format("${:X}", static_cast<int16_t>(ReadWord()));
            else if (iis == 3) od = std::format("${:08X}", ReadLong());
            return std::format("([{},{}],{},{})", bd, bStr, iStr, od);
        }
    } else {
        int8_t disp = static_cast<int8_t>(ext & 0xFF);
        return std::format("({},{},{}{}{})", static_cast<int>(disp), baseReg, idxReg, idxSize, scaleStr);
    }
}

// ============================================================================
// FormatRegisterList
// ============================================================================

std::string Disassembler::FormatRegisterList(uint16_t mask, bool reverse)
{
    std::string result;
    for (int i = 0; i < 16; i++) {
        int bit = reverse ? (15 - i) : i;
        if ((mask & (1 << i)) != 0) {
            if (!result.empty()) result += '/';
            if (bit < 8)
                result += std::format("D{}", bit);
            else
                result += std::format("A{}", bit - 8);
        }
    }
    return result;
}

// ============================================================================
// Group 0 - Bit manipulation / MOVEP / Immediate
// ============================================================================

void Disassembler::DisasmGroup0(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if ((opcode & 0x0100) != 0) {
        if (mode == 1) {
            // MOVEP
            int16_t disp = static_cast<int16_t>(ReadWord());
            int opMode = (opcode >> 6) & 7;
            std::string suf = (opMode == 4 || opMode == 6) ? ".W" : ".L";
            line.Mnemonic = std::string("MOVEP") + suf;

            if (opMode >= 6)
                line.Operands = std::format("D{},({},A{})", reg, disp, eaReg);
            else
                line.Operands = std::format("({},A{}),D{}", disp, eaReg, reg);
            return;
        }
        const char* bitOps[] = { "BTST", "BCHG", "BCLR", "BSET" };
        int op = (opcode >> 6) & 3;
        line.Mnemonic = bitOps[op];
        std::string ea = FormatEA(mode, eaReg, mode == 0 ? 4 : 1);
        line.Operands = std::format("D{},{}", reg, ea);
        return;
    }

    switch (reg) {
        case 0:
            if (((opcode >> 6) & 3) == 3) DisasmCMP2_CHK2(opcode, line, 1);
            else DisasmImmediate(opcode, line, "ORI");
            break;
        case 1:
            if (((opcode >> 6) & 3) == 3) DisasmCMP2_CHK2(opcode, line, 2);
            else DisasmImmediate(opcode, line, "ANDI");
            break;
        case 2:
            if (((opcode >> 6) & 3) == 3) DisasmCMP2_CHK2(opcode, line, 4);
            else DisasmImmediate(opcode, line, "SUBI");
            break;
        case 3:
            DisasmImmediate(opcode, line, "ADDI");
            break;
        case 4: {
            const char* bitOps[] = { "BTST", "BCHG", "BCLR", "BSET" };
            int bitOp = (opcode >> 6) & 3;
            int bitNum = ReadWord() & 0xFF;
            line.Mnemonic = bitOps[bitOp];
            std::string ea = FormatEA(mode, eaReg, mode == 0 ? 4 : 1);
            line.Operands = std::format("#${:02X},{}", bitNum, ea);
            break;
        }
        case 5:
            if (((opcode >> 6) & 3) == 3) DisasmCAS(opcode, line, 1);
            else DisasmImmediate(opcode, line, "EORI");
            break;
        case 6:
            if (((opcode >> 6) & 3) == 3) DisasmCAS(opcode, line, 2);
            else DisasmImmediate(opcode, line, "CMPI");
            break;
        case 7:
            if (((opcode >> 6) & 3) == 3) DisasmCAS(opcode, line, 4);
            else {
                uint16_t ext = ReadWord();
                int rn = (ext >> 12) & 0xF;
                int size = GetSize2(opcode);
                line.Mnemonic = std::string("MOVES") + SizeSuffix(size);
                std::string ea = FormatEA(mode, eaReg, size);
                std::string rStr = rn < 8 ? std::format("D{}", rn) : std::format("A{}", rn - 8);
                if ((ext & 0x0800) != 0)
                    line.Operands = std::format("{},{}", rStr, ea);
                else
                    line.Operands = std::format("{},{}", ea, rStr);
            }
            break;
    }
}

// ============================================================================
// Immediate instructions (ORI, ANDI, SUBI, ADDI, EORI, CMPI)
// ============================================================================

void Disassembler::DisasmImmediate(uint16_t opcode, DisasmLine& line, const std::string& name)
{
    int size = GetSize2(opcode);
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;

    if (mode == 7 && reg == 4) {
        uint16_t imm = ReadWord();
        if (size == 1) {
            line.Mnemonic = name;
            line.Operands = std::format("#${:02X},CCR", imm & 0xFF);
        } else {
            line.Mnemonic = name;
            line.Operands = std::format("#${:04X},SR", imm);
        }
        return;
    }

    std::string immStr;
    if (size == 1) {
        uint16_t v = ReadWord();
        immStr = std::format("#${:02X}", v & 0xFF);
    } else if (size == 2) {
        uint16_t v = ReadWord();
        immStr = std::format("#${:04X}", v);
    } else {
        uint32_t v = ReadLong();
        immStr = std::format("#${:08X}", v);
    }

    std::string ea = FormatEA(mode, reg, size);
    line.Mnemonic = name + SizeSuffix(size);
    line.Operands = std::format("{},{}", immStr, ea);
}

// ============================================================================
// MOVE / MOVEA
// ============================================================================

void Disassembler::DisasmMOVE(uint16_t opcode, DisasmLine& line, int size)
{
    int srcMode = (opcode >> 3) & 7;
    int srcReg = opcode & 7;
    int dstReg = (opcode >> 9) & 7;
    int dstMode = (opcode >> 6) & 7;

    std::string src = FormatEA(srcMode, srcReg, size);

    if (dstMode == 1) {
        line.Mnemonic = std::string("MOVEA") + SizeSuffix(size);
        line.Operands = std::format("{},A{}", src, dstReg);
        return;
    }

    std::string dst = FormatEA(dstMode, dstReg, size);
    line.Mnemonic = std::string("MOVE") + SizeSuffix(size);
    line.Operands = std::format("{},{}", src, dst);
}

// ============================================================================
// Group 4 - Miscellaneous
// ============================================================================

void Disassembler::DisasmGroup4(uint16_t opcode, DisasmLine& line)
{
    // Special instructions
    if ((opcode & 0xFFC0) == 0x40C0) {
        line.Mnemonic = "MOVE";
        line.Operands = std::format("SR,{}", FormatEA((opcode >> 3) & 7, opcode & 7, 2));
        return;
    }
    if ((opcode & 0xFFC0) == 0x44C0) {
        line.Mnemonic = "MOVE";
        line.Operands = std::format("{},CCR", FormatEA((opcode >> 3) & 7, opcode & 7, 2));
        return;
    }
    if ((opcode & 0xFFC0) == 0x46C0) {
        line.Mnemonic = "MOVE";
        line.Operands = std::format("{},SR", FormatEA((opcode >> 3) & 7, opcode & 7, 2));
        return;
    }

    if ((opcode & 0xFF00) == 0x4000 && ((opcode >> 6) & 3) != 3) {
        int size = GetSize2(opcode);
        line.Mnemonic = std::string("NEGX") + SizeSuffix(size);
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, size);
        return;
    }
    if ((opcode & 0xFF00) == 0x4200 && ((opcode >> 6) & 3) != 3) {
        int size = GetSize2(opcode);
        line.Mnemonic = std::string("CLR") + SizeSuffix(size);
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, size);
        return;
    }
    if ((opcode & 0xFF00) == 0x4400 && ((opcode >> 6) & 3) != 3) {
        int size = GetSize2(opcode);
        line.Mnemonic = std::string("NEG") + SizeSuffix(size);
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, size);
        return;
    }
    if ((opcode & 0xFF00) == 0x4600 && ((opcode >> 6) & 3) != 3) {
        int size = GetSize2(opcode);
        line.Mnemonic = std::string("NOT") + SizeSuffix(size);
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, size);
        return;
    }

    if ((opcode & 0xFFF8) == 0x4880) { line.Mnemonic = "EXT.W"; line.Operands = std::format("D{}", opcode & 7); return; }
    if ((opcode & 0xFFF8) == 0x48C0) { line.Mnemonic = "EXT.L"; line.Operands = std::format("D{}", opcode & 7); return; }
    if ((opcode & 0xFFF8) == 0x49C0) { line.Mnemonic = "EXTB.L"; line.Operands = std::format("D{}", opcode & 7); return; }

    if ((opcode & 0xFFF8) == 0x4840) { line.Mnemonic = "SWAP"; line.Operands = std::format("D{}", opcode & 7); return; }

    if ((opcode & 0xFFC0) == 0x4840) {
        line.Mnemonic = "PEA";
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, 4);
        return;
    }

    if ((opcode & 0xFF00) == 0x4A00 && ((opcode >> 6) & 3) != 3) {
        int size = GetSize2(opcode);
        line.Mnemonic = std::string("TST") + SizeSuffix(size);
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, size);
        return;
    }
    if ((opcode & 0xFFC0) == 0x4AC0) {
        line.Mnemonic = "TAS";
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, 1);
        return;
    }

    if (opcode == 0x4AFC) { line.Mnemonic = "ILLEGAL"; return; }

    // MOVEM
    if ((opcode & 0xFB80) == 0x4880) {
        bool isLong = (opcode & 0x0040) != 0;
        bool toRegs = (opcode & 0x0400) != 0;
        uint16_t mask = ReadWord();
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;
        bool reverse = (mode == 4);

        std::string regs = FormatRegisterList(mask, reverse);
        std::string ea = FormatEA(mode, reg, isLong ? 4 : 2);

        line.Mnemonic = isLong ? "MOVEM.L" : "MOVEM.W";
        if (toRegs)
            line.Operands = std::format("{},{}", ea, regs);
        else
            line.Operands = std::format("{},{}", regs, ea);
        return;
    }

    // LEA
    if ((opcode & 0xF1C0) == 0x41C0) {
        int areg = (opcode >> 9) & 7;
        line.Mnemonic = "LEA";
        line.Operands = std::format("{},A{}", FormatEA((opcode >> 3) & 7, opcode & 7, 4), areg);
        return;
    }

    // CHK
    if ((opcode & 0xF1C0) == 0x4180) {
        int dreg = (opcode >> 9) & 7;
        line.Mnemonic = "CHK.W";
        line.Operands = std::format("{},D{}", FormatEA((opcode >> 3) & 7, opcode & 7, 2), dreg);
        return;
    }

    // LINK
    if ((opcode & 0xFFF8) == 0x4E50) {
        int16_t disp = static_cast<int16_t>(ReadWord());
        line.Mnemonic = "LINK";
        line.Operands = std::format("A{},#${:04X}", opcode & 7, static_cast<uint16_t>(disp));
        return;
    }
    if ((opcode & 0xFFF8) == 0x4808) {
        int32_t disp = static_cast<int32_t>(ReadLong());
        line.Mnemonic = "LINK.L";
        line.Operands = std::format("A{},#${:08X}", opcode & 7, static_cast<uint32_t>(disp));
        return;
    }
    // UNLK
    if ((opcode & 0xFFF8) == 0x4E58) {
        line.Mnemonic = "UNLK";
        line.Operands = std::format("A{}", opcode & 7);
        return;
    }

    // MOVE USP
    if ((opcode & 0xFFF0) == 0x4E60) {
        if ((opcode & 0x0008) != 0) {
            line.Mnemonic = "MOVE";
            line.Operands = std::format("USP,A{}", opcode & 7);
        } else {
            line.Mnemonic = "MOVE";
            line.Operands = std::format("A{},USP", opcode & 7);
        }
        return;
    }

    if (opcode == 0x4E70) { line.Mnemonic = "RESET"; return; }
    if (opcode == 0x4E71) { line.Mnemonic = "NOP"; return; }
    if (opcode == 0x4E72) {
        uint16_t imm = ReadWord();
        line.Mnemonic = "STOP";
        line.Operands = std::format("#${:04X}", imm);
        return;
    }
    if (opcode == 0x4E73) { line.Mnemonic = "RTE"; return; }
    if (opcode == 0x4E74) {
        int16_t disp = static_cast<int16_t>(ReadWord());
        line.Mnemonic = "RTD";
        line.Operands = std::format("#${:04X}", static_cast<uint16_t>(disp));
        return;
    }
    if (opcode == 0x4E75) { line.Mnemonic = "RTS"; return; }
    if (opcode == 0x4E77) { line.Mnemonic = "RTR"; return; }

    // MOVEC
    if ((opcode & 0xFFFE) == 0x4E7A) {
        uint16_t ext = ReadWord();
        int rn = (ext >> 12) & 0xF;
        int creg = ext & 0xFFF;
        std::string rStr = rn < 8 ? std::format("D{}", rn) : std::format("A{}", rn - 8);
        std::string cStr;
        switch (creg) {
            case 0x000: cStr = "SFC"; break;
            case 0x001: cStr = "DFC"; break;
            case 0x002: cStr = "CACR"; break;
            case 0x800: cStr = "USP"; break;
            case 0x801: cStr = "VBR"; break;
            case 0x802: cStr = "CAAR"; break;
            case 0x803: cStr = "MSP"; break;
            case 0x804: cStr = "ISP"; break;
            default: cStr = std::format("CR${:03X}", creg); break;
        }
        line.Mnemonic = "MOVEC";
        // 0x4E7A = MOVEC Rc,Rn (read control reg), 0x4E7B = MOVEC Rn,Rc (write control reg)
        if ((opcode & 1) == 0)
            line.Operands = std::format("{},{}", cStr, rStr);
        else
            line.Operands = std::format("{},{}", rStr, cStr);
        return;
    }

    // TRAP
    if ((opcode & 0xFFF0) == 0x4E40) {
        line.Mnemonic = "TRAP";
        line.Operands = std::format("#{}", opcode & 0xF);
        return;
    }

    // JSR
    if ((opcode & 0xFFC0) == 0x4E80) {
        line.Mnemonic = "JSR";
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, 4);
        return;
    }

    // JMP
    if ((opcode & 0xFFC0) == 0x4EC0) {
        line.Mnemonic = "JMP";
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, 4);
        return;
    }

    // NBCD
    if ((opcode & 0xFFC0) == 0x4800) {
        line.Mnemonic = "NBCD";
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, 1);
        return;
    }

    // MULS.L / MULU.L (68020+)
    if ((opcode & 0xFFC0) == 0x4C00) {
        uint16_t ext = ReadWord();
        int dl = (ext >> 12) & 7;
        int dh = ext & 7;
        bool signed_ = (ext & 0x0800) != 0;
        bool quad = (ext & 0x0400) != 0;
        line.Mnemonic = signed_ ? "MULS.L" : "MULU.L";
        std::string ea = FormatEA((opcode >> 3) & 7, opcode & 7, 4);
        if (quad)
            line.Operands = std::format("{},D{}:D{}", ea, dh, dl);
        else
            line.Operands = std::format("{},D{}", ea, dl);
        return;
    }

    // DIVS.L / DIVU.L (68020+)
    if ((opcode & 0xFFC0) == 0x4C40) {
        uint16_t ext = ReadWord();
        int dq = (ext >> 12) & 7;
        int dr = ext & 7;
        bool signed_ = (ext & 0x0800) != 0;
        bool quad = (ext & 0x0400) != 0;
        line.Mnemonic = signed_ ? "DIVS.L" : "DIVU.L";
        std::string ea = FormatEA((opcode >> 3) & 7, opcode & 7, 4);
        if (quad || dr != dq)
            line.Operands = std::format("{},D{}:D{}", ea, dr, dq);
        else
            line.Operands = std::format("{},D{}", ea, dq);
        return;
    }

    line.Mnemonic = "DC.W";
    line.Operands = std::format("${:04X}", opcode);
}

// ============================================================================
// Group 5 - ADDQ / SUBQ / Scc / DBcc / TRAPcc
// ============================================================================

void Disassembler::DisasmGroup5(uint16_t opcode, DisasmLine& line)
{
    int sizeField = (opcode >> 6) & 3;
    int cond = (opcode >> 8) & 0xF;

    if (sizeField == 3) {
        int mode = (opcode >> 3) & 7;
        int reg = opcode & 7;

        if (mode == 1) {
            uint32_t pcBefore = m_pc;
            int16_t disp = static_cast<int16_t>(ReadWord());
            line.Mnemonic = std::string("DB") + CondCodes[cond];
            line.Operands = std::format("D{},${:08X}", reg, static_cast<uint32_t>(pcBefore + disp));
            return;
        }

        if (mode == 7 && reg >= 2 && reg <= 4) {
            line.Mnemonic = std::string("TRAP") + CondCodes[cond];
            if (reg == 2) {
                uint16_t w = ReadWord();
                line.Operands = std::format("#${:04X}", w);
            } else if (reg == 3) {
                uint32_t l = ReadLong();
                line.Operands = std::format("#${:08X}", l);
            }
            return;
        }

        line.Mnemonic = std::string("S") + CondCodes[cond];
        line.Operands = FormatEA(mode, reg, 1);
        return;
    }

    int data = (opcode >> 9) & 7;
    if (data == 0) data = 8;
    int size;
    switch (sizeField) {
        case 0: size = 1; break;
        case 1: size = 2; break;
        default: size = 4; break;
    }
    bool isSub = (opcode & 0x0100) != 0;

    line.Mnemonic = std::string(isSub ? "SUBQ" : "ADDQ") + SizeSuffix(size);
    int eaMode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;
    std::string ea = FormatEA(eaMode, eaReg, size);
    line.Operands = std::format("#{},{}", data, ea);
}

// ============================================================================
// Group 6 - Bcc / BRA / BSR
// ============================================================================

void Disassembler::DisasmGroup6(uint16_t opcode, DisasmLine& line)
{
    int cond = (opcode >> 8) & 0xF;
    int disp8 = static_cast<int8_t>(opcode & 0xFF);
    uint32_t savedPC = m_pc;

    int displacement;
    std::string suffix;
    if (disp8 == 0) {
        displacement = static_cast<int16_t>(ReadWord());
        suffix = ".W";
    } else if (disp8 == -1) {
        displacement = static_cast<int32_t>(ReadLong());
        suffix = ".L";
    } else {
        displacement = disp8;
        suffix = ".S";
    }

    uint32_t target = static_cast<uint32_t>(savedPC + displacement);

    std::string mnemonic;
    switch (cond) {
        case 0: mnemonic = "BRA"; break;
        case 1: mnemonic = "BSR"; break;
        default: mnemonic = std::string("B") + CondCodes[cond]; break;
    }

    line.Mnemonic = mnemonic + suffix;
    line.Operands = std::format("${:08X}", target);
}

// ============================================================================
// MOVEQ
// ============================================================================

void Disassembler::DisasmMOVEQ(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    line.Mnemonic = "MOVEQ";
    line.Operands = std::format("#${:02X},D{}", static_cast<uint8_t>(opcode & 0xFF), reg);
}

// ============================================================================
// Group 8 - OR / DIV / SBCD / PACK / UNPK
// ============================================================================

void Disassembler::DisasmGroup8(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if (opMode == 3) {
        line.Mnemonic = "DIVU.W";
        line.Operands = std::format("{},D{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }
    if (opMode == 7) {
        line.Mnemonic = "DIVS.W";
        line.Operands = std::format("{},D{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }
    if (opMode == 4 && (mode == 0 || mode == 1)) {
        line.Mnemonic = "SBCD";
        if (mode == 0)
            line.Operands = std::format("D{},D{}", eaReg, reg);
        else
            line.Operands = std::format("-(A{}),-(A{})", eaReg, reg);
        return;
    }
    if (opMode == 5 && (mode == 0 || mode == 1)) {
        uint16_t adj = ReadWord();
        line.Mnemonic = "PACK";
        if (mode == 0)
            line.Operands = std::format("D{},D{},#${:04X}", eaReg, reg, adj);
        else
            line.Operands = std::format("-(A{}),-(A{}),#${:04X}", eaReg, reg, adj);
        return;
    }
    if (opMode == 6 && (mode == 0 || mode == 1)) {
        uint16_t adj = ReadWord();
        line.Mnemonic = "UNPK";
        if (mode == 0)
            line.Operands = std::format("D{},D{},#${:04X}", eaReg, reg, adj);
        else
            line.Operands = std::format("-(A{}),-(A{}),#${:04X}", eaReg, reg, adj);
        return;
    }

    int size;
    switch (opMode) {
        case 0: size = 1; break;
        case 1: size = 2; break;
        case 2: size = 4; break;
        case 4: size = 1; break;
        case 5: size = 2; break;
        case 6: size = 4; break;
        default: size = 2; break;
    }
    line.Mnemonic = std::string("OR") + SizeSuffix(size);
    std::string ea = FormatEA(mode, eaReg, size);
    if ((opMode & 4) != 0)
        line.Operands = std::format("D{},{}", reg, ea);
    else
        line.Operands = std::format("{},D{}", ea, reg);
}

// ============================================================================
// Group 9 - SUB / SUBA / SUBX
// ============================================================================

void Disassembler::DisasmGroup9(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if (opMode == 3) {
        line.Mnemonic = "SUBA.W";
        line.Operands = std::format("{},A{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }
    if (opMode == 7) {
        line.Mnemonic = "SUBA.L";
        line.Operands = std::format("{},A{}", FormatEA(mode, eaReg, 4), reg);
        return;
    }

    if ((opMode == 4 || opMode == 5 || opMode == 6) && (mode == 0 || mode == 1)) {
        int size;
        switch (opMode) {
            case 4: size = 1; break;
            case 5: size = 2; break;
            default: size = 4; break;
        }
        line.Mnemonic = std::string("SUBX") + SizeSuffix(size);
        if (mode == 0)
            line.Operands = std::format("D{},D{}", eaReg, reg);
        else
            line.Operands = std::format("-(A{}),-(A{})", eaReg, reg);
        return;
    }

    int sz;
    switch (opMode) {
        case 0: sz = 1; break;
        case 1: sz = 2; break;
        case 2: sz = 4; break;
        case 4: sz = 1; break;
        case 5: sz = 2; break;
        case 6: sz = 4; break;
        default: sz = 2; break;
    }
    line.Mnemonic = std::string("SUB") + SizeSuffix(sz);
    std::string ea = FormatEA(mode, eaReg, sz);
    if ((opMode & 4) != 0)
        line.Operands = std::format("D{},{}", reg, ea);
    else
        line.Operands = std::format("{},D{}", ea, reg);
}

// ============================================================================
// Group B - CMP / CMPA / CMPM / EOR
// ============================================================================

void Disassembler::DisasmGroupB(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if (opMode == 3) {
        line.Mnemonic = "CMPA.W";
        line.Operands = std::format("{},A{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }
    if (opMode == 7) {
        line.Mnemonic = "CMPA.L";
        line.Operands = std::format("{},A{}", FormatEA(mode, eaReg, 4), reg);
        return;
    }

    if ((opMode == 4 || opMode == 5 || opMode == 6) && mode == 1) {
        int size;
        switch (opMode) {
            case 4: size = 1; break;
            case 5: size = 2; break;
            default: size = 4; break;
        }
        line.Mnemonic = std::string("CMPM") + SizeSuffix(size);
        line.Operands = std::format("(A{})+,(A{})+", eaReg, reg);
        return;
    }

    if (opMode >= 4) {
        int size;
        switch (opMode) {
            case 4: size = 1; break;
            case 5: size = 2; break;
            default: size = 4; break;
        }
        line.Mnemonic = std::string("EOR") + SizeSuffix(size);
        line.Operands = std::format("D{},{}", reg, FormatEA(mode, eaReg, size));
        return;
    }

    int sz;
    switch (opMode) {
        case 0: sz = 1; break;
        case 1: sz = 2; break;
        default: sz = 4; break;
    }
    line.Mnemonic = std::string("CMP") + SizeSuffix(sz);
    line.Operands = std::format("{},D{}", FormatEA(mode, eaReg, sz), reg);
}

// ============================================================================
// Group C - AND / MUL / ABCD / EXG
// ============================================================================

void Disassembler::DisasmGroupC(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if (opMode == 3) {
        line.Mnemonic = "MULU.W";
        line.Operands = std::format("{},D{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }
    if (opMode == 7) {
        line.Mnemonic = "MULS.W";
        line.Operands = std::format("{},D{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }

    if (opMode == 4 && (mode == 0 || mode == 1)) {
        line.Mnemonic = "ABCD";
        if (mode == 0)
            line.Operands = std::format("D{},D{}", eaReg, reg);
        else
            line.Operands = std::format("-(A{}),-(A{})", eaReg, reg);
        return;
    }

    if (opMode == 5 && mode == 0) { line.Mnemonic = "EXG"; line.Operands = std::format("D{},D{}", reg, eaReg); return; }
    if (opMode == 5 && mode == 1) { line.Mnemonic = "EXG"; line.Operands = std::format("A{},A{}", reg, eaReg); return; }
    if (opMode == 6 && mode == 1) { line.Mnemonic = "EXG"; line.Operands = std::format("D{},A{}", reg, eaReg); return; }

    int size;
    switch (opMode) {
        case 0: size = 1; break;
        case 1: size = 2; break;
        case 2: size = 4; break;
        case 4: size = 1; break;
        case 5: size = 2; break;
        case 6: size = 4; break;
        default: size = 2; break;
    }
    line.Mnemonic = std::string("AND") + SizeSuffix(size);
    std::string ea = FormatEA(mode, eaReg, size);
    if ((opMode & 4) != 0)
        line.Operands = std::format("D{},{}", reg, ea);
    else
        line.Operands = std::format("{},D{}", ea, reg);
}

// ============================================================================
// Group D - ADD / ADDA / ADDX
// ============================================================================

void Disassembler::DisasmGroupD(uint16_t opcode, DisasmLine& line)
{
    int reg = (opcode >> 9) & 7;
    int opMode = (opcode >> 6) & 7;
    int mode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    if (opMode == 3) {
        line.Mnemonic = "ADDA.W";
        line.Operands = std::format("{},A{}", FormatEA(mode, eaReg, 2), reg);
        return;
    }
    if (opMode == 7) {
        line.Mnemonic = "ADDA.L";
        line.Operands = std::format("{},A{}", FormatEA(mode, eaReg, 4), reg);
        return;
    }

    if ((opMode == 4 || opMode == 5 || opMode == 6) && (mode == 0 || mode == 1)) {
        int size;
        switch (opMode) {
            case 4: size = 1; break;
            case 5: size = 2; break;
            default: size = 4; break;
        }
        line.Mnemonic = std::string("ADDX") + SizeSuffix(size);
        if (mode == 0)
            line.Operands = std::format("D{},D{}", eaReg, reg);
        else
            line.Operands = std::format("-(A{}),-(A{})", eaReg, reg);
        return;
    }

    int sz;
    switch (opMode) {
        case 0: sz = 1; break;
        case 1: sz = 2; break;
        case 2: sz = 4; break;
        case 4: sz = 1; break;
        case 5: sz = 2; break;
        case 6: sz = 4; break;
        default: sz = 2; break;
    }
    line.Mnemonic = std::string("ADD") + SizeSuffix(sz);
    std::string ea = FormatEA(mode, eaReg, sz);
    if ((opMode & 4) != 0)
        line.Operands = std::format("D{},{}", reg, ea);
    else
        line.Operands = std::format("{},D{}", ea, reg);
}

// ============================================================================
// Group E - Shifts / Rotates / Bit Fields
// ============================================================================

void Disassembler::DisasmGroupE(uint16_t opcode, DisasmLine& line)
{
    int sizeField = (opcode >> 6) & 3;

    if (sizeField == 3) {
        if ((opcode & 0x0800) != 0) {
            // Bit field instructions
            DisasmBitField(opcode, line);
            return;
        }

        const char* ops[] = { "AS", "LS", "ROX", "RO" };
        int type = (opcode >> 9) & 3;
        bool left = (opcode & 0x0100) != 0;
        std::string dir = left ? "L" : "R";
        line.Mnemonic = std::string(ops[type]) + dir + ".W";
        line.Operands = FormatEA((opcode >> 3) & 7, opcode & 7, 2);
        return;
    }

    int count = (opcode >> 9) & 7;
    bool ir = (opcode & 0x0020) != 0;
    int dreg = opcode & 7;
    int size;
    switch (sizeField) {
        case 0: size = 1; break;
        case 1: size = 2; break;
        default: size = 4; break;
    }
    bool isLeft = (opcode & 0x0100) != 0;
    int shiftType = (opcode >> 3) & 3;

    const char* shiftOps[] = { "AS", "LS", "ROX", "RO" };
    std::string dir2 = isLeft ? "L" : "R";
    line.Mnemonic = std::string(shiftOps[shiftType]) + dir2 + SizeSuffix(size);

    if (ir)
        line.Operands = std::format("D{},D{}", count, dreg);
    else {
        int cnt = count == 0 ? 8 : count;
        line.Operands = std::format("#{},D{}", cnt, dreg);
    }
}

// ============================================================================
// Group F - Coprocessor (FPU / MMU)
// ============================================================================

void Disassembler::DisasmGroupF(uint16_t opcode, DisasmLine& line)
{
    int cpId = (opcode >> 9) & 7;

    if (cpId == 0) { // MMU
        DisasmMMU(opcode, line);
        return;
    }

    if (cpId == 1) { // FPU
        DisasmFPU(opcode, line);
        return;
    }

    line.Mnemonic = "DC.W";
    line.Operands = std::format("${:04X}", opcode);
}

// ============================================================================
// MMU instructions
// ============================================================================

void Disassembler::DisasmMMU(uint16_t opcode, DisasmLine& line)
{
    uint16_t ext = ReadWord();
    int mmuOp = (ext >> 13) & 7;
    std::string ea = FormatEA((opcode >> 3) & 7, opcode & 7, 4);
    bool toMem = (ext & 0x0200) != 0;

    switch (mmuOp) {
        case 0: { // PMOVE TT0/TT1
            int pmReg = (ext >> 10) & 7;
            std::string regName = (pmReg >= 0 && pmReg < 4 && !MmuTTRegNames[pmReg].empty())
                ? MmuTTRegNames[pmReg] : std::format("TT?{}", pmReg);
            line.Mnemonic = "PMOVE";
            line.Operands = toMem ? std::format("{},{}", regName, ea) : std::format("{},{}", ea, regName);
            return;
        }

        case 1: { // PFLUSH / PFLUSHA / PLOAD
            if (ext == 0x2400) {
                line.Mnemonic = "PFLUSHA";
                return;
            }
            // PLOAD: bits 12-11=00, bits 4-1=0000
            if ((ext & 0x1800) == 0 && (ext & 0x001E) == 0) {
                bool isRead = (ext & 0x0200) != 0;
                line.Mnemonic = isRead ? "PLOADR" : "PLOADW";
                line.Operands = std::format("#FC,{}", ea);
                return;
            }
            // PFLUSH
            uint8_t mask = static_cast<uint8_t>((ext >> 5) & 0xF);
            bool hasEA = (ext & 0x0010) != 0;
            line.Mnemonic = "PFLUSH";
            line.Operands = hasEA ? std::format("#FC,#{},{}", mask, ea) : std::format("#FC,#{}", mask);
            return;
        }

        case 2: { // PMOVE TC/SRP/CRP
            int pmReg = (ext >> 10) & 7;
            std::string regName = (pmReg >= 0 && pmReg < 4 && !MmuTCSRegNames[pmReg].empty())
                ? MmuTCSRegNames[pmReg] : std::format("???{}", pmReg);
            int regSize = (pmReg == 0) ? 4 : 8; // TC=4bytes, SRP/CRP=8bytes
            line.Mnemonic = "PMOVE";
            if (regSize == 8 && !toMem)
                ea = FormatEA((opcode >> 3) & 7, opcode & 7, 4); // 8-byte read
            line.Operands = toMem ? std::format("{},{}", regName, ea) : std::format("{},{}", ea, regName);
            return;
        }

        case 3: { // PMOVE MMUSR
            std::string ea16 = FormatEA((opcode >> 3) & 7, opcode & 7, 2);
            line.Mnemonic = "PMOVE";
            line.Operands = toMem ? std::format("MMUSR,{}", ea16) : std::format("{},MMUSR", ea16);
            return;
        }

        case 4: { // PTEST
            int level = (ext >> 10) & 7;
            bool isRead = (ext & 0x0200) != 0;
            bool hasAReg = (ext & 0x0100) != 0;
            int aReg = (ext >> 5) & 7;
            line.Mnemonic = isRead ? "PTESTR" : "PTESTW";
            line.Operands = hasAReg
                ? std::format("#FC,{},#{},A{}", ea, level, aReg)
                : std::format("#FC,{},#{}", ea, level);
            return;
        }
    }

    line.Mnemonic = "DC.W";
    line.Operands = std::format("${:04X}", opcode);
}

// ============================================================================
// FPU instructions
// ============================================================================

void Disassembler::DisasmFPU(uint16_t opcode, DisasmLine& line)
{
    int type = (opcode >> 6) & 7;
    int eaMode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    switch (type) {
        case 0: // General FPU
            DisasmFPUGeneral(opcode, line, eaMode, eaReg);
            break;

        case 1: { // FDBcc / FScc / FTRAPcc
            uint16_t cmdWord = ReadWord();
            int cond = cmdWord & 0x3F;
            std::string condName = cond < 32 ? FpuCondNames[cond] : std::format("#{}", cond);

            if (eaMode == 1) { // FDBcc
                int16_t disp = static_cast<int16_t>(ReadWord());
                uint32_t target = static_cast<uint32_t>(m_pc - 2 + disp);
                line.Mnemonic = std::string("FDB") + condName;
                line.Operands = std::format("D{},${:08X}", eaReg, target);
            } else if (eaMode == 7 && eaReg == 2) { // FTRAPcc.W
                uint16_t imm = ReadWord();
                line.Mnemonic = std::string("FTRAP") + condName + ".W";
                line.Operands = std::format("#${:04X}", imm);
            } else if (eaMode == 7 && eaReg == 3) { // FTRAPcc.L
                uint32_t imm = ReadLong();
                line.Mnemonic = std::string("FTRAP") + condName + ".L";
                line.Operands = std::format("#${:08X}", imm);
            } else if (eaMode == 7 && eaReg == 4) { // FTRAPcc
                line.Mnemonic = std::string("FTRAP") + condName;
            } else { // FScc
                line.Mnemonic = std::string("FS") + condName;
                line.Operands = FormatEA(eaMode, eaReg, 1);
            }
            break;
        }

        case 2: { // FBcc.W
            int cond = opcode & 0x3F;
            int16_t disp = static_cast<int16_t>(ReadWord());
            uint32_t target = static_cast<uint32_t>(m_pc - 2 + disp);
            std::string condName = cond < 32 ? FpuCondNames[cond] : std::format("#{}", cond);
            line.Mnemonic = std::string("FB") + condName + ".W";
            line.Operands = std::format("${:08X}", target);
            break;
        }

        case 3: { // FBcc.L
            int cond = opcode & 0x3F;
            int32_t disp = static_cast<int32_t>(ReadLong());
            uint32_t target = static_cast<uint32_t>(m_pc - 4 + disp);
            std::string condName = cond < 32 ? FpuCondNames[cond] : std::format("#{}", cond);
            line.Mnemonic = std::string("FB") + condName + ".L";
            line.Operands = std::format("${:08X}", target);
            break;
        }

        case 4: // FSAVE
            line.Mnemonic = "FSAVE";
            line.Operands = FormatEA(eaMode, eaReg, 4);
            break;

        case 5: // FRESTORE
            line.Mnemonic = "FRESTORE";
            line.Operands = FormatEA(eaMode, eaReg, 4);
            break;

        default:
            line.Mnemonic = "DC.W";
            line.Operands = std::format("${:04X}", opcode);
            break;
    }
}

// ============================================================================
// FPU General (type 0)
// ============================================================================

void Disassembler::DisasmFPUGeneral(uint16_t opcode, DisasmLine& line, int eaMode, int eaReg)
{
    uint16_t cmdWord = ReadWord();
    int cmdType = (cmdWord >> 13) & 7;

    switch (cmdType) {
        case 0: { // Register to register
            int srcReg = (cmdWord >> 10) & 7;
            int dstReg = (cmdWord >> 7) & 7;
            int op = cmdWord & 0x7F;
            std::string opName = GetFpuOpName(op);
            if (op == 0x3A) // FTST
                line.Operands = std::format("FP{}", srcReg);
            else if (op >= 0x30 && op <= 0x37)
                line.Operands = std::format("FP{},FP{}:FP{}", srcReg, op & 7, dstReg);
            else if (op == 0x00 && srcReg == dstReg)
                line.Operands = std::format("FP{}", dstReg);
            else
                line.Operands = std::format("FP{},FP{}", srcReg, dstReg);
            line.Mnemonic = opName;
            break;
        }

        case 2: { // EA to register
            int srcFormat = (cmdWord >> 10) & 7;
            int dstReg = (cmdWord >> 7) & 7;
            int op = cmdWord & 0x7F;
            std::string opName = GetFpuOpName(op);
            std::string fmtSuffix = FpuFormatName(srcFormat);
            std::string ea = FormatEAForFpu(eaMode, eaReg, srcFormat);
            if (op == 0x3A) // FTST
                line.Operands = ea;
            else if (op >= 0x30 && op <= 0x37)
                line.Operands = std::format("{},FP{}:FP{}", ea, op & 7, dstReg);
            else
                line.Operands = std::format("{},FP{}", ea, dstReg);
            line.Mnemonic = opName + fmtSuffix;
            break;
        }

        case 3: { // Register to EA (FMOVE)
            int dstFormat = (cmdWord >> 10) & 7;
            int srcReg = (cmdWord >> 7) & 7;
            std::string fmtSuffix = FpuFormatName(dstFormat);
            std::string ea = FormatEAForFpu(eaMode, eaReg, dstFormat);
            line.Mnemonic = std::string("FMOVE") + fmtSuffix;
            line.Operands = std::format("FP{},{}", srcReg, ea);
            break;
        }

        case 4: // EA to control register
        case 5: { // Control register to EA
            int regSelect = (cmdWord >> 10) & 7;
            std::string regList = FormatFpuControlRegs(regSelect);
            std::string ea = FormatEA(eaMode, eaReg, 4);
            if (cmdType == 4) {
                line.Mnemonic = (regSelect != 0 && (regSelect & (regSelect - 1)) != 0) ? "FMOVEM.L" : "FMOVE.L";
                line.Operands = std::format("{},{}", ea, regList);
            } else {
                line.Mnemonic = (regSelect != 0 && (regSelect & (regSelect - 1)) != 0) ? "FMOVEM.L" : "FMOVE.L";
                line.Operands = std::format("{},{}", regList, ea);
            }
            break;
        }

        case 6: { // FMOVEM register list to EA
            int regList = cmdWord & 0xFF;
            std::string ea = FormatEA(eaMode, eaReg, 12);
            bool predec = (eaMode == 4);
            line.Mnemonic = "FMOVEM.X";
            line.Operands = std::format("{},{}", FormatFpuRegList(regList, predec), ea);
            break;
        }

        case 7: { // FMOVEM EA to register list
            int regList = cmdWord & 0xFF;
            std::string ea = FormatEA(eaMode, eaReg, 12);
            line.Mnemonic = "FMOVEM.X";
            line.Operands = std::format("{},{}", ea, FormatFpuRegList(regList, false));
            break;
        }

        default:
            line.Mnemonic = "DC.W";
            line.Operands = std::format("${:04X},${:04X}", opcode, cmdWord);
            break;
    }
}

// ============================================================================
// FPU helpers
// ============================================================================

std::string Disassembler::GetFpuOpName(int op)
{
    if (op < 128 && !FpuOpNames[op].empty())
        return FpuOpNames[op];
    return std::format("FPU_OP${:02X}", op);
}

std::string Disassembler::FormatEAForFpu(int eaMode, int eaReg, int format)
{
    int size = FpuFormatSize(format);
    if (eaMode == 7 && eaReg == 4) { // Immediate
        switch (format) {
            case 0: // Long
                return std::format("#${:08X}", ReadLong());
            case 1: { // Single
                uint32_t bits = ReadLong();
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                return std::format("#${:08X} ({:G})", bits, f);
            }
            case 2: { // Extended (12 bytes)
                uint32_t w0 = ReadLong();
                uint32_t w1 = ReadLong();
                uint32_t w2 = ReadLong();
                return std::format("#${:08X}{:08X}{:08X}", w0, w1, w2);
            }
            case 3: { // Packed Decimal (12 bytes)
                uint32_t w0 = ReadLong();
                uint32_t w1 = ReadLong();
                uint32_t w2 = ReadLong();
                return std::format("#${:08X}{:08X}{:08X}", w0, w1, w2);
            }
            case 4: // Word
                return std::format("#${:04X}", ReadWord());
            case 5: { // Double (8 bytes)
                uint32_t hi = ReadLong();
                uint32_t lo = ReadLong();
                int64_t bits = (static_cast<int64_t>(hi) << 32) | lo;
                double d;
                std::memcpy(&d, &bits, sizeof(d));
                return std::format("#${:08X}{:08X} ({:G})", hi, lo, d);
            }
            case 6: // Byte
                return std::format("#${:02X}", ReadWord() & 0xFF);
            default:
                return FormatEA(eaMode, eaReg, size);
        }
    }
    return FormatEA(eaMode, eaReg, size);
}

std::string Disassembler::FormatFpuControlRegs(int select)
{
    std::string result;
    if ((select & 4) != 0) {
        if (!result.empty()) result += '/';
        result += "FPCR";
    }
    if ((select & 2) != 0) {
        if (!result.empty()) result += '/';
        result += "FPSR";
    }
    if ((select & 1) != 0) {
        if (!result.empty()) result += '/';
        result += "FPIAR";
    }
    return result.empty() ? "???" : result;
}

std::string Disassembler::FormatFpuRegList(int mask, bool reverse)
{
    std::string result;
    if (reverse) {
        for (int i = 0; i < 8; i++) {
            if ((mask & (1 << i)) != 0) {
                if (!result.empty()) result += '/';
                result += std::format("FP{}", i);
            }
        }
    } else {
        for (int i = 0; i < 8; i++) {
            if ((mask & (1 << (7 - i))) != 0) {
                if (!result.empty()) result += '/';
                result += std::format("FP{}", i);
            }
        }
    }
    return result.empty() ? "???" : result;
}

// ============================================================================
// CMP2 / CHK2
// ============================================================================

void Disassembler::DisasmCMP2_CHK2(uint16_t opcode, DisasmLine& line, int size)
{
    uint16_t ext = ReadWord();
    int rn = (ext >> 12) & 0xF;
    bool isChk = (ext & 0x0800) != 0;
    std::string rStr = rn < 8 ? std::format("D{}", rn) : std::format("A{}", rn - 8);
    line.Mnemonic = (isChk ? std::string("CHK2") : std::string("CMP2")) + SizeSuffix(size);
    line.Operands = std::format("{},{}", FormatEA((opcode >> 3) & 7, opcode & 7, size), rStr);
}

// ============================================================================
// CAS / CAS2
// ============================================================================

void Disassembler::DisasmCAS(uint16_t opcode, DisasmLine& line, int size)
{
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;

    // CAS2 check
    if (mode == 7 && reg == 4) {
        uint16_t ext1 = ReadWord();
        uint16_t ext2 = ReadWord();
        int dc1 = ext1 & 7, du1 = (ext1 >> 6) & 7;
        int rn1 = (ext1 >> 12) & 0xF;
        int dc2 = ext2 & 7, du2 = (ext2 >> 6) & 7;
        int rn2 = (ext2 >> 12) & 0xF;
        std::string r1 = rn1 < 8 ? std::format("D{}", rn1) : std::format("A{}", rn1 - 8);
        std::string r2 = rn2 < 8 ? std::format("D{}", rn2) : std::format("A{}", rn2 - 8);
        line.Mnemonic = std::string("CAS2") + SizeSuffix(size);
        line.Operands = std::format("D{}:D{},D{}:D{},({}):({})", dc1, dc2, du1, du2, r1, r2);
        return;
    }

    uint16_t ext = ReadWord();
    int dc = ext & 7;
    int du = (ext >> 6) & 7;
    std::string ea = FormatEA(mode, reg, size);
    line.Mnemonic = std::string("CAS") + SizeSuffix(size);
    line.Operands = std::format("D{},D{},{}", dc, du, ea);
}

// ============================================================================
// Bit Field instructions
// ============================================================================

void Disassembler::DisasmBitField(uint16_t opcode, DisasmLine& line)
{
    int bfOp = (opcode >> 8) & 7;
    int mode = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint16_t ext = ReadWord();

    int dnReg = (ext >> 12) & 7;
    bool doReg = (ext & 0x0800) != 0;
    bool dwReg = (ext & 0x0020) != 0;
    std::string offsetStr = doReg ? std::format("D{}", (ext >> 6) & 7) : std::format("{}", (ext >> 6) & 0x1F);
    int widthVal = ext & 0x1F;
    std::string widthStr = dwReg ? std::format("D{}", ext & 7) : (widthVal == 0 ? "32" : std::format("{}", widthVal));

    std::string ea = mode == 0 ? std::format("D{}", reg) : FormatEA(mode, reg, 1);
    std::string bfSpec = std::format("{{{}:{}}}", offsetStr, widthStr);

    line.Mnemonic = BfOpNames[bfOp];
    switch (bfOp) {
        case 0: // BFTST
        case 2: // BFCHG
        case 4: // BFCLR
        case 6: // BFSET
            line.Operands = ea + bfSpec;
            break;
        case 1: // BFEXTU
        case 3: // BFEXTS
        case 5: // BFFFO
            line.Operands = std::format("{}{},D{}", ea, bfSpec, dnReg);
            break;
        case 7: // BFINS
            line.Operands = std::format("D{},{}{}", dnReg, ea, bfSpec);
            break;
    }
}

} // namespace Em68030::Core
