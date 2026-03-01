#pragma once

#include <cstdint>
#include <string>
#include <memory>

namespace Em68030::Core
{

// Forward declarations
class MC68030;
class FpuInstructionDecoder;

class InstructionDecoder
{
public:
    explicit InstructionDecoder(MC68030& cpu);
    ~InstructionDecoder();

    // Non-copyable, non-movable
    InstructionDecoder(const InstructionDecoder&) = delete;
    InstructionDecoder& operator=(const InstructionDecoder&) = delete;
    InstructionDecoder(InstructionDecoder&&) = delete;
    InstructionDecoder& operator=(InstructionDecoder&&) = delete;

    void ExecuteNext();

private:
    // ====================================================================
    // Group decoders
    // ====================================================================
    void DecodeGroup0(uint16_t opcode);
    void DecodeGroup4(uint16_t opcode);
    void DecodeGroup5(uint16_t opcode);
    void DecodeGroup6(uint16_t opcode);
    void DecodeGroup8(uint16_t opcode);
    void DecodeGroup9(uint16_t opcode);
    void DecodeGroupB(uint16_t opcode);
    void DecodeGroupC(uint16_t opcode);
    void DecodeGroupD(uint16_t opcode);
    void DecodeGroupE(uint16_t opcode);
    void DecodeLineA(uint16_t opcode);
    void DecodeLineF(uint16_t opcode);

    // ====================================================================
    // Sub-decoders (Group 0)
    // ====================================================================
    void DecodeMOVE(uint16_t opcode, int size);
    void DecodeMOVEQ(uint16_t opcode);
    void DecodeMOVEP(uint16_t opcode);
    void DecodeORI(uint16_t opcode);
    void DecodeANDI(uint16_t opcode);
    void DecodeEORI(uint16_t opcode);
    void DecodeSUBI(uint16_t opcode);
    void DecodeADDI(uint16_t opcode);
    void DecodeCMPI(uint16_t opcode);
    void DecodeStaticBit(uint16_t opcode, int op);
    void DecodeCMP2_CHK2(uint16_t opcode);
    void DecodeMOVES(uint16_t opcode);
    void DecodeCAS(uint16_t opcode);
    void DecodeCAS2(uint16_t opcode, int size);

    // ====================================================================
    // Sub-decoders (Group 4)
    // ====================================================================
    void DecodeNEGX(uint16_t opcode);
    void DecodeCLR(uint16_t opcode);
    void DecodeNEG(uint16_t opcode);
    void DecodeNOT(uint16_t opcode);
    void ExtWord(int reg);
    void ExtLong(int reg);
    void ExtByteLong(int reg);
    void DecodeMOVEM(uint16_t opcode);
    void DecodeLINK(uint16_t opcode, bool longDisp);
    void DecodeUNLK(uint16_t opcode);
    void DecodeMOVEC(uint16_t opcode);
    void DecodeLongMul(uint16_t opcode);
    void DecodeLongDiv(uint16_t opcode);

    // ====================================================================
    // Line-F sub-decoders (MMU)
    // ====================================================================
    void DecodeMMUInstruction(uint16_t opcode);
    uint8_t ResolveFunctionCode(uint16_t ext);
    void DecodePMOVE_TT(uint16_t opcode, uint16_t ext);
    void DecodePFLUSH_PLOAD(uint16_t opcode, uint16_t ext);
    void DecodePMOVE_TC_SRP_CRP(uint16_t opcode, uint16_t ext);
    void DecodePMOVE_MMUSR(uint16_t opcode, uint16_t ext);
    void DecodePTEST(uint16_t opcode, uint16_t ext);

    // ====================================================================
    // Group E sub-decoders (Bit Field)
    // ====================================================================
    void DecodeBitField(uint16_t opcode);
    static uint32_t ExtractBitFieldReg(uint32_t data, int offset, int width);
    static uint32_t InsertBitFieldReg(uint32_t data, uint32_t field, int offset, int width);
    void WriteBitFieldMem(uint32_t addr, uint64_t data, int bytes);
    void SetBitFieldFlags(uint32_t field, int width);

    // ====================================================================
    // MOVE size wrappers for opcode table dispatch
    // ====================================================================
    void DecodeMoveB(uint16_t opcode);
    void DecodeMoveL(uint16_t opcode);
    void DecodeMoveW(uint16_t opcode);

    // ====================================================================
    // Specialized fast handlers (Phase 3.2)
    // ====================================================================
    void FastMOVEQ(uint16_t opcode);
    void FastBcc8(uint16_t opcode);
    void FastRTS(uint16_t opcode);
    void FastMoveLDnDm(uint16_t opcode);
    void FastAddLDnDm(uint16_t opcode);
    void FastSubLDnDm(uint16_t opcode);
    void FastCmpLDnDm(uint16_t opcode);

    // ====================================================================
    // Helpers
    // ====================================================================
    int GetSize2(uint16_t opcode);
    uint32_t ReadImmediate(int size);
    void SetLogicFlags(uint32_t value, int size);
    static uint32_t ReadRegValue(uint32_t reg, int size);
    static void WriteRegValue(uint32_t& reg, uint32_t value, int size);
    uint32_t ReadMemValue(uint32_t addr, int size);
    void WriteMemValue(uint32_t addr, uint32_t value, int size);

    // ====================================================================
    // Opcode dispatch table (65536 entries)
    // ====================================================================
    using OpcodeHandler = void (InstructionDecoder::*)(uint16_t);
    static OpcodeHandler s_opcodeTable[65536];
    static bool s_tableInitialized;
    static void InitOpcodeTable();

    // ====================================================================
    // Members
    // ====================================================================
    MC68030& _cpu;
    std::unique_ptr<FpuInstructionDecoder> _fpuDecoder;
};

} // namespace Em68030::Core
