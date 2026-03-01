#include "pch.h"

#include "FpuInstructionDecoder.h"
#include "MC68030.h"
#include "Fpu.h"
#include "AddressingModes.h"

#include <cmath>
#include <bit>
#include <cstring>

namespace Em68030::Core
{

FpuInstructionDecoder::FpuInstructionDecoder(MC68030& cpu, Fpu& fpu)
    : _cpu(cpu), _fpu(fpu)
{
}

// ============================================================================
// Execute - main entry point
// ============================================================================

void FpuInstructionDecoder::Execute(uint16_t opcode)
{
    _fpu.FPIAR = _cpu.PC - 2; // Save instruction address

    int type = (opcode >> 6) & 7;
    int eaMode = (opcode >> 3) & 7;
    int eaReg = opcode & 7;

    switch (type)
    {
        case 0: // General FPU instructions
            ExecuteGeneral(opcode, eaMode, eaReg);
            break;

        case 1: // FDBcc / FScc / FTRAPcc
            ExecuteFSccDBccTRAPcc(opcode, eaMode, eaReg);
            break;

        case 2: // FBcc.W
            ExecuteFBcc(opcode, false);
            break;

        case 3: // FBcc.L
            ExecuteFBcc(opcode, true);
            break;

        case 4: // FSAVE (supervisor only)
            if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
            ExecuteFSave(opcode, eaMode, eaReg);
            break;

        case 5: // FRESTORE (supervisor only)
            if (!_cpu.GetSupervisorMode()) { _cpu.RaiseException(8); return; }
            ExecuteFRestore(opcode, eaMode, eaReg);
            break;

        default:
            _cpu.RaiseException(11); // Line-F
            break;
    }
}

// ============================================================================
// ExecuteGeneral
// ============================================================================

void FpuInstructionDecoder::ExecuteGeneral(uint16_t opcode, int eaMode, int eaReg)
{
    uint16_t cmdWord = _cpu.FetchWord();
    int cmdType = (cmdWord >> 13) & 7;

    switch (cmdType)
    {
        case 0: // Register to register
        {
            int srcReg = (cmdWord >> 10) & 7;
            int dstReg = (cmdWord >> 7) & 7;
            int op = cmdWord & 0x7F;
            double src = _fpu.FP[srcReg];
            ExecuteArithmetic(op, src, dstReg);
            break;
        }

        case 2: // EA to register (with operation)
        {
            int srcFormat = (cmdWord >> 10) & 7;
            int dstReg = (cmdWord >> 7) & 7;
            int op = cmdWord & 0x7F;
            double src = ReadEAFloat(eaMode, eaReg, srcFormat);
            ExecuteArithmetic(op, src, dstReg);
            break;
        }

        case 3: // Register to EA (FMOVE)
        {
            int dstFormat = (cmdWord >> 10) & 7;
            int srcReg = (cmdWord >> 7) & 7;
            double val = _fpu.FP[srcReg];
            _fpu.SetConditionCodes(val);
            WriteEAFloat(eaMode, eaReg, dstFormat, val);
            break;
        }

        case 4: // EA to control register (FMOVE/FMOVEM to FPCR/FPSR/FPIAR)
        {
            int regSelect = (cmdWord >> 10) & 7;
            auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
            if (regSelect == 0) break; // No register selected

            // May move multiple control registers
            if ((regSelect & 4) != 0) // FPCR
            {
                _fpu.FPCR = EffectiveAddress::ReadValue(_cpu, mode, reg, 4);
                if ((regSelect & 3) != 0)
                {
                    // Advance address for next register
                    mode = AdvanceEA(mode, reg, 4, eaMode, eaReg);
                }
            }
            if ((regSelect & 2) != 0) // FPSR
            {
                _fpu.FPSR = EffectiveAddress::ReadValue(_cpu, mode, reg, 4);
                if ((regSelect & 1) != 0)
                {
                    mode = AdvanceEA(mode, reg, 4, eaMode, eaReg);
                }
            }
            if ((regSelect & 1) != 0) // FPIAR
            {
                _fpu.FPIAR = EffectiveAddress::ReadValue(_cpu, mode, reg, 4);
            }
            break;
        }

        case 5: // Control register to EA (FMOVE/FMOVEM from FPCR/FPSR/FPIAR)
        {
            int regSelect = (cmdWord >> 10) & 7;
            auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
            if (regSelect == 0) break;

            if ((regSelect & 4) != 0) // FPCR
            {
                EffectiveAddress::WriteValue(_cpu, mode, reg, 4, _fpu.FPCR);
                if ((regSelect & 3) != 0)
                    mode = AdvanceEA(mode, reg, 4, eaMode, eaReg);
            }
            if ((regSelect & 2) != 0) // FPSR
            {
                EffectiveAddress::WriteValue(_cpu, mode, reg, 4, _fpu.FPSR);
                if ((regSelect & 1) != 0)
                    mode = AdvanceEA(mode, reg, 4, eaMode, eaReg);
            }
            if ((regSelect & 1) != 0) // FPIAR
            {
                EffectiveAddress::WriteValue(_cpu, mode, reg, 4, _fpu.FPIAR);
            }
            break;
        }

        case 6: // FMOVEM memory to FP register list (RESTORE)
                // Per MC68881/68882 UM: dr=0 (bit 13=0) = memory to register
        {
            int listMode = (cmdWord >> 11) & 3;
            auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);

            if (listMode == 0 || listMode == 2) // Static list
            {
                int regList = cmdWord & 0xFF;
                bool postincrement = (eaMode == 3);
                uint32_t addr;
                if (postincrement)
                    addr = _cpu.A[eaReg];
                else
                    addr = EffectiveAddress::ResolveAddress(_cpu, mode, reg, 12);

                // listMode 0 = predecrement order (reversed): bit 0=FP7
                // listMode 2 = postincrement order (natural): bit 0=FP0
                for (int i = 0; i < 8; i++)
                {
                    if ((regList & (1 << i)) != 0)
                    {
                        int fpReg = (listMode == 0) ? 7 - i : i;
                        _fpu.FP[fpReg] = Fpu::ReadFromMemory(_cpu, addr, 2);
                        addr += 12;
                    }
                }
                if (postincrement) _cpu.A[eaReg] = addr;
            }
            else // Dynamic list (listMode 1 or 3)
            {
                int dynReg = (cmdWord >> 4) & 7;
                int regList = static_cast<int>(_cpu.D[dynReg] & 0xFF);
                bool postincrement = (eaMode == 3);
                uint32_t addr;
                if (postincrement)
                    addr = _cpu.A[eaReg];
                else
                    addr = EffectiveAddress::ResolveAddress(_cpu, mode, reg, 12);

                bool reversed = (listMode == 1);
                for (int i = 0; i < 8; i++)
                {
                    if ((regList & (1 << i)) != 0)
                    {
                        int fpReg = reversed ? 7 - i : i;
                        _fpu.FP[fpReg] = Fpu::ReadFromMemory(_cpu, addr, 2);
                        addr += 12;
                    }
                }
                if (postincrement) _cpu.A[eaReg] = addr;
            }
            break;
        }

        case 7: // FMOVEM FP register list to memory (SAVE)
                // Per MC68881/68882 UM: dr=1 (bit 13=1) = register to memory
        {
            int listMode = (cmdWord >> 11) & 3;
            auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);

            if (listMode == 0 || listMode == 2) // Static list
            {
                int regList = cmdWord & 0xFF;
                bool predecrement = (eaMode == 4);

                if (predecrement)
                {
                    // Predecrement: manually handle address without ResolveAddress
                    // to avoid double-decrement. Reversed register order: bit 0=FP7.
                    uint32_t addr = _cpu.A[eaReg];
                    for (int i = 0; i < 8; i++)
                    {
                        if ((regList & (1 << i)) != 0)
                        {
                            addr -= 12;
                            Fpu::WriteToMemory(_cpu, addr, 2, _fpu.FP[7 - i]);
                        }
                    }
                    _cpu.A[eaReg] = addr;
                }
                else
                {
                    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, mode, reg, 12);
                    // listMode 0 = predecrement order (reversed): bit 0=FP7
                    // listMode 2 = postincrement order (natural): bit 0=FP0
                    for (int i = 0; i < 8; i++)
                    {
                        if ((regList & (1 << i)) != 0)
                        {
                            int fpReg = (listMode == 0) ? 7 - i : i;
                            Fpu::WriteToMemory(_cpu, addr, 2, _fpu.FP[fpReg]);
                            addr += 12;
                        }
                    }
                }
            }
            else // Dynamic list (listMode 1 or 3)
            {
                int dynReg = (cmdWord >> 4) & 7;
                int regList = static_cast<int>(_cpu.D[dynReg] & 0xFF);
                bool predecrement = (eaMode == 4);

                if (predecrement)
                {
                    uint32_t addr = _cpu.A[eaReg];
                    bool reversed = (listMode == 1);
                    for (int i = 0; i < 8; i++)
                    {
                        if ((regList & (1 << i)) != 0)
                        {
                            addr -= 12;
                            int fpReg = reversed ? 7 - i : i;
                            Fpu::WriteToMemory(_cpu, addr, 2, _fpu.FP[fpReg]);
                        }
                    }
                    _cpu.A[eaReg] = addr;
                }
                else
                {
                    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, mode, reg, 12);
                    bool reversed = (listMode == 1);
                    for (int i = 0; i < 8; i++)
                    {
                        if ((regList & (1 << i)) != 0)
                        {
                            int fpReg = reversed ? 7 - i : i;
                            Fpu::WriteToMemory(_cpu, addr, 2, _fpu.FP[fpReg]);
                            addr += 12;
                        }
                    }
                }
            }
            break;
        }

        default:
            _cpu.RaiseException(11);
            break;
    }
}

// ============================================================================
// ExecuteArithmetic
// ============================================================================

void FpuInstructionDecoder::ExecuteArithmetic(int op, double src, int dstReg)
{
    double dst = _fpu.FP[dstReg];
    double result;

    switch (op)
    {
        case 0x00: // FMOVE
            result = src;
            break;
        case 0x01: // FINT (round to integer)
            // C# Math.Round with MidpointRounding.ToEven = banker's rounding = std::rint
            result = std::rint(src);
            break;
        case 0x02: // FSINH
            result = std::sinh(src);
            break;
        case 0x03: // FINTRZ (round to integer toward zero)
            result = std::trunc(src);
            break;
        case 0x04: // FSQRT
            result = std::sqrt(src);
            break;
        case 0x06: // FLOGNP1 (ln(x+1))
            result = std::log(src + 1.0);
            break;
        case 0x08: // FETOXM1 (e^x - 1)
            result = std::exp(src) - 1.0;
            break;
        case 0x09: // FTANH
            result = std::tanh(src);
            break;
        case 0x0A: // FATAN
            result = std::atan(src);
            break;
        case 0x0C: // FASIN
            result = std::asin(src);
            break;
        case 0x0D: // FATANH
            result = std::atanh(src);
            break;
        case 0x0E: // FSIN
            result = std::sin(src);
            break;
        case 0x0F: // FTAN
            result = std::tan(src);
            break;
        case 0x10: // FETOX (e^x)
            result = std::exp(src);
            break;
        case 0x11: // FTWOTOX (2^x)
            result = std::pow(2.0, src);
            break;
        case 0x12: // FTENTOX (10^x)
            result = std::pow(10.0, src);
            break;
        case 0x14: // FLOGN (ln)
            result = std::log(src);
            break;
        case 0x15: // FLOG10
            result = std::log10(src);
            break;
        case 0x16: // FLOG2
            result = std::log2(src);
            break;
        case 0x18: // FABS
            result = std::fabs(src);
            break;
        case 0x19: // FCOSH
            result = std::cosh(src);
            break;
        case 0x1A: // FNEG
            result = -src;
            break;
        case 0x1C: // FACOS
            result = std::acos(src);
            break;
        case 0x1D: // FCOS
            result = std::cos(src);
            break;
        case 0x1E: // FGETEXP
        {
            if (src == 0.0 || std::isnan(src) || std::isinf(src))
                result = src;
            else
                result = static_cast<double>(std::ilogb(src));
            break;
        }
        case 0x1F: // FGETMAN
        {
            if (src == 0.0 || std::isnan(src) || std::isinf(src))
                result = src;
            else
            {
                int exp = std::ilogb(src);
                result = src / std::pow(2.0, exp);
            }
            break;
        }
        case 0x20: // FDIV
            result = dst / src;
            break;
        case 0x21: // FMOD (IEEE remainder with quotient sign of dividend)
            result = std::remainder(dst, src);
            // Adjust for FMOD semantics (same sign as dividend)
            if (src != 0)
            {
                result = dst - std::trunc(dst / src) * src;
            }
            break;
        case 0x22: // FADD
            result = dst + src;
            break;
        case 0x23: // FMUL
            result = dst * src;
            break;
        case 0x24: // FSGLDIV (single precision divide)
            result = static_cast<double>(static_cast<float>(dst / src));
            break;
        case 0x25: // FREM (IEEE remainder)
            result = std::remainder(dst, src);
            break;
        case 0x26: // FSCALE
            result = dst * std::pow(2.0, std::trunc(src));
            break;
        case 0x27: // FSGLMUL (single precision multiply)
            result = static_cast<double>(static_cast<float>(dst * src));
            break;
        case 0x28: // FSUB
            result = dst - src;
            break;
        case 0x38: // FCMP
            _fpu.SetConditionCodes(dst - src);
            return; // Don't write result

        case 0x3A: // FTST
            _fpu.SetConditionCodes(src);
            return; // Don't write result

        default:
            // Check for FSINCOS (0x30-0x37)
            if (op >= 0x30 && op <= 0x37)
            {
                int cosReg = op & 7;
                _fpu.FP[cosReg] = std::cos(src);
                result = std::sin(src);
                break;
            }
            // 68040 single/double precision variants
            if ((op & 0x40) != 0)
            {
                int baseOp = op & 0x3F;
                // Recurse with base operation, result will be rounded
                ExecuteArithmetic(baseOp, src, dstReg);
                return;
            }
            _cpu.RaiseException(11);
            return;
    }

    _fpu.FP[dstReg] = result;
    _fpu.SetConditionCodes(result);
}

// ============================================================================
// ExecuteFBcc
// ============================================================================

void FpuInstructionDecoder::ExecuteFBcc(uint16_t opcode, bool longDisp)
{
    int condition = opcode & 0x3F;
    int disp;

    if (longDisp)
    {
        uint32_t d = _cpu.FetchLong();
        disp = static_cast<int32_t>(d);
    }
    else
    {
        disp = static_cast<int16_t>(_cpu.FetchWord());
    }

    if (_fpu.EvaluateCondition(condition))
    {
        _cpu.PC = static_cast<uint32_t>(
            static_cast<int32_t>(_cpu.PC) - (longDisp ? 4 : 2) + disp);
    }
}

// ============================================================================
// ExecuteFSccDBccTRAPcc
// ============================================================================

void FpuInstructionDecoder::ExecuteFSccDBccTRAPcc(uint16_t opcode, int eaMode, int eaReg)
{
    uint16_t cmdWord = _cpu.FetchWord();
    int condition = cmdWord & 0x3F;

    if (eaMode == 1) // FDBcc
    {
        int dispWord = static_cast<int16_t>(_cpu.FetchWord());
        if (!_fpu.EvaluateCondition(condition))
        {
            int cnt = static_cast<int16_t>(_cpu.D[eaReg] & 0xFFFF) - 1;
            _cpu.D[eaReg] = (_cpu.D[eaReg] & 0xFFFF0000u) | static_cast<uint32_t>(cnt & 0xFFFF);
            if (cnt != -1)
            {
                _cpu.PC = static_cast<uint32_t>(
                    static_cast<int32_t>(_cpu.PC) - 2 + dispWord);
            }
        }
    }
    else if (eaMode == 7 && eaReg == 2) // FTRAPcc.W
    {
        _cpu.FetchWord(); // skip extension
        if (_fpu.EvaluateCondition(condition))
            _cpu.RaiseException(7); // TRAP
    }
    else if (eaMode == 7 && eaReg == 3) // FTRAPcc.L
    {
        _cpu.FetchLong(); // skip extension
        if (_fpu.EvaluateCondition(condition))
            _cpu.RaiseException(7);
    }
    else if (eaMode == 7 && eaReg == 4) // FTRAPcc (no operand)
    {
        if (_fpu.EvaluateCondition(condition))
            _cpu.RaiseException(7);
    }
    else // FScc
    {
        auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
        uint32_t val = _fpu.EvaluateCondition(condition) ? 0xFFu : 0u;
        EffectiveAddress::WriteValue(_cpu, mode, reg, 1, val);
    }
}

// ============================================================================
// ExecuteFSave
// ============================================================================

void FpuInstructionDecoder::ExecuteFSave(uint16_t opcode, int eaMode, int eaReg)
{
    // Simplified: write a null frame (idle state)
    // Use CPU memory access (through MMU) not direct physical memory
    auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, mode, reg, 4);
    _cpu.WriteLong(addr, 0x00000000); // Null frame
}

// ============================================================================
// ExecuteFRestore
// ============================================================================

void FpuInstructionDecoder::ExecuteFRestore(uint16_t opcode, int eaMode, int eaReg)
{
    // Simplified: read frame header and skip
    // Use CPU memory access (through MMU) not direct physical memory
    auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
    uint32_t addr = EffectiveAddress::ResolveAddress(_cpu, mode, reg, 4);
    uint32_t header = _cpu.ReadLong(addr);
    // Null frame = reset FPU
    if (header == 0)
    {
        _fpu.Reset();
    }
}

// ============================================================================
// ReadEAFloat
// ============================================================================

double FpuInstructionDecoder::ReadEAFloat(int eaMode, int eaReg, int format)
{
    if (eaMode <= 1) // Data/Address register direct
    {
        if (format == 0) // Long integer from Dn
            return static_cast<double>(static_cast<int32_t>(_cpu.D[eaReg]));
        if (format == 4) // Word integer from Dn
            return static_cast<double>(static_cast<int16_t>(_cpu.D[eaReg] & 0xFFFF));
        if (format == 6) // Byte integer from Dn
            return static_cast<double>(static_cast<int8_t>(_cpu.D[eaReg] & 0xFF));
        if (format == 1) // Single from Dn
        {
            float f = std::bit_cast<float>(static_cast<int32_t>(_cpu.D[eaReg]));
            return static_cast<double>(f);
        }
        // Other formats: treat as long
        return static_cast<double>(static_cast<int32_t>(_cpu.D[eaReg]));
    }

    auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
    int size = Fpu::FormatSize(format);

    // For immediate mode, data follows the instruction
    if (mode == AddressingMode::Immediate)
    {
        uint32_t addr = _cpu.PC;
        double val = Fpu::ReadFromMemory(_cpu, addr, format);
        _cpu.PC += static_cast<uint32_t>((size + 1) & ~1); // Align to word
        return val;
    }

    uint32_t ea = EffectiveAddress::ResolveAddress(_cpu, mode, reg, size);
    double result = Fpu::ReadFromMemory(_cpu, ea, format);
    // Post-increment and pre-decrement already handled in ResolveAddress

    return result;
}

// ============================================================================
// WriteEAFloat
// ============================================================================

void FpuInstructionDecoder::WriteEAFloat(int eaMode, int eaReg, int format, double value)
{
    if (eaMode == 0) // Data register direct
    {
        if (format == 0) // Long integer
            _cpu.D[eaReg] = static_cast<uint32_t>(static_cast<int32_t>(std::round(value)));
        else if (format == 1) // Single
            _cpu.D[eaReg] = static_cast<uint32_t>(std::bit_cast<int32_t>(static_cast<float>(value)));
        else if (format == 4) // Word
        {
            int16_t sv = static_cast<int16_t>(std::round(value));
            _cpu.D[eaReg] = (_cpu.D[eaReg] & 0xFFFF0000u) | static_cast<uint32_t>(static_cast<uint16_t>(sv));
        }
        else if (format == 6) // Byte
        {
            int8_t bv = static_cast<int8_t>(std::round(value));
            _cpu.D[eaReg] = (_cpu.D[eaReg] & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<uint8_t>(bv));
        }
        return;
    }

    auto [mode, reg] = EffectiveAddress::Decode(eaMode, eaReg);
    int size = Fpu::FormatSize(format);
    uint32_t ea = EffectiveAddress::ResolveAddress(_cpu, mode, reg, size);
    Fpu::WriteToMemory(_cpu, ea, format, value);
    // Post-increment and pre-decrement already handled in ResolveAddress
}

// ============================================================================
// AdvanceEA
// ============================================================================

AddressingMode FpuInstructionDecoder::AdvanceEA(
    AddressingMode mode, int& /*reg*/, int /*size*/, int /*origEaMode*/, int /*origEaReg*/)
{
    // For memory modes, we just re-decode with offset
    // This is a simplification; real hardware advances the address
    return mode;
}

} // namespace Em68030::Core
