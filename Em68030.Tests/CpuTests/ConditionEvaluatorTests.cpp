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
#include <gtest/gtest.h>
#include "Helpers/CpuTestFixture.h"
#include "Core/ConditionEvaluator.h"

using namespace Em68030::Core;

namespace Em68030::Tests {

class ConditionEvaluatorTests : public CpuTestFixture {
protected:
    bool Eval(const std::string& cond)
    {
        return EvaluateCondition(cond, Cpu, Memory);
    }
};

// ========================================================================
// Empty / malformed input → always true (unconditional)
// ========================================================================

TEST_F(ConditionEvaluatorTests, EmptyCondition_ReturnsTrue)
{
    EXPECT_TRUE(Eval(""));
}

TEST_F(ConditionEvaluatorTests, WhitespaceOnly_ReturnsTrue)
{
    EXPECT_TRUE(Eval("   "));
}

TEST_F(ConditionEvaluatorTests, UnknownRegister_ReturnsTrue)
{
    EXPECT_TRUE(Eval("XY==0"));
}

TEST_F(ConditionEvaluatorTests, MalformedOperator_ReturnsTrue)
{
    Cpu.D[0] = 100;
    EXPECT_TRUE(Eval("D0~100"));
}

TEST_F(ConditionEvaluatorTests, MalformedMemoryDeref_MissingCloseBracket_ReturnsTrue)
{
    EXPECT_TRUE(Eval("[0x1000.w==0"));
}

// ========================================================================
// Register comparisons: ==, !=, <, >, <=, >=
// ========================================================================

TEST_F(ConditionEvaluatorTests, DataRegister_Equal_True)
{
    Cpu.D[0] = 0x1234;
    EXPECT_TRUE(Eval("D0==0x1234"));
}

TEST_F(ConditionEvaluatorTests, DataRegister_Equal_False)
{
    Cpu.D[0] = 0x1235;
    EXPECT_FALSE(Eval("D0==0x1234"));
}

TEST_F(ConditionEvaluatorTests, DataRegister_NotEqual_True)
{
    Cpu.D[3] = 0xABCD;
    EXPECT_TRUE(Eval("D3!=0x1234"));
}

TEST_F(ConditionEvaluatorTests, DataRegister_NotEqual_False)
{
    Cpu.D[3] = 0x1234;
    EXPECT_FALSE(Eval("D3!=0x1234"));
}

TEST_F(ConditionEvaluatorTests, AddressRegister_LessThan_True)
{
    Cpu.A[7] = 0x0FFFF;
    EXPECT_TRUE(Eval("A7<0x10000"));
}

TEST_F(ConditionEvaluatorTests, AddressRegister_LessThan_False)
{
    Cpu.A[7] = 0x10000;
    EXPECT_FALSE(Eval("A7<0x10000"));
}

TEST_F(ConditionEvaluatorTests, GreaterThan)
{
    Cpu.D[1] = 100;
    EXPECT_TRUE(Eval("D1>50"));
    EXPECT_FALSE(Eval("D1>100"));
}

TEST_F(ConditionEvaluatorTests, LessThanOrEqual)
{
    Cpu.D[2] = 100;
    EXPECT_TRUE(Eval("D2<=100"));
    EXPECT_TRUE(Eval("D2<=200"));
    EXPECT_FALSE(Eval("D2<=99"));
}

TEST_F(ConditionEvaluatorTests, GreaterThanOrEqual)
{
    Cpu.D[4] = 100;
    EXPECT_TRUE(Eval("D4>=100"));
    EXPECT_TRUE(Eval("D4>=50"));
    EXPECT_FALSE(Eval("D4>=101"));
}

// ========================================================================
// Special registers: PC, SR, SP
// ========================================================================

TEST_F(ConditionEvaluatorTests, PC_Register)
{
    Cpu.PC = 0x00002000;
    EXPECT_TRUE(Eval("PC==0x2000"));
    EXPECT_FALSE(Eval("PC==0x3000"));
}

TEST_F(ConditionEvaluatorTests, SR_Register)
{
    Cpu.SR = 0x2700;
    EXPECT_TRUE(Eval("SR==0x2700"));
}

TEST_F(ConditionEvaluatorTests, SP_Register_IsA7)
{
    Cpu.A[7] = 0x00800000;
    EXPECT_TRUE(Eval("SP==0x800000"));
}

TEST_F(ConditionEvaluatorTests, CaseInsensitive_Register)
{
    Cpu.SR = 0x2700;
    Cpu.PC = 0x1000;
    EXPECT_TRUE(Eval("sr==0x2700"));
    EXPECT_TRUE(Eval("pc==0x1000"));
    EXPECT_TRUE(Eval("sp==0x800000"));
}

// ========================================================================
// Bitwise AND test: SR&mask!=0, SR&mask==0
// ========================================================================

TEST_F(ConditionEvaluatorTests, BitwiseAnd_SupervisorBitSet)
{
    Cpu.SR = 0x2700;
    EXPECT_TRUE(Eval("SR&0x2000!=0"));
}

TEST_F(ConditionEvaluatorTests, BitwiseAnd_SupervisorBitClear)
{
    Cpu.SR = 0x0000;
    EXPECT_FALSE(Eval("SR&0x2000!=0"));
}

TEST_F(ConditionEvaluatorTests, BitwiseAnd_EqualZero)
{
    Cpu.SR = 0x0700;
    EXPECT_TRUE(Eval("SR&0x2000==0"));
}

TEST_F(ConditionEvaluatorTests, BitwiseAnd_BareResult)
{
    // "D0&0xFF" without comparison → true if (D0 & 0xFF) != 0
    Cpu.D[0] = 0x00000100;
    EXPECT_FALSE(Eval("D0&0xFF"));   // low byte is 0
    Cpu.D[0] = 0x00000001;
    EXPECT_TRUE(Eval("D0&0xFF"));    // low byte is 1
}

// ========================================================================
// Number format variants: decimal, 0x, $
// ========================================================================

TEST_F(ConditionEvaluatorTests, DecimalNumber)
{
    Cpu.D[0] = 255;
    EXPECT_TRUE(Eval("D0==255"));
}

TEST_F(ConditionEvaluatorTests, HexNumber_0x)
{
    Cpu.D[0] = 0xFF;
    EXPECT_TRUE(Eval("D0==0xFF"));
    EXPECT_TRUE(Eval("D0==0xff"));
    EXPECT_TRUE(Eval("D0==0XFF"));
}

TEST_F(ConditionEvaluatorTests, HexNumber_Dollar)
{
    Cpu.D[0] = 0xABCD;
    EXPECT_TRUE(Eval("D0==$ABCD"));
}

// ========================================================================
// Register-to-register comparison
// ========================================================================

TEST_F(ConditionEvaluatorTests, RegisterVsRegister_Equal)
{
    Cpu.D[0] = 42;
    Cpu.D[1] = 42;
    EXPECT_TRUE(Eval("D0==D1"));
}

TEST_F(ConditionEvaluatorTests, RegisterVsRegister_NotEqual)
{
    Cpu.D[0] = 42;
    Cpu.D[1] = 99;
    EXPECT_FALSE(Eval("D0==D1"));
    EXPECT_TRUE(Eval("D0!=D1"));
}

TEST_F(ConditionEvaluatorTests, RegisterVsRegister_LessThan)
{
    Cpu.D[0] = 10;
    Cpu.A[0] = 20;
    EXPECT_TRUE(Eval("D0<A0"));
}

// ========================================================================
// Bare expression (no operator): true if non-zero
// ========================================================================

TEST_F(ConditionEvaluatorTests, BareRegister_NonZero)
{
    Cpu.D[0] = 1;
    EXPECT_TRUE(Eval("D0"));
}

TEST_F(ConditionEvaluatorTests, BareRegister_Zero)
{
    Cpu.D[0] = 0;
    EXPECT_FALSE(Eval("D0"));
}

TEST_F(ConditionEvaluatorTests, BareNumber_NonZero)
{
    EXPECT_TRUE(Eval("0x1234"));
}

TEST_F(ConditionEvaluatorTests, BareNumber_Zero)
{
    EXPECT_FALSE(Eval("0"));
}

// ========================================================================
// Memory dereference: [addr].b, [addr].w, [addr].l
// ========================================================================

TEST_F(ConditionEvaluatorTests, MemoryDeref_Byte)
{
    Memory.WriteByte(0x1000, 0x42);
    EXPECT_TRUE(Eval("[0x1000].b==0x42"));
    EXPECT_FALSE(Eval("[0x1000].b==0x43"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_Word)
{
    Memory.WriteWord(0x2000, 0xABCD);
    EXPECT_TRUE(Eval("[0x2000].w==0xABCD"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_Long)
{
    Memory.WriteLong(0x3000, 0x12345678);
    EXPECT_TRUE(Eval("[0x3000].l==0x12345678"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_DefaultIsWord)
{
    Memory.WriteWord(0x4000, 0xBEEF);
    EXPECT_TRUE(Eval("[0x4000]==0xBEEF"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_RegisterAddress)
{
    Cpu.A[0] = 0x5000;
    Memory.WriteLong(0x5000, 0xDEADBEEF);
    EXPECT_TRUE(Eval("[A0].l==0xDEADBEEF"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_DollarAddress)
{
    Memory.WriteByte(0x6000, 0xFF);
    EXPECT_TRUE(Eval("[$6000].b==0xFF"));
}

// ========================================================================
// Address arithmetic: [reg+offset], [reg-offset], [num+reg]
// ========================================================================

TEST_F(ConditionEvaluatorTests, MemoryDeref_RegisterPlusDecimal)
{
    Cpu.A[7] = 0x10000;
    Memory.WriteLong(0x1000C, 0xCAFEBABE);
    EXPECT_TRUE(Eval("[A7+12].l==0xCAFEBABE"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_RegisterPlusHex)
{
    Cpu.A[7] = 0x10000;
    Memory.WriteLong(0x1000C, 0xCAFEBABE);
    EXPECT_TRUE(Eval("[A7+0xC].l==0xCAFEBABE"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_RegisterPlusDollarHex)
{
    Cpu.A[0] = 0x2000;
    Memory.WriteWord(0x2010, 0xBEEF);
    EXPECT_TRUE(Eval("[A0+$10].w==0xBEEF"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_RegisterMinusOffset)
{
    Cpu.A[7] = 0x10010;
    Memory.WriteLong(0x10000, 0xDEADBEEF);
    EXPECT_TRUE(Eval("[A7-16].l==0xDEADBEEF"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_NumberPlusRegister)
{
    Cpu.D[0] = 0x100;
    Memory.WriteByte(0x1100, 0x42);
    EXPECT_TRUE(Eval("[0x1000+D0].b==0x42"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_RegisterPlusRegister)
{
    Cpu.A[0] = 0x3000;
    Cpu.D[1] = 0x20;
    Memory.WriteWord(0x3020, 0x1234);
    EXPECT_TRUE(Eval("[A0+D1].w==0x1234"));
}

TEST_F(ConditionEvaluatorTests, MemoryDeref_ArithmeticWithSpaces)
{
    Cpu.A[7] = 0x10000;
    Memory.WriteLong(0x1000C, 0xCAFEBABE);
    EXPECT_TRUE(Eval("[A7 + 12].l==0xCAFEBABE"));
    EXPECT_TRUE(Eval("[ A7+12 ].l==0xCAFEBABE"));
}

// ========================================================================
// Whitespace handling
// ========================================================================

TEST_F(ConditionEvaluatorTests, WhitespaceAroundOperator)
{
    Cpu.D[0] = 100;
    EXPECT_TRUE(Eval("D0 == 100"));
    EXPECT_TRUE(Eval("  D0 == 100  "));
}

TEST_F(ConditionEvaluatorTests, WhitespaceAroundBitwiseAnd)
{
    Cpu.SR = 0x2700;
    EXPECT_TRUE(Eval("SR & 0x2000 != 0"));
}

// ========================================================================
// Logical OR (||) and AND (&&)
// ========================================================================

TEST_F(ConditionEvaluatorTests, LogicalOr_FirstTrue)
{
    Cpu.D[0] = 1;
    EXPECT_TRUE(Eval("D0==1 || D0==3"));
}

TEST_F(ConditionEvaluatorTests, LogicalOr_SecondTrue)
{
    Cpu.D[0] = 3;
    EXPECT_TRUE(Eval("D0==1 || D0==3"));
}

TEST_F(ConditionEvaluatorTests, LogicalOr_NeitherTrue)
{
    Cpu.D[0] = 2;
    EXPECT_FALSE(Eval("D0==1 || D0==3"));
}

TEST_F(ConditionEvaluatorTests, LogicalOr_ThreeClauses)
{
    Cpu.D[0] = 5;
    EXPECT_TRUE(Eval("D0==1 || D0==3 || D0==5"));
    Cpu.D[0] = 4;
    EXPECT_FALSE(Eval("D0==1 || D0==3 || D0==5"));
}

TEST_F(ConditionEvaluatorTests, LogicalAnd_BothTrue)
{
    Cpu.D[0] = 10;
    Cpu.D[1] = 20;
    EXPECT_TRUE(Eval("D0==10 && D1==20"));
}

TEST_F(ConditionEvaluatorTests, LogicalAnd_OneFalse)
{
    Cpu.D[0] = 10;
    Cpu.D[1] = 99;
    EXPECT_FALSE(Eval("D0==10 && D1==20"));
}

TEST_F(ConditionEvaluatorTests, LogicalOr_WithAnd_Precedence)
{
    // "D0==1 || D0==3 && D1==10" means "D0==1 || (D0==3 && D1==10)"
    Cpu.D[0] = 1;
    Cpu.D[1] = 0;
    EXPECT_TRUE(Eval("D0==1 || D0==3 && D1==10")); // first OR clause true

    Cpu.D[0] = 3;
    Cpu.D[1] = 10;
    EXPECT_TRUE(Eval("D0==1 || D0==3 && D1==10")); // second clause true

    Cpu.D[0] = 3;
    Cpu.D[1] = 0;
    EXPECT_FALSE(Eval("D0==1 || D0==3 && D1==10")); // neither satisfied
}

TEST_F(ConditionEvaluatorTests, LogicalOr_WithMemoryDeref)
{
    Cpu.A[7] = 0x10000;
    Memory.WriteLong(0x1000C, 1);
    EXPECT_TRUE(Eval("[A7+12].l==1 || [A7+12].l==3"));
    Memory.WriteLong(0x1000C, 3);
    EXPECT_TRUE(Eval("[A7+12].l==1 || [A7+12].l==3"));
    Memory.WriteLong(0x1000C, 2);
    EXPECT_FALSE(Eval("[A7+12].l==1 || [A7+12].l==3"));
}

TEST_F(ConditionEvaluatorTests, LogicalAnd_WithBitwiseAnd)
{
    // "SR&0x2000!=0 && D0==0" — supervisor mode AND D0 is zero
    Cpu.SR = 0x2700;
    Cpu.D[0] = 0;
    EXPECT_TRUE(Eval("SR&0x2000!=0 && D0==0"));
    Cpu.D[0] = 1;
    EXPECT_FALSE(Eval("SR&0x2000!=0 && D0==0"));
}

// ========================================================================
// IN {set} operator
// ========================================================================

TEST_F(ConditionEvaluatorTests, InSet_Match)
{
    Cpu.D[0] = 3;
    EXPECT_TRUE(Eval("D0 IN {1, 3, 7, 20}"));
}

TEST_F(ConditionEvaluatorTests, InSet_NoMatch)
{
    Cpu.D[0] = 5;
    EXPECT_FALSE(Eval("D0 IN {1, 3, 7, 20}"));
}

TEST_F(ConditionEvaluatorTests, InSet_HexValues)
{
    Cpu.D[0] = 0xFF;
    EXPECT_TRUE(Eval("D0 IN {0xFE, 0xFF, 0x100}"));
    Cpu.D[0] = 0xFD;
    EXPECT_FALSE(Eval("D0 IN {0xFE, 0xFF, 0x100}"));
}

TEST_F(ConditionEvaluatorTests, InSet_MemoryDeref)
{
    Cpu.A[7] = 0x10000;
    Memory.WriteLong(0x1000C, 7);
    EXPECT_TRUE(Eval("[A7+12].l IN {1, 3, 7, 20}"));
    Memory.WriteLong(0x1000C, 99);
    EXPECT_FALSE(Eval("[A7+12].l IN {1, 3, 7, 20}"));
}

TEST_F(ConditionEvaluatorTests, InSet_WithRegisterValues)
{
    Cpu.D[0] = 42;
    Cpu.D[1] = 42;
    EXPECT_TRUE(Eval("D0 IN {10, D1, 99}"));
}

TEST_F(ConditionEvaluatorTests, InSet_CaseInsensitive)
{
    Cpu.D[0] = 5;
    EXPECT_TRUE(Eval("D0 in {3, 5, 7}"));
    EXPECT_TRUE(Eval("D0 In {3, 5, 7}"));
}

TEST_F(ConditionEvaluatorTests, InSet_WithAndOr)
{
    Cpu.D[0] = 3;
    Cpu.D[1] = 100;
    EXPECT_TRUE(Eval("D0 IN {1, 3, 7} && D1==100"));
    Cpu.D[1] = 99;
    EXPECT_FALSE(Eval("D0 IN {1, 3, 7} && D1==100"));
}

// ========================================================================
// Parentheses grouping
// ========================================================================

TEST_F(ConditionEvaluatorTests, Parens_GroupingOrThenAnd)
{
    // Without parens: D0==1 || D0==3 && D1==10 => D0==1 || (D0==3 && D1==10)
    // With parens: (D0==1 || D0==3) && D1==10
    Cpu.D[0] = 1;
    Cpu.D[1] = 0;
    // Without parens: true (first OR clause)
    EXPECT_TRUE(Eval("D0==1 || D0==3 && D1==10"));
    // With parens: (true || false) && false => false
    EXPECT_FALSE(Eval("(D0==1 || D0==3) && D1==10"));
}

TEST_F(ConditionEvaluatorTests, Parens_GroupingAndThenOr)
{
    Cpu.D[0] = 3;
    Cpu.D[1] = 10;
    Cpu.D[2] = 99;
    // (D0==3 && D1==10) || D2==0 => true || false => true
    EXPECT_TRUE(Eval("(D0==3 && D1==10) || D2==0"));
    // D0==3 && (D1==10 || D2==0) => true && true => true
    EXPECT_TRUE(Eval("D0==3 && (D1==10 || D2==0)"));
    // D0==3 && (D1==99 || D2==0) => true && false => false
    EXPECT_FALSE(Eval("D0==3 && (D1==99 || D2==0)"));
}

TEST_F(ConditionEvaluatorTests, Parens_NestedParens)
{
    Cpu.D[0] = 1;
    Cpu.D[1] = 2;
    Cpu.D[2] = 3;
    // ((D0==1 && D1==2) || D2==0) => (true || false) => true
    EXPECT_TRUE(Eval("((D0==1 && D1==2) || D2==0)"));
}

TEST_F(ConditionEvaluatorTests, Parens_InSetInsideCompound)
{
    Cpu.D[0] = 3;
    Cpu.D[1] = 100;
    // (D0 IN {1, 3, 7}) && D1==100
    EXPECT_TRUE(Eval("(D0 IN {1, 3, 7}) && D1==100"));
    Cpu.D[1] = 99;
    EXPECT_FALSE(Eval("(D0 IN {1, 3, 7}) && D1==100"));
}

// ========================================================================
// All data/address registers
// ========================================================================

TEST_F(ConditionEvaluatorTests, AllDataRegisters)
{
    for (int i = 0; i < 8; i++)
    {
        Cpu.D[i] = 100 + i;
        std::string cond = "D" + std::to_string(i) + "==" + std::to_string(100 + i);
        EXPECT_TRUE(Eval(cond)) << "Failed for " << cond;
    }
}

TEST_F(ConditionEvaluatorTests, AllAddressRegisters)
{
    for (int i = 0; i < 8; i++)
    {
        Cpu.A[i] = 0x1000 * (i + 1);
        std::string cond = "A" + std::to_string(i) + "==0x" +
            std::to_string((i + 1) * 0x1000 / 0x1000) + "000";
    }
    // Just verify A0 and A6 explicitly
    Cpu.A[0] = 0x1000;
    Cpu.A[6] = 0x7000;
    EXPECT_TRUE(Eval("A0==0x1000"));
    EXPECT_TRUE(Eval("A6==0x7000"));
}

} // namespace Em68030::Tests
