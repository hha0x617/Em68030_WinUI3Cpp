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

using namespace Em68030::Core;

// StepOut テスト。
// SP ベースの StepOut ロジック（RTS + A7 >= stepOutSP で停止）を検証する。
// -fomit-frame-pointer 環境（LINK/UNLK なし）でも正しく動作することを確認する。

class StepOutTests : public Em68030::Tests::CpuTestFixture {};

/// StepOut ロジックのヘルパー: stepOutSP を記録し、RTS + A7 >= stepOutSP で停止するまで実行。
/// 最大 maxSteps 命令実行して見つからなければ false を返す。
static bool RunStepOut(MC68030& cpu, Memory& mem, uint32_t stepOutSP, int maxSteps = 1000)
{
    for (int i = 0; i < maxSteps; i++)
    {
        if (cpu.A[7] >= stepOutSP)
        {
            uint16_t nextOp = mem.ReadWord(cpu.PC);
            if (nextOp == 0x4E75) // RTS
            {
                cpu.ExecuteStep(); // Execute the RTS
                return true;
            }
        }
        cpu.ExecuteStep();
    }
    return false; // Timeout
}

// ---- Test 1: 単純な BSR + RTS (スタック操作なし) ----
// BSR で呼ばれた関数が即座に RTS する場合
TEST_F(StepOutTests, SimpleSubroutine_RtsImmediately)
{
    // Main: BSR sub → NOP (return here)
    uint32_t mainAddr = 0x1000;
    uint32_t subAddr  = 0x2000;
    uint32_t returnAddr = mainAddr + 4; // BSR.W is 4 bytes

    // BSR.W to subAddr: 0x6100 + displacement
    int16_t disp = static_cast<int16_t>(subAddr - (mainAddr + 2));
    Memory.WriteWord(mainAddr, 0x6100);      // BSR.W
    Memory.WriteWord(mainAddr + 2, static_cast<uint16_t>(disp));
    Memory.WriteWord(returnAddr, 0x4E71);    // NOP (after return)

    // Sub: RTS
    Memory.WriteWord(subAddr, 0x4E75);       // RTS

    Cpu.PC = mainAddr;
    Cpu.ExecuteStep(); // Execute BSR → now inside sub, PC = subAddr

    EXPECT_EQ(Cpu.PC, subAddr);
    uint32_t spAtStepOut = Cpu.A[7];

    // StepOut
    bool found = RunStepOut(Cpu, Memory, spAtStepOut);

    EXPECT_TRUE(found);
    EXPECT_EQ(Cpu.PC, returnAddr);
}

// ---- Test 2: MOVEM で複数レジスタを退避する関数 ----
// sub: MOVEM.L D0-D3,-(A7) → ... → MOVEM.L (A7)+,D0-D3 → RTS
TEST_F(StepOutTests, SubroutineWithMovem_RestoresAndReturns)
{
    uint32_t mainAddr = 0x1000;
    uint32_t subAddr  = 0x2000;
    uint32_t returnAddr = mainAddr + 4;

    // BSR.W to sub
    int16_t disp = static_cast<int16_t>(subAddr - (mainAddr + 2));
    Memory.WriteWord(mainAddr, 0x6100);
    Memory.WriteWord(mainAddr + 2, static_cast<uint16_t>(disp));
    Memory.WriteWord(returnAddr, 0x4E71);    // NOP

    // Sub: MOVEM.L D0-D3,-(A7) → NOP → MOVEM.L (A7)+,D0-D3 → RTS
    uint32_t addr = subAddr;
    Memory.WriteWord(addr, 0x48E7);          // MOVEM.L reg,-(A7)
    Memory.WriteWord(addr + 2, 0xF000);      // D0-D3 (mask: D0=bit15, D1=bit14, D2=bit13, D3=bit12)
    addr += 4;
    Memory.WriteWord(addr, 0x4E71);          // NOP (simulates function body)
    addr += 2;
    Memory.WriteWord(addr, 0x4CDF);          // MOVEM.L (A7)+,reg
    Memory.WriteWord(addr + 2, 0x000F);      // D0-D3 (reversed: D0=bit0, D1=bit1, D2=bit2, D3=bit3)
    addr += 4;
    Memory.WriteWord(addr, 0x4E75);          // RTS

    Cpu.PC = mainAddr;
    Cpu.ExecuteStep(); // Execute BSR

    EXPECT_EQ(Cpu.PC, subAddr);
    uint32_t spAtStepOut = Cpu.A[7];

    bool found = RunStepOut(Cpu, Memory, spAtStepOut);

    EXPECT_TRUE(found);
    EXPECT_EQ(Cpu.PC, returnAddr);
}

// ---- Test 3: LINK/UNLK を使う関数 ----
// sub: LINK A6,#-8 → NOP → UNLK A6 → RTS
TEST_F(StepOutTests, SubroutineWithLinkUnlk_StepsOutCorrectly)
{
    uint32_t mainAddr = 0x1000;
    uint32_t subAddr  = 0x2000;
    uint32_t returnAddr = mainAddr + 4;

    // BSR.W to sub
    int16_t disp = static_cast<int16_t>(subAddr - (mainAddr + 2));
    Memory.WriteWord(mainAddr, 0x6100);
    Memory.WriteWord(mainAddr + 2, static_cast<uint16_t>(disp));
    Memory.WriteWord(returnAddr, 0x4E71);

    // Sub: LINK A6,#-8 → NOP → UNLK A6 → RTS
    uint32_t addr = subAddr;
    Memory.WriteWord(addr, 0x4E56);          // LINK A6
    Memory.WriteWord(addr + 2, 0xFFF8);      // #-8 (allocate 8 bytes)
    addr += 4;
    Memory.WriteWord(addr, 0x4E71);          // NOP
    addr += 2;
    Memory.WriteWord(addr, 0x4E5E);          // UNLK A6
    addr += 2;
    Memory.WriteWord(addr, 0x4E75);          // RTS

    Cpu.PC = mainAddr;
    Cpu.ExecuteStep(); // Execute BSR

    EXPECT_EQ(Cpu.PC, subAddr);
    uint32_t spAtStepOut = Cpu.A[7];

    bool found = RunStepOut(Cpu, Memory, spAtStepOut);

    EXPECT_TRUE(found);
    EXPECT_EQ(Cpu.PC, returnAddr);
}

// ---- Test 4: ネストされたサブルーチン呼び出し ----
// sub1 が sub2 を呼ぶ。sub1 内で StepOut すると sub2 の RTS ではなく sub1 の RTS で停止する。
TEST_F(StepOutTests, NestedSubroutines_StopsAtCorrectLevel)
{
    uint32_t mainAddr = 0x1000;
    uint32_t sub1Addr = 0x2000;
    uint32_t sub2Addr = 0x3000;
    uint32_t returnAddr = mainAddr + 4;

    // Main: BSR.W sub1 → NOP
    int16_t disp1 = static_cast<int16_t>(sub1Addr - (mainAddr + 2));
    Memory.WriteWord(mainAddr, 0x6100);
    Memory.WriteWord(mainAddr + 2, static_cast<uint16_t>(disp1));
    Memory.WriteWord(returnAddr, 0x4E71);

    // Sub1: NOP → BSR.W sub2 → NOP → RTS
    uint32_t addr = sub1Addr;
    Memory.WriteWord(addr, 0x4E71);          // NOP
    addr += 2;
    int16_t disp2 = static_cast<int16_t>(sub2Addr - (addr + 2));
    Memory.WriteWord(addr, 0x6100);          // BSR.W sub2
    Memory.WriteWord(addr + 2, static_cast<uint16_t>(disp2));
    addr += 4;
    Memory.WriteWord(addr, 0x4E71);          // NOP (sub1 continues after sub2 returns)
    addr += 2;
    Memory.WriteWord(addr, 0x4E75);          // RTS (sub1 returns)

    // Sub2: NOP → RTS
    Memory.WriteWord(sub2Addr, 0x4E71);      // NOP
    Memory.WriteWord(sub2Addr + 2, 0x4E75);  // RTS

    Cpu.PC = mainAddr;
    Cpu.ExecuteStep(); // Execute BSR to sub1 → now at sub1

    EXPECT_EQ(Cpu.PC, sub1Addr);
    uint32_t spAtStepOut = Cpu.A[7]; // SP at entry of sub1

    // StepOut should skip sub2's RTS and stop at sub1's RTS
    bool found = RunStepOut(Cpu, Memory, spAtStepOut);

    EXPECT_TRUE(found);
    EXPECT_EQ(Cpu.PC, returnAddr); // Back in main, after BSR sub1
}

// ---- Test 5: ADDQ.L #N,A7 でスタックを手動調整する関数 ----
// -fomit-frame-pointer スタイル: SUB/ADD で SP を直接操作
TEST_F(StepOutTests, SubroutineWithManualSpAdjust_NoFramePointer)
{
    uint32_t mainAddr = 0x1000;
    uint32_t subAddr  = 0x2000;
    uint32_t returnAddr = mainAddr + 4;

    // BSR.W to sub
    int16_t disp = static_cast<int16_t>(subAddr - (mainAddr + 2));
    Memory.WriteWord(mainAddr, 0x6100);
    Memory.WriteWord(mainAddr + 2, static_cast<uint16_t>(disp));
    Memory.WriteWord(returnAddr, 0x4E71);

    // Sub: SUBQ.L #8,A7 → NOP → ADDQ.L #8,A7 → RTS
    uint32_t addr = subAddr;
    Memory.WriteWord(addr, 0x518F);          // SUBQ.L #8,A7
    addr += 2;
    Memory.WriteWord(addr, 0x4E71);          // NOP (function body)
    addr += 2;
    Memory.WriteWord(addr, 0x508F);          // ADDQ.L #8,A7
    addr += 2;
    Memory.WriteWord(addr, 0x4E75);          // RTS

    Cpu.PC = mainAddr;
    Cpu.ExecuteStep(); // Execute BSR

    EXPECT_EQ(Cpu.PC, subAddr);
    uint32_t spAtStepOut = Cpu.A[7];

    bool found = RunStepOut(Cpu, Memory, spAtStepOut);

    EXPECT_TRUE(found);
    EXPECT_EQ(Cpu.PC, returnAddr);
}

// ---- Test 6: 関数途中（SP が下がった状態）で StepOut ----
// SUBQ.L 後、SP < stepOutSP の状態で StepOut を開始
TEST_F(StepOutTests, StepOutFromMiddleOfFunction_AfterStackAllocation)
{
    uint32_t mainAddr = 0x1000;
    uint32_t subAddr  = 0x2000;
    uint32_t returnAddr = mainAddr + 4;

    // BSR.W to sub
    int16_t disp = static_cast<int16_t>(subAddr - (mainAddr + 2));
    Memory.WriteWord(mainAddr, 0x6100);
    Memory.WriteWord(mainAddr + 2, static_cast<uint16_t>(disp));
    Memory.WriteWord(returnAddr, 0x4E71);

    // Sub: SUBQ.L #8,A7 → NOP → NOP → ADDQ.L #8,A7 → RTS
    uint32_t addr = subAddr;
    Memory.WriteWord(addr, 0x518F);          // SUBQ.L #8,A7
    addr += 2;
    Memory.WriteWord(addr, 0x4E71);          // NOP
    addr += 2;
    Memory.WriteWord(addr, 0x4E71);          // NOP
    addr += 2;
    Memory.WriteWord(addr, 0x508F);          // ADDQ.L #8,A7
    addr += 2;
    Memory.WriteWord(addr, 0x4E75);          // RTS

    Cpu.PC = mainAddr;
    Cpu.ExecuteStep(); // Execute BSR
    Cpu.ExecuteStep(); // Execute SUBQ.L #8,A7 (SP decreases)
    Cpu.ExecuteStep(); // Execute first NOP (now in middle of function)

    // SP is now lower than at function entry
    uint32_t spAtStepOut = Cpu.A[7];

    bool found = RunStepOut(Cpu, Memory, spAtStepOut);

    EXPECT_TRUE(found);
    EXPECT_EQ(Cpu.PC, returnAddr);
}
