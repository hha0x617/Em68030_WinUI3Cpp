#include "pch.h"
#include <gtest/gtest.h>
#include "Core/Alu.h"

using namespace Em68030::Core;

// CCR bit masks
static constexpr uint8_t CCR_C = 0x01;
static constexpr uint8_t CCR_V = 0x02;
static constexpr uint8_t CCR_Z = 0x04;
static constexpr uint8_t CCR_N = 0x08;
static constexpr uint8_t CCR_X = 0x10;

// ============================================================================
// Add
// ============================================================================

TEST(AluTests, AddByte_Basic)
{
    auto r = Alu::AddByte(0x10, 0x20, 0);
    EXPECT_EQ(0x30, r.result);
    EXPECT_EQ(0, r.ccr & CCR_N); // not negative
    EXPECT_EQ(0, r.ccr & CCR_Z); // not zero
    EXPECT_EQ(0, r.ccr & CCR_V); // no overflow
    EXPECT_EQ(0, r.ccr & CCR_C); // no carry
}

TEST(AluTests, AddWord_Basic)
{
    auto r = Alu::AddWord(0x1000, 0x2000, 0);
    EXPECT_EQ(0x3000, r.result);
    EXPECT_EQ(0, r.ccr & CCR_N);
    EXPECT_EQ(0, r.ccr & CCR_Z);
}

TEST(AluTests, AddLong_Basic)
{
    auto r = Alu::AddLong(0x10000000, 0x20000000, 0);
    EXPECT_EQ(0x30000000u, r.result);
    EXPECT_EQ(0, r.ccr & CCR_N);
    EXPECT_EQ(0, r.ccr & CCR_Z);
}

TEST(AluTests, AddByte_ZeroResult_SetsZFlag)
{
    auto r = Alu::AddByte(0x00, 0x00, 0);
    EXPECT_EQ(0x00, r.result);
    EXPECT_NE(0, r.ccr & CCR_Z);
}

TEST(AluTests, AddByte_Overflow_SetsVFlag)
{
    // 0x7F + 0x01 = 0x80 → positive + positive = negative → overflow
    auto r = Alu::AddByte(0x7F, 0x01, 0);
    EXPECT_EQ(0x80, r.result);
    EXPECT_NE(0, r.ccr & CCR_V);
    EXPECT_NE(0, r.ccr & CCR_N);
}

TEST(AluTests, AddByte_Carry_SetsCFlag)
{
    // 0xFF + 0x01 = 0x00 with carry
    auto r = Alu::AddByte(0xFF, 0x01, 0);
    EXPECT_EQ(0x00, r.result);
    EXPECT_NE(0, r.ccr & CCR_C);
    EXPECT_NE(0, r.ccr & CCR_X);
    EXPECT_NE(0, r.ccr & CCR_Z);
}

// ============================================================================
// Sub
// ============================================================================

TEST(AluTests, SubByte_Basic)
{
    auto r = Alu::SubByte(0x30, 0x10, 0);
    EXPECT_EQ(0x20, r.result);
    EXPECT_EQ(0, r.ccr & CCR_N);
    EXPECT_EQ(0, r.ccr & CCR_Z);
    EXPECT_EQ(0, r.ccr & CCR_C);
}

TEST(AluTests, SubWord_Basic)
{
    auto r = Alu::SubWord(0x3000, 0x1000, 0);
    EXPECT_EQ(0x2000, r.result);
}

TEST(AluTests, SubLong_Basic)
{
    auto r = Alu::SubLong(0x30000000, 0x10000000, 0);
    EXPECT_EQ(0x20000000u, r.result);
}

TEST(AluTests, SubByte_Borrow_SetsCFlag)
{
    // 0x00 - 0x01 = 0xFF with borrow
    auto r = Alu::SubByte(0x00, 0x01, 0);
    EXPECT_EQ(0xFF, r.result);
    EXPECT_NE(0, r.ccr & CCR_C);
    EXPECT_NE(0, r.ccr & CCR_X);
    EXPECT_NE(0, r.ccr & CCR_N);
}

TEST(AluTests, SubByte_Overflow_SetsVFlag)
{
    // 0x80 - 0x01 = 0x7F → negative - positive = positive → overflow
    auto r = Alu::SubByte(0x80, 0x01, 0);
    EXPECT_EQ(0x7F, r.result);
    EXPECT_NE(0, r.ccr & CCR_V);
}

// ============================================================================
// Multiply
// ============================================================================

TEST(AluTests, MulSigned_Basic)
{
    auto r = Alu::MulSigned(10, 20);
    EXPECT_EQ(200u, r.result);
    EXPECT_EQ(0, r.ccr & CCR_N);
    EXPECT_EQ(0, r.ccr & CCR_Z);
}

TEST(AluTests, MulSigned_Negative)
{
    auto r = Alu::MulSigned(-3, 7);
    // -3 * 7 = -21 = 0xFFFFFFEB
    EXPECT_EQ(static_cast<uint32_t>(-21), r.result);
    EXPECT_NE(0, r.ccr & CCR_N);
}

TEST(AluTests, MulUnsigned_Basic)
{
    auto r = Alu::MulUnsigned(100, 200);
    EXPECT_EQ(20000u, r.result);
}

// ============================================================================
// Divide
// ============================================================================

TEST(AluTests, DivSigned_Basic)
{
    auto r = Alu::DivSigned(100, 7);
    EXPECT_EQ(14u, r.quotient);
    EXPECT_EQ(2u, r.remainder);
    EXPECT_FALSE(r.overflow);
}

TEST(AluTests, DivUnsigned_Basic)
{
    auto r = Alu::DivUnsigned(100, 7);
    EXPECT_EQ(14u, r.quotient);
    EXPECT_EQ(2u, r.remainder);
    EXPECT_FALSE(r.overflow);
}

TEST(AluTests, DivSigned_ByZero_SetsOverflow)
{
    auto r = Alu::DivSigned(100, 0);
    EXPECT_TRUE(r.overflow);
}

TEST(AluTests, DivUnsigned_ByZero_SetsOverflow)
{
    auto r = Alu::DivUnsigned(100, 0);
    EXPECT_TRUE(r.overflow);
}

// ============================================================================
// Shift
// ============================================================================

TEST(AluTests, ShiftLeft_Byte)
{
    auto r = Alu::ShiftLeft(static_cast<uint8_t>(0x01), 3, 0);
    EXPECT_EQ(0x08, r.result);
}

TEST(AluTests, ArithShiftRight_PreservesSign)
{
    // 0x80 >> 1 = 0xC0 (sign extended)
    auto r = Alu::ArithShiftRight(static_cast<uint8_t>(0x80), 1);
    EXPECT_EQ(0xC0, r.result);
    EXPECT_NE(0, r.ccr & CCR_N);
}

TEST(AluTests, LogicalShiftRight_ZeroFill)
{
    // 0x80 >> 1 = 0x40 (zero fill)
    auto r = Alu::LogicalShiftRight(static_cast<uint8_t>(0x80), 1);
    EXPECT_EQ(0x40, r.result);
    EXPECT_EQ(0, r.ccr & CCR_N);
}

// ============================================================================
// Rotate
// ============================================================================

TEST(AluTests, RotateLeft_Word)
{
    // Rotate 0x8001 left by 1 (size=2 bytes = word)
    auto r = Alu::RotateLeft(0x8001, 1, 2);
    EXPECT_EQ(0x0003u, r.result);
    EXPECT_NE(0, r.ccr & CCR_C); // Last bit rotated out was 1
}

TEST(AluTests, RotateRight_Byte)
{
    // Rotate 0x01 right by 1 (size=1 byte)
    auto r = Alu::RotateRight(0x01, 1, 1);
    EXPECT_EQ(0x80u, r.result);
    EXPECT_NE(0, r.ccr & CCR_C); // Last bit rotated out was 1
}

// ============================================================================
// BCD
// ============================================================================

TEST(AluTests, AddBcd_Basic)
{
    // BCD: 0x15 + 0x27 = 0x42
    auto r = Alu::AddBcd(0x27, 0x15, 0);
    EXPECT_EQ(0x42, r.result);
}

TEST(AluTests, SubBcd_Basic)
{
    // BCD: 0x42 - 0x15 = 0x27
    auto r = Alu::SubBcd(0x15, 0x42, 0);
    EXPECT_EQ(0x27, r.result);
}

// ============================================================================
// 32-bit extended operations (68020+)
// ============================================================================

TEST(AluTests, MulSignedLong_Basic)
{
    auto r = Alu::MulSignedLong(0x10000, 0x10000);
    // 0x10000 * 0x10000 = 0x1_0000_0000 → resultHi=1, resultLo=0
    EXPECT_EQ(0x00000000u, r.resultLo);
    EXPECT_EQ(0x00000001u, r.resultHi);
}

TEST(AluTests, MulUnsignedLong_Basic)
{
    auto r = Alu::MulUnsignedLong(0xFFFFFFFF, 2);
    // 0xFFFFFFFF * 2 = 0x1_FFFFFFFE → resultHi=1, resultLo=0xFFFFFFFE
    EXPECT_EQ(0xFFFFFFFEu, r.resultLo);
    EXPECT_EQ(0x00000001u, r.resultHi);
}

TEST(AluTests, DivSignedLong_Basic)
{
    auto r = Alu::DivSignedLong(1000000LL, 333);
    EXPECT_EQ(3003u, r.quotient);
    EXPECT_EQ(1u, r.remainder);
    EXPECT_FALSE(r.overflow);
}

TEST(AluTests, DivSignedLong_ByZero_SetsOverflow)
{
    auto r = Alu::DivSignedLong(1000000LL, 0);
    EXPECT_TRUE(r.overflow);
}

TEST(AluTests, DivUnsignedLong_Basic)
{
    auto r = Alu::DivUnsignedLong(1000000ULL, 333);
    EXPECT_EQ(3003u, r.quotient);
    EXPECT_EQ(1u, r.remainder);
    EXPECT_FALSE(r.overflow);
}
