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

#include "Alu.h"

namespace Em68030::Core
{

// ============================================================================
//  Add
// ============================================================================

AluResult8 Alu::AddByte(uint8_t a, uint8_t b, uint8_t ccr, bool withExtend)
{
    int x = (withExtend && (ccr & 0x10) != 0) ? 1 : 0;
    int result = a + b + x;
    uint8_t r = static_cast<uint8_t>(result);
    uint8_t newCcr = 0;

    if (result > 0xFF) newCcr |= 0x11; // C and X
    if (r == 0) newCcr |= 0x04;        // Z
    if ((r & 0x80) != 0) newCcr |= 0x08; // N
    if (((a ^ r) & (b ^ r) & 0x80) != 0) newCcr |= 0x02; // V

    return { r, newCcr };
}

AluResult16 Alu::AddWord(uint16_t a, uint16_t b, uint8_t ccr, bool withExtend)
{
    int x = (withExtend && (ccr & 0x10) != 0) ? 1 : 0;
    int result = a + b + x;
    uint16_t r = static_cast<uint16_t>(result);
    uint8_t newCcr = 0;

    if (result > 0xFFFF) newCcr |= 0x11;
    if (r == 0) newCcr |= 0x04;
    if ((r & 0x8000) != 0) newCcr |= 0x08;
    if (((a ^ r) & (b ^ r) & 0x8000) != 0) newCcr |= 0x02;

    return { r, newCcr };
}

AluResult32 Alu::AddLong(uint32_t a, uint32_t b, uint8_t ccr, bool withExtend)
{
    int x = (withExtend && (ccr & 0x10) != 0) ? 1 : 0;
    int64_t result = static_cast<int64_t>(a) + b + x;
    uint32_t r = static_cast<uint32_t>(result);
    uint8_t newCcr = 0;

    if (result > 0xFFFFFFFF) newCcr |= 0x11;
    if (r == 0) newCcr |= 0x04;
    if ((r & 0x80000000u) != 0) newCcr |= 0x08;
    if (((a ^ r) & (b ^ r) & 0x80000000u) != 0) newCcr |= 0x02;

    return { r, newCcr };
}

// ============================================================================
//  Sub
// ============================================================================

AluResult8 Alu::SubByte(uint8_t a, uint8_t b, uint8_t ccr, bool withExtend)
{
    int x = (withExtend && (ccr & 0x10) != 0) ? 1 : 0;
    int result = a - b - x;
    uint8_t r = static_cast<uint8_t>(result);
    uint8_t newCcr = 0;

    if (result < 0) newCcr |= 0x11;
    if (r == 0) newCcr |= 0x04;
    if ((r & 0x80) != 0) newCcr |= 0x08;
    if (((a ^ b) & (a ^ r) & 0x80) != 0) newCcr |= 0x02;

    return { r, newCcr };
}

AluResult16 Alu::SubWord(uint16_t a, uint16_t b, uint8_t ccr, bool withExtend)
{
    int x = (withExtend && (ccr & 0x10) != 0) ? 1 : 0;
    int result = a - b - x;
    uint16_t r = static_cast<uint16_t>(result);
    uint8_t newCcr = 0;

    if (result < 0) newCcr |= 0x11;
    if (r == 0) newCcr |= 0x04;
    if ((r & 0x8000) != 0) newCcr |= 0x08;
    if (((a ^ b) & (a ^ r) & 0x8000) != 0) newCcr |= 0x02;

    return { r, newCcr };
}

AluResult32 Alu::SubLong(uint32_t a, uint32_t b, uint8_t ccr, bool withExtend)
{
    int x = (withExtend && (ccr & 0x10) != 0) ? 1 : 0;
    int64_t result = static_cast<int64_t>(a) - b - x;
    uint32_t r = static_cast<uint32_t>(result);
    uint8_t newCcr = 0;

    if (result < 0) newCcr |= 0x11;
    if (r == 0) newCcr |= 0x04;
    if ((r & 0x80000000u) != 0) newCcr |= 0x08;
    if (((a ^ b) & (a ^ r) & 0x80000000u) != 0) newCcr |= 0x02;

    return { r, newCcr };
}

// ============================================================================
//  NZ flags
// ============================================================================

uint8_t Alu::SetNZFlags(uint8_t value)
{
    uint8_t ccr = 0;
    if (value == 0) ccr |= 0x04;
    if ((value & 0x80) != 0) ccr |= 0x08;
    return ccr;
}

uint8_t Alu::SetNZFlags(uint16_t value)
{
    uint8_t ccr = 0;
    if (value == 0) ccr |= 0x04;
    if ((value & 0x8000) != 0) ccr |= 0x08;
    return ccr;
}

uint8_t Alu::SetNZFlags(uint32_t value)
{
    uint8_t ccr = 0;
    if (value == 0) ccr |= 0x04;
    if ((value & 0x80000000u) != 0) ccr |= 0x08;
    return ccr;
}

// ============================================================================
//  Multiply (16-bit operands -> 32-bit result)
// ============================================================================

AluResult32 Alu::MulSigned(int32_t a, int32_t b)
{
    int64_t result = static_cast<int64_t>(a) * b;
    uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(result));
    uint8_t ccr = SetNZFlags(r);
    if (result > INT32_MAX || result < INT32_MIN) ccr |= 0x02; // V
    return { r, ccr };
}

AluResult32 Alu::MulUnsigned(uint32_t a, uint32_t b)
{
    uint64_t result = static_cast<uint64_t>(a) * b;
    uint32_t r = static_cast<uint32_t>(result);
    uint8_t ccr = SetNZFlags(r);
    if (result > 0xFFFFFFFFu) ccr |= 0x02;
    return { r, ccr };
}

// ============================================================================
//  Divide (16-bit divisor)
// ============================================================================

DivResult Alu::DivSigned(int32_t dividend, int32_t divisor)
{
    if (divisor == 0)
        return { 0, 0, 0, true };

    int64_t q = static_cast<int64_t>(dividend) / divisor;
    int64_t rem = static_cast<int64_t>(dividend) % divisor;

    if (q > INT16_MAX || q < INT16_MIN)
        return { 0, 0, 0x02, true };

    uint32_t quotient = static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(q)));
    uint32_t remainder = static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(rem)));
    uint8_t ccr = SetNZFlags(static_cast<uint16_t>(q));
    return { quotient, remainder, ccr, false };
}

DivResult Alu::DivUnsigned(uint32_t dividend, uint32_t divisor)
{
    if (divisor == 0)
        return { 0, 0, 0, true };

    uint32_t d16 = static_cast<uint16_t>(divisor);
    uint32_t q = dividend / d16;
    uint32_t rem = dividend % d16;

    if (q > 0xFFFF)
        return { 0, 0, 0x02, true };

    uint8_t ccr = SetNZFlags(static_cast<uint16_t>(q));
    return { q & 0xFFFF, rem & 0xFFFF, ccr, false };
}

// ============================================================================
//  Shift Left
// ============================================================================

AluResult8 Alu::ShiftLeft(uint8_t value, int count, uint8_t /*ccr*/)
{
    uint8_t r = value;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>((r >> 7) & 1);
        r <<= 1;
    }
    uint8_t newCcr = SetNZFlags(r);
    if (count > 0 && c != 0) newCcr |= 0x11;
    if (((value ^ r) & 0x80) != 0 && count > 0) newCcr |= 0x02;
    return { r, newCcr };
}

AluResult16 Alu::ShiftLeft(uint16_t value, int count, uint8_t /*ccr*/)
{
    uint16_t r = value;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>((r >> 15) & 1);
        r <<= 1;
    }
    uint8_t newCcr = SetNZFlags(r);
    if (count > 0 && c != 0) newCcr |= 0x11;
    if (((value ^ r) & 0x8000) != 0 && count > 0) newCcr |= 0x02;
    return { r, newCcr };
}

AluResult32 Alu::ShiftLeft(uint32_t value, int count, uint8_t /*ccr*/)
{
    uint32_t r = value;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>((r >> 31) & 1);
        r <<= 1;
    }
    uint8_t newCcr = SetNZFlags(r);
    if (count > 0 && c != 0) newCcr |= 0x11;
    if (((value ^ r) & 0x80000000u) != 0 && count > 0) newCcr |= 0x02;
    return { r, newCcr };
}

// ============================================================================
//  Arithmetic Shift Right
// ============================================================================

AluResult8 Alu::ArithShiftRight(uint8_t value, int count)
{
    int r = static_cast<int8_t>(value);
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r >>= 1;
    }
    uint8_t result = static_cast<uint8_t>(r);
    uint8_t newCcr = SetNZFlags(result);
    if (count > 0 && c != 0) newCcr |= 0x11;
    return { result, newCcr };
}

AluResult16 Alu::ArithShiftRight(uint16_t value, int count)
{
    int r = static_cast<int16_t>(value);
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r >>= 1;
    }
    uint16_t result = static_cast<uint16_t>(static_cast<int16_t>(r));
    uint8_t newCcr = SetNZFlags(result);
    if (count > 0 && c != 0) newCcr |= 0x11;
    return { result, newCcr };
}

AluResult32 Alu::ArithShiftRight(uint32_t value, int count)
{
    int32_t r = static_cast<int32_t>(value);
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r >>= 1;
    }
    uint32_t result = static_cast<uint32_t>(r);
    uint8_t newCcr = SetNZFlags(result);
    if (count > 0 && c != 0) newCcr |= 0x11;
    return { result, newCcr };
}

// ============================================================================
//  Logical Shift Right
// ============================================================================

AluResult8 Alu::LogicalShiftRight(uint8_t value, int count)
{
    uint8_t r = value;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r >>= 1;
    }
    uint8_t newCcr = SetNZFlags(r);
    if (count > 0 && c != 0) newCcr |= 0x11;
    return { r, newCcr };
}

AluResult16 Alu::LogicalShiftRight(uint16_t value, int count)
{
    uint16_t r = value;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r >>= 1;
    }
    uint8_t newCcr = SetNZFlags(r);
    if (count > 0 && c != 0) newCcr |= 0x11;
    return { r, newCcr };
}

AluResult32 Alu::LogicalShiftRight(uint32_t value, int count)
{
    uint32_t r = value;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r >>= 1;
    }
    uint8_t newCcr = SetNZFlags(r);
    if (count > 0 && c != 0) newCcr |= 0x11;
    return { r, newCcr };
}

// ============================================================================
//  Rotate Left / Right
// ============================================================================

AluResult32 Alu::RotateLeft(uint32_t value, int count, int size)
{
    uint32_t mask = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    int bits = size * 8;
    uint32_t r = value & mask;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>((r >> (bits - 1)) & 1);
        r = ((r << 1) | c) & mask;
    }
    uint8_t newCcr;
    switch (size)
    {
        case 1:  newCcr = SetNZFlags(static_cast<uint8_t>(r)); break;
        case 2:  newCcr = SetNZFlags(static_cast<uint16_t>(r)); break;
        default: newCcr = SetNZFlags(r); break;
    }
    if (count > 0 && c != 0) newCcr |= 0x01;
    return { r, newCcr };
}

AluResult32 Alu::RotateRight(uint32_t value, int count, int size)
{
    uint32_t mask = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    int bits = size * 8;
    uint32_t r = value & mask;
    uint8_t c = 0;
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r = (r >> 1) | (static_cast<uint32_t>(c) << (bits - 1));
        r &= mask;
    }
    uint8_t newCcr;
    switch (size)
    {
        case 1:  newCcr = SetNZFlags(static_cast<uint8_t>(r)); break;
        case 2:  newCcr = SetNZFlags(static_cast<uint16_t>(r)); break;
        default: newCcr = SetNZFlags(r); break;
    }
    if (count > 0 && c != 0) newCcr |= 0x01;
    return { r, newCcr };
}

// ============================================================================
//  Rotate Left / Right through eXtend (ROXL / ROXR)
// ============================================================================

AluResult32 Alu::RotateLeftX(uint32_t value, int count, int size, uint8_t ccr)
{
    uint32_t mask = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    int bits = size * 8;
    uint32_t r = value & mask;
    int x = (ccr & 0x10) != 0 ? 1 : 0;
    uint8_t c = static_cast<uint8_t>(x);
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>((r >> (bits - 1)) & 1);
        r = ((r << 1) | static_cast<uint32_t>(x)) & mask;
        x = c;
    }
    uint8_t newCcr;
    switch (size)
    {
        case 1:  newCcr = SetNZFlags(static_cast<uint8_t>(r)); break;
        case 2:  newCcr = SetNZFlags(static_cast<uint16_t>(r)); break;
        default: newCcr = SetNZFlags(r); break;
    }
    if (count > 0)
    {
        if (c != 0) newCcr |= 0x11;
    }
    else
    {
        newCcr |= static_cast<uint8_t>(ccr & 0x10);
        if ((ccr & 0x10) != 0) newCcr |= 0x01;
    }
    return { r, newCcr };
}

AluResult32 Alu::RotateRightX(uint32_t value, int count, int size, uint8_t ccr)
{
    uint32_t mask = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    int bits = size * 8;
    uint32_t r = value & mask;
    int x = (ccr & 0x10) != 0 ? 1 : 0;
    uint8_t c = static_cast<uint8_t>(x);
    for (int i = 0; i < count; i++)
    {
        c = static_cast<uint8_t>(r & 1);
        r = (r >> 1) | (static_cast<uint32_t>(x) << (bits - 1));
        r &= mask;
        x = c;
    }
    uint8_t newCcr;
    switch (size)
    {
        case 1:  newCcr = SetNZFlags(static_cast<uint8_t>(r)); break;
        case 2:  newCcr = SetNZFlags(static_cast<uint16_t>(r)); break;
        default: newCcr = SetNZFlags(r); break;
    }
    if (count > 0)
    {
        if (c != 0) newCcr |= 0x11;
    }
    else
    {
        newCcr |= static_cast<uint8_t>(ccr & 0x10);
        if ((ccr & 0x10) != 0) newCcr |= 0x01;
    }
    return { r, newCcr };
}

// ============================================================================
//  BCD (ABCD / SBCD)
// ============================================================================

AluResult8 Alu::AddBcd(uint8_t src, uint8_t dst, uint8_t ccr)
{
    int x = (ccr & 0x10) != 0 ? 1 : 0;
    int lo = (dst & 0x0F) + (src & 0x0F) + x;
    int carry = 0;
    if (lo > 9) { lo -= 10; carry = 1; }
    int hi = ((dst >> 4) & 0x0F) + ((src >> 4) & 0x0F) + carry;
    carry = 0;
    if (hi > 9) { hi -= 10; carry = 1; }
    uint8_t result = static_cast<uint8_t>(((hi & 0x0F) << 4) | (lo & 0x0F));
    uint8_t newCcr = 0;
    if (carry != 0) newCcr |= 0x11;                      // C and X
    if (result == 0) newCcr |= static_cast<uint8_t>(ccr & 0x04); // Z unchanged if zero
    if ((result & 0x80) != 0) newCcr |= 0x08;            // N (undefined per spec)
    return { result, newCcr };
}

AluResult8 Alu::SubBcd(uint8_t src, uint8_t dst, uint8_t ccr)
{
    int x = (ccr & 0x10) != 0 ? 1 : 0;
    int lo = (dst & 0x0F) - (src & 0x0F) - x;
    int borrow = 0;
    if (lo < 0) { lo += 10; borrow = 1; }
    int hi = ((dst >> 4) & 0x0F) - ((src >> 4) & 0x0F) - borrow;
    borrow = 0;
    if (hi < 0) { hi += 10; borrow = 1; }
    uint8_t result = static_cast<uint8_t>(((hi & 0x0F) << 4) | (lo & 0x0F));
    uint8_t newCcr = 0;
    if (borrow != 0) newCcr |= 0x11;                      // C and X
    if (result == 0) newCcr |= static_cast<uint8_t>(ccr & 0x04); // Z unchanged if zero
    if ((result & 0x80) != 0) newCcr |= 0x08;             // N (undefined per spec)
    return { result, newCcr };
}

// ============================================================================
//  32-bit multiply (68020+)
// ============================================================================

MulLongResult Alu::MulSignedLong(int32_t a, int32_t b)
{
    int64_t result = static_cast<int64_t>(a) * b;
    uint32_t lo = static_cast<uint32_t>(result & 0xFFFFFFFF);
    uint32_t hi = static_cast<uint32_t>(static_cast<uint64_t>(result) >> 32);
    uint8_t flags = SetNZFlags(lo);
    return { lo, hi, flags };
}

MulLongResult Alu::MulUnsignedLong(uint32_t a, uint32_t b)
{
    uint64_t result = static_cast<uint64_t>(a) * b;
    uint32_t lo = static_cast<uint32_t>(result & 0xFFFFFFFF);
    uint32_t hi = static_cast<uint32_t>(result >> 32);
    uint8_t flags = SetNZFlags(lo);
    return { lo, hi, flags };
}

// ============================================================================
//  32-bit divide (68020+)
// ============================================================================

DivResult Alu::DivSignedLong(int64_t dividend, int32_t divisor)
{
    if (divisor == 0)
        return { 0, 0, 0, true };

    int64_t q = dividend / divisor;
    int64_t rem = dividend % divisor;

    if (q > INT32_MAX || q < INT32_MIN)
        return { 0, 0, 0x02, true };

    uint32_t quotient = static_cast<uint32_t>(static_cast<int32_t>(q));
    uint32_t remainder = static_cast<uint32_t>(static_cast<int32_t>(rem));
    uint8_t flags = SetNZFlags(quotient);
    return { quotient, remainder, flags, false };
}

DivResult Alu::DivUnsignedLong(uint64_t dividend, uint32_t divisor)
{
    if (divisor == 0)
        return { 0, 0, 0, true };

    uint64_t q = dividend / divisor;
    uint64_t rem = dividend % divisor;

    if (q > 0xFFFFFFFFu)
        return { 0, 0, 0x02, true };

    uint32_t quotient = static_cast<uint32_t>(q);
    uint32_t remainder = static_cast<uint32_t>(rem);
    uint8_t flags = SetNZFlags(quotient);
    return { quotient, remainder, flags, false };
}

} // namespace Em68030::Core
