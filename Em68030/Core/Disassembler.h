#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Em68030::Core {

// Forward declaration
class Memory;

// ============================================================================
// DisasmLine
// ============================================================================

struct DisasmLine {
    uint32_t Address = 0;
    std::string RawBytes;
    std::string Mnemonic;
    std::string Operands;
    int Length = 0;

    std::string ToString() const;
};

// ============================================================================
// Disassembler
// ============================================================================

class Disassembler {
public:
    explicit Disassembler(Memory& memory);

    DisasmLine DisassembleOne(uint32_t address);
    std::vector<DisasmLine> Disassemble(uint32_t startAddress, int count);
    std::vector<DisasmLine> DisassembleRange(uint32_t startAddress, uint32_t endAddress, int maxLines = 5000);

private:
    Memory& m_memory;
    uint32_t m_pc = 0;

    uint16_t ReadWord();
    uint32_t ReadLong();

    static const std::string CondCodes[16];
    static const std::string BfOpNames[8];
    static const std::string FpuCondNames[32];
    static std::string FpuOpNames[128];
    static const std::string MmuTTRegNames[4];
    static const std::string MmuTCSRegNames[4];

    static bool s_fpuOpNamesInitialized;
    static void InitFpuOpNames();

    static std::string SizeSuffix(int size);

    std::string FormatEA(int mode, int reg, int size);
    std::string FormatIndexed(const std::string& baseReg);
    static std::string FormatRegisterList(uint16_t mask, bool reverse);

    // FPU format helpers (local to disassembler, mirrors Fpu static methods)
    static int FpuFormatSize(int format);
    static std::string FpuFormatName(int format);

    // Group disassemblers
    void DisasmGroup0(uint16_t opcode, DisasmLine& line);
    void DisasmImmediate(uint16_t opcode, DisasmLine& line, const std::string& name);
    void DisasmMOVE(uint16_t opcode, DisasmLine& line, int size);
    void DisasmGroup4(uint16_t opcode, DisasmLine& line);
    void DisasmGroup5(uint16_t opcode, DisasmLine& line);
    void DisasmGroup6(uint16_t opcode, DisasmLine& line);
    void DisasmMOVEQ(uint16_t opcode, DisasmLine& line);
    void DisasmGroup8(uint16_t opcode, DisasmLine& line);
    void DisasmGroup9(uint16_t opcode, DisasmLine& line);
    void DisasmGroupB(uint16_t opcode, DisasmLine& line);
    void DisasmGroupC(uint16_t opcode, DisasmLine& line);
    void DisasmGroupD(uint16_t opcode, DisasmLine& line);
    void DisasmGroupE(uint16_t opcode, DisasmLine& line);
    void DisasmGroupF(uint16_t opcode, DisasmLine& line);

    void DisasmMMU(uint16_t opcode, DisasmLine& line);
    void DisasmFPU(uint16_t opcode, DisasmLine& line);
    void DisasmFPUGeneral(uint16_t opcode, DisasmLine& line, int eaMode, int eaReg);

    std::string GetFpuOpName(int op);
    std::string FormatEAForFpu(int eaMode, int eaReg, int format);
    static std::string FormatFpuControlRegs(int select);
    static std::string FormatFpuRegList(int mask, bool reverse);

    void DisasmCMP2_CHK2(uint16_t opcode, DisasmLine& line, int size);
    void DisasmCAS(uint16_t opcode, DisasmLine& line, int size);
    void DisasmBitField(uint16_t opcode, DisasmLine& line);

    static int GetSize2(uint16_t opcode);
};

} // namespace Em68030::Core
