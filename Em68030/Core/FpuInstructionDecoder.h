#pragma once

#include <cstdint>

namespace Em68030::Core
{

// Forward declarations
class MC68030;
class Fpu;
enum class AddressingMode : int;

/// Decodes and executes MC68881/MC68882 FPU coprocessor instructions.
/// Called from InstructionDecoder when a Line-F with cpID=1 is detected.
class FpuInstructionDecoder
{
public:
    FpuInstructionDecoder(MC68030& cpu, Fpu& fpu);

    /// Decode and execute an FPU instruction. opcode is the first word already fetched.
    void Execute(uint16_t opcode);

private:
    void ExecuteGeneral(uint16_t opcode, int eaMode, int eaReg);
    void ExecuteArithmetic(int op, double src, int dstReg);
    void ExecuteFBcc(uint16_t opcode, bool longDisp);
    void ExecuteFSccDBccTRAPcc(uint16_t opcode, int eaMode, int eaReg);
    void ExecuteFSave(uint16_t opcode, int eaMode, int eaReg);
    void ExecuteFRestore(uint16_t opcode, int eaMode, int eaReg);

    /// Read an FP value from the effective address in the given format.
    double ReadEAFloat(int eaMode, int eaReg, int format);

    /// Write an FP value to the effective address in the given format.
    void WriteEAFloat(int eaMode, int eaReg, int format, double value);

    /// Helper for multi-register FMOVEM control register transfers.
    AddressingMode AdvanceEA(AddressingMode mode, int& reg, int size, int origEaMode, int origEaReg);

    /// MC68882 FMOVECR constant ROM lookup.
    static double GetFmovecrConstant(int offset);

    MC68030& _cpu;
    Fpu& _fpu;
};

} // namespace Em68030::Core
