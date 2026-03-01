#include "pch.h"

#include "Fpu.h"
#include "MC68030.h"
#include <cmath>
#include <bit>
#include <cstring>
#include <limits>

namespace Em68030::Core
{

// --- FPSR condition code bits (bits 27-24) ---

bool Fpu::getCondN() const  { return (FPSR & 0x08000000u) != 0; }
void Fpu::setCondN(bool value) { FPSR = value ? (FPSR | 0x08000000u) : (FPSR & ~0x08000000u); }

bool Fpu::getCondZ() const  { return (FPSR & 0x04000000u) != 0; }
void Fpu::setCondZ(bool value) { FPSR = value ? (FPSR | 0x04000000u) : (FPSR & ~0x04000000u); }

bool Fpu::getCondI() const  { return (FPSR & 0x02000000u) != 0; }
void Fpu::setCondI(bool value) { FPSR = value ? (FPSR | 0x02000000u) : (FPSR & ~0x02000000u); }

bool Fpu::getCondNAN() const  { return (FPSR & 0x01000000u) != 0; }
void Fpu::setCondNAN(bool value) { FPSR = value ? (FPSR | 0x01000000u) : (FPSR & ~0x01000000u); }

// --- FPCR rounding mode (bits 5-4) ---

RoundingMode Fpu::getRoundMode() const
{
    return static_cast<RoundingMode>((FPCR >> 4) & 3);
}

// --- FPCR rounding precision (bits 7-6) ---

RoundingPrecision Fpu::getRoundPrec() const
{
    return static_cast<RoundingPrecision>((FPCR >> 6) & 3);
}

// --- Reset ---

void Fpu::Reset()
{
    for (int i = 0; i < 8; i++)
        FP[i] = 0.0;
    FPCR = 0;
    FPSR = 0;
    FPIAR = 0;
}

// --- SetConditionCodes ---

void Fpu::SetConditionCodes(double value)
{
    // Clear condition code bits
    FPSR &= 0xF0FFFFFFu;

    if (std::isnan(value))
    {
        setCondNAN(true);
    }
    else if (std::isinf(value) && value > 0)
    {
        setCondI(true);
    }
    else if (std::isinf(value) && value < 0)
    {
        setCondN(true);
        setCondI(true);
    }
    else if (value == 0.0)
    {
        setCondZ(true);
        // Check for negative zero
        if (std::signbit(value))
            setCondN(true);
    }
    else if (value < 0.0)
    {
        setCondN(true);
    }
    // else: all clear (positive non-zero)
}

// --- EvaluateCondition ---

bool Fpu::EvaluateCondition(int condition)
{
    bool n   = getCondN();
    bool z   = getCondZ();
    bool nan = getCondNAN();

    switch (condition & 0x1F)
    {
        case 0x00: return false;                          // F
        case 0x01: return z;                              // EQ
        case 0x02: return !(nan || z || n);               // OGT
        case 0x03: return z || !(nan || n);               // OGE
        case 0x04: return n && !(nan || z);               // OLT
        case 0x05: return z || (n && !nan);               // OLE
        case 0x06: return !(nan || z);                    // OGL
        case 0x07: return !nan;                           // OR
        case 0x08: return nan;                            // UN
        case 0x09: return nan || z;                       // UEQ
        case 0x0A: return nan || !(n || z);               // UGT
        case 0x0B: return nan || z || !n;                 // UGE
        case 0x0C: return nan || (n && !z);               // ULT
        case 0x0D: return nan || z || n;                  // ULE
        case 0x0E: return !z;                             // NE
        case 0x0F: return true;                           // T
        case 0x10: return false;                          // SF
        case 0x11: return z;                              // SEQ
        case 0x12: return !(nan || z || n);               // GT
        case 0x13: return z || !(nan || n);               // GE
        case 0x14: return n && !(nan || z);               // LT
        case 0x15: return z || (n && !nan);               // LE
        case 0x16: return !(nan || z);                    // GL
        case 0x17: return !nan;                           // GLE
        case 0x18: return nan;                            // NGLE
        case 0x19: return nan || z;                       // NGL
        case 0x1A: return nan || !(n || z);               // NLE
        case 0x1B: return nan || z || !n;                 // NLT
        case 0x1C: return nan || (n && !z);               // NGE
        case 0x1D: return nan || z || n;                  // NGT
        case 0x1E: return !z;                             // SNE
        case 0x1F: return true;                           // ST
        default:   return false;
    }
}

// --- ReadFromMemory ---

double Fpu::ReadFromMemory(MC68030& cpu, uint32_t address, int format)
{
    switch (format)
    {
        case 0: // Long Integer (32-bit)
            return static_cast<double>(static_cast<int32_t>(cpu.ReadLong(address)));

        case 1: // Single Precision (32-bit IEEE)
        {
            uint32_t bits = cpu.ReadLong(address);
            float f = std::bit_cast<float>(static_cast<int32_t>(bits));
            return static_cast<double>(f);
        }

        case 2: // Extended Precision (96-bit)
        {
            // 68881 extended format: 12 bytes (96 bits)
            // Bytes 0-1: sign (bit 15) + exponent (bits 14-0)
            // Bytes 2-3: zero padding
            // Bytes 4-11: 64-bit mantissa (with explicit integer bit)
            uint32_t w0 = cpu.ReadLong(address);
            // skip padding at address+2 (it's included in w0's lower 16 bits)
            uint32_t mantHi = cpu.ReadLong(address + 4);
            uint32_t mantLo = cpu.ReadLong(address + 8);

            int sign = static_cast<int>(w0 >> 31) & 1;
            int exponent = static_cast<int>((w0 >> 16) & 0x7FFF);
            uint64_t mantissa = (static_cast<uint64_t>(mantHi) << 32) | mantLo;

            if (exponent == 0 && mantissa == 0)
                return sign == 1 ? -0.0 : 0.0;
            if (exponent == 0x7FFF)
            {
                if (mantissa == 0)
                    return sign == 1 ? -std::numeric_limits<double>::infinity()
                                     :  std::numeric_limits<double>::infinity();
                return std::numeric_limits<double>::quiet_NaN();
            }

            // Convert from 80-bit extended to double
            // Extended: bias = 16383, mantissa has explicit integer bit
            // Double: bias = 1023, mantissa has implicit integer bit
            double val = static_cast<double>(mantissa) / (1ULL << 63) * std::pow(2.0, exponent - 16383);
            return sign == 1 ? -val : val;
        }

        case 3: // Packed Decimal (96-bit BCD)
        {
            // Simplified: read as 12 bytes, extract BCD
            uint32_t w0 = cpu.ReadLong(address);
            [[maybe_unused]] uint32_t w1 = cpu.ReadLong(address + 4);
            [[maybe_unused]] uint32_t w2 = cpu.ReadLong(address + 8);
            int sign = static_cast<int>(w0 >> 31) & 1;
            [[maybe_unused]] int signExp = static_cast<int>(w0 >> 30) & 1;
            // Simplified BCD decode
            return sign == 1 ? -0.0 : 0.0; // Placeholder
        }

        case 4: // Word Integer (16-bit)
            return static_cast<double>(static_cast<int16_t>(cpu.ReadWord(address)));

        case 5: // Double Precision (64-bit IEEE)
        {
            uint32_t hi = cpu.ReadLong(address);
            uint32_t lo = cpu.ReadLong(address + 4);
            int64_t bits = (static_cast<int64_t>(hi) << 32) | lo;
            return std::bit_cast<double>(bits);
        }

        case 6: // Byte Integer (8-bit)
            return static_cast<double>(static_cast<int8_t>(cpu.ReadByte(address)));

        default:
            return 0.0;
    }
}

// --- WriteToMemory ---

void Fpu::WriteToMemory(MC68030& cpu, uint32_t address, int format, double value)
{
    switch (format)
    {
        case 0: // Long Integer
            cpu.WriteLong(address, static_cast<uint32_t>(static_cast<int32_t>(std::round(value))));
            break;

        case 1: // Single Precision
        {
            int32_t bits = std::bit_cast<int32_t>(static_cast<float>(value));
            cpu.WriteLong(address, static_cast<uint32_t>(bits));
            break;
        }

        case 2: // Extended Precision (96-bit)
        {
            int sign = std::signbit(value) ? 1 : 0;
            if (sign == 1) value = -value;

            uint16_t exponent;
            uint64_t mantissa;

            if (std::isnan(value))
            {
                exponent = 0x7FFF;
                mantissa = 0xFFFFFFFFFFFFFFFFULL;
            }
            else if (std::isinf(value))
            {
                exponent = 0x7FFF;
                mantissa = 0x8000000000000000ULL;
            }
            else if (value == 0.0)
            {
                exponent = 0;
                mantissa = 0;
            }
            else
            {
                // Convert double to 80-bit extended
                int64_t dbits = std::bit_cast<int64_t>(value);
                int dexp = static_cast<int>((dbits >> 52) & 0x7FF);
                int64_t dmant = dbits & 0x000FFFFFFFFFFFFFLL;

                exponent = static_cast<uint16_t>(dexp - 1023 + 16383);
                mantissa = 0x8000000000000000ULL | (static_cast<uint64_t>(dmant) << 11);
            }

            uint32_t w0 = (static_cast<uint32_t>(sign) << 31) | (static_cast<uint32_t>(exponent) << 16);
            cpu.WriteLong(address, w0);
            cpu.WriteLong(address + 4, static_cast<uint32_t>(mantissa >> 32));
            cpu.WriteLong(address + 8, static_cast<uint32_t>(mantissa & 0xFFFFFFFF));
            break;
        }

        case 4: // Word Integer
            cpu.WriteWord(address, static_cast<uint16_t>(static_cast<int16_t>(std::round(value))));
            break;

        case 5: // Double Precision
        {
            int64_t bits = std::bit_cast<int64_t>(value);
            cpu.WriteLong(address, static_cast<uint32_t>(bits >> 32));
            cpu.WriteLong(address + 4, static_cast<uint32_t>(bits & 0xFFFFFFFF));
            break;
        }

        case 6: // Byte Integer
            cpu.WriteByte(address, static_cast<uint8_t>(static_cast<int8_t>(std::round(value))));
            break;
    }
}

// --- FormatSize ---

int Fpu::FormatSize(int format)
{
    switch (format)
    {
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

// --- FormatName ---

std::string Fpu::FormatName(int format)
{
    switch (format)
    {
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

} // namespace Em68030::Core
