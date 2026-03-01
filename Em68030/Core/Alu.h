#pragma once

#include <cstdint>
#include <climits>

namespace Em68030::Core
{

struct AluResult8  { uint8_t  result; uint8_t ccr; };
struct AluResult16 { uint16_t result; uint8_t ccr; };
struct AluResult32 { uint32_t result; uint8_t ccr; };
struct DivResult   { uint32_t quotient; uint32_t remainder; uint8_t ccr; bool overflow; };
struct MulLongResult { uint32_t resultLo; uint32_t resultHi; uint8_t ccr; };

struct Alu
{
    // --- Add ---
    static AluResult8  AddByte(uint8_t a, uint8_t b, uint8_t ccr, bool withExtend = false);
    static AluResult16 AddWord(uint16_t a, uint16_t b, uint8_t ccr, bool withExtend = false);
    static AluResult32 AddLong(uint32_t a, uint32_t b, uint8_t ccr, bool withExtend = false);

    // --- Sub ---
    static AluResult8  SubByte(uint8_t a, uint8_t b, uint8_t ccr, bool withExtend = false);
    static AluResult16 SubWord(uint16_t a, uint16_t b, uint8_t ccr, bool withExtend = false);
    static AluResult32 SubLong(uint32_t a, uint32_t b, uint8_t ccr, bool withExtend = false);

    // --- NZ flags ---
    static uint8_t SetNZFlags(uint8_t value);
    static uint8_t SetNZFlags(uint16_t value);
    static uint8_t SetNZFlags(uint32_t value);

    // --- Multiply (16-bit operands, 32-bit result) ---
    static AluResult32 MulSigned(int32_t a, int32_t b);
    static AluResult32 MulUnsigned(uint32_t a, uint32_t b);

    // --- Divide (16-bit divisor) ---
    static DivResult DivSigned(int32_t dividend, int32_t divisor);
    static DivResult DivUnsigned(uint32_t dividend, uint32_t divisor);

    // --- Shift Left ---
    static AluResult8  ShiftLeft(uint8_t value, int count, uint8_t ccr);
    static AluResult16 ShiftLeft(uint16_t value, int count, uint8_t ccr);
    static AluResult32 ShiftLeft(uint32_t value, int count, uint8_t ccr);

    // --- Arithmetic Shift Right ---
    static AluResult8  ArithShiftRight(uint8_t value, int count);
    static AluResult16 ArithShiftRight(uint16_t value, int count);
    static AluResult32 ArithShiftRight(uint32_t value, int count);

    // --- Logical Shift Right ---
    static AluResult8  LogicalShiftRight(uint8_t value, int count);
    static AluResult16 LogicalShiftRight(uint16_t value, int count);
    static AluResult32 LogicalShiftRight(uint32_t value, int count);

    // --- Rotate ---
    static AluResult32 RotateLeft(uint32_t value, int count, int size);
    static AluResult32 RotateRight(uint32_t value, int count, int size);

    // --- Rotate through eXtend ---
    static AluResult32 RotateLeftX(uint32_t value, int count, int size, uint8_t ccr);
    static AluResult32 RotateRightX(uint32_t value, int count, int size, uint8_t ccr);

    // --- BCD ---
    static AluResult8 AddBcd(uint8_t src, uint8_t dst, uint8_t ccr);
    static AluResult8 SubBcd(uint8_t src, uint8_t dst, uint8_t ccr);

    // --- 32-bit multiply (68020+) ---
    static MulLongResult MulSignedLong(int32_t a, int32_t b);
    static MulLongResult MulUnsignedLong(uint32_t a, uint32_t b);

    // --- 32-bit divide (68020+) ---
    static DivResult DivSignedLong(int64_t dividend, int32_t divisor);
    static DivResult DivUnsignedLong(uint64_t dividend, uint32_t divisor);
};

} // namespace Em68030::Core
