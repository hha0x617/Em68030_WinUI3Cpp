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
#include "Core/BusErrorException.h"

using namespace Em68030::Core;

// レジスタスナップショットのリグレッションテスト。
// ExecuteNextFast の即時スナップショットが、プリデクリメントアドレッシングの
// バスエラー時にレジスタを正しく復元することを検証する。
//
// 背景: 遅延スナップショット最適化では、EnsureRegSnapshot() を最初のメモリ
// アクセス時に実行していた。しかし -(An) アドレッシングは ResolveAddress 内で
// A[n] を変更した後にメモリ書き込みを行うため、スナップショットが変更後の
// レジスタ値をキャプチャし、バスエラー時の復元が不正になっていた。

class RegisterSnapshotTests : public Em68030::Tests::CpuTestFixture {
protected:
    void SetUpBusErrorHandler()
    {
        // Bus error handler at 0x00002000 — just an RTE
        uint32_t handlerAddr = 0x00002000;
        Memory.WriteWord(handlerAddr, 0x4E73); // RTE

        Cpu.VBR = 0x00000000;
        Memory.WriteLong(0x00000008, handlerAddr); // Vector 2 = Bus Error
    }
};

// MOVE.L D0,-(A0) で A[0] がメモリ境界を超える場合のバスエラー復元テスト。
// CpuTestFixture は 16MB RAM (0x00000000-0x00FFFFFF) を提供する。
// A[0] = 0x01000004 → pre-dec → write to 0x01000000 (unmapped) → bus error。
// 復元後の A[0] は元の 0x01000004 でなければならない。
TEST_F(RegisterSnapshotTests, MovePreDec_BusError_RestoresRegisters)
{
    SetUpBusErrorHandler();

    // Place MOVE.L D0, -(A0) at PC
    // Opcode: 0010 000 100 000 000 = 0x2100
    Cpu.PC = 0x00001000;
    Memory.WriteWord(0x00001000, 0x2100);

    Cpu.D[0] = 0xDEADBEEF;
    Cpu.A[0] = 0x01000004; // Just past 16MB — pre-dec by 4 → 0x01000000 (unmapped)

    uint32_t originalA0 = Cpu.A[0];
    uint32_t originalA7 = Cpu.A[7];

    // Use ExecuteNextFast + HandleBusError (the path used by the emulation loop)
    // Bus error is now surfaced via BusErrorPending and handled inside
    // ExecuteNextFast; assert the flag was raised and the CPU recovered.
    Cpu.ExecuteNextFast();
    EXPECT_FALSE(Cpu.BusErrorPending)
        << "BusErrorPending should be cleared by ExecuteNextFast's internal HandleBusError call";

    EXPECT_FALSE(Cpu.Halted) << "CPU should not double-fault";

    // A[0] must be restored to pre-instruction value (not the decremented value)
    EXPECT_EQ(originalA0, Cpu.A[0])
        << "A[0] should be restored to pre-instruction value after bus error";

    // The bus error frame (32 bytes) should be pushed from the correct A[7]
    EXPECT_EQ(originalA7 - 32, Cpu.A[7])
        << "Stack pointer should reflect frame push from original A[7]";
}

// MOVE.W D1,-(A2) — word サイズのプリデクリメントでも同じ復元を検証。
TEST_F(RegisterSnapshotTests, MoveWordPreDec_BusError_RestoresRegisters)
{
    SetUpBusErrorHandler();

    // MOVE.W D1, -(A2)
    // Opcode: 0011 (word) DDD=010(A2) MMM=100(predec) SSS=000 sss=001(D1)
    // 0011 010 100 000 001 = 0x3501
    Cpu.PC = 0x00001000;
    Memory.WriteWord(0x00001000, 0x3501);

    Cpu.D[1] = 0x12345678;
    Cpu.A[2] = 0x01000002; // pre-dec by 2 → 0x01000000 (unmapped)

    uint32_t originalA2 = Cpu.A[2];

    // Bus error is now surfaced via BusErrorPending and handled inside
    // ExecuteNextFast; assert the flag was raised and the CPU recovered.
    Cpu.ExecuteNextFast();
    EXPECT_FALSE(Cpu.BusErrorPending)
        << "BusErrorPending should be cleared by ExecuteNextFast's internal HandleBusError call";

    EXPECT_FALSE(Cpu.Halted);
    EXPECT_EQ(originalA2, Cpu.A[2])
        << "A[2] should be restored to pre-instruction value";
}

// CLR.L -(A3) — CLR もプリデクリメント先がバスエラーなら復元が必要。
TEST_F(RegisterSnapshotTests, ClrPreDec_BusError_RestoresRegisters)
{
    SetUpBusErrorHandler();

    // CLR.L -(A3)
    // 0100 0010 10 100 011 = 0x42A3
    Cpu.PC = 0x00001000;
    Memory.WriteWord(0x00001000, 0x42A3);

    Cpu.A[3] = 0x01000004; // pre-dec by 4 → 0x01000000 (unmapped)

    uint32_t originalA3 = Cpu.A[3];

    // Bus error is now surfaced via BusErrorPending and handled inside
    // ExecuteNextFast; assert the flag was raised and the CPU recovered.
    Cpu.ExecuteNextFast();
    EXPECT_FALSE(Cpu.BusErrorPending)
        << "BusErrorPending should be cleared by ExecuteNextFast's internal HandleBusError call";

    EXPECT_FALSE(Cpu.Halted);
    EXPECT_EQ(originalA3, Cpu.A[3])
        << "A[3] should be restored to pre-instruction value";
}

// オペコードフェッチ時のバスエラーで、前の命令のレジスタ変更が巻き戻されないことを検証。
//
// 背景: 遅延スナップショット最適化では、ExecuteNextFast が _regSnapshotNeeded = true を
// 設定し、グループデコーダーの先頭で EnsureRegSnapshot() を呼ぶ。しかし ExecuteNext() の
// FetchWord() でバスエラーが発生すると、EnsureRegSnapshot は呼ばれず _savedA/_savedD は
// 前の命令のスナップショット値のまま。HandleBusError がこの古い値で復元すると、
// 前の命令のレジスタ変更が不正に巻き戻される。
//
// HandleBusError は _regSnapshotNeeded が true なら復元をスキップする必要がある
// (フェッチ前なのでレジスタは未変更、現在の値が正しい)。
TEST_F(RegisterSnapshotTests, OpcodeFetchBusError_DoesNotRevertDataRegister)
{
    SetUpBusErrorHandler();

    // Place ADDQ.L #1, D0 at the last word in mapped memory (0x00FFFFFE)
    // After fetch, PC = 0x01000000; ADDQ executes, D0 changes.
    // Next ExecuteNextFast: FetchWord(0x01000000) → unmapped → bus error
    Memory.WriteWord(0x00FFFFFE, 0x5280); // ADDQ.L #1, D0

    Cpu.D[0] = 0x00000042;
    Cpu.PC = 0x00FFFFFE;

    // Execute ADDQ — succeeds, D[0] becomes 0x43
    Cpu.ExecuteNextFast();
    ASSERT_EQ(0x00000043u, Cpu.D[0]);

    // Next ExecuteNextFast: opcode fetch at 0x01000000 → bus error
    uint32_t d0AfterAddq = Cpu.D[0]; // 0x43

    // Bus error (unmapped fetch) is now surfaced via BusErrorPending and
    // handled inside ExecuteNextFast; verify the flag clears after return.
    Cpu.ExecuteNextFast();
    EXPECT_FALSE(Cpu.BusErrorPending)
        << "BusErrorPending should be cleared after ExecuteNextFast processes the fault";

    EXPECT_FALSE(Cpu.Halted) << "CPU should not double-fault";
    // D[0] must be 0x43 (post-ADDQ), NOT 0x42 (stale snapshot from ADDQ's EnsureRegSnapshot)
    EXPECT_EQ(d0AfterAddq, Cpu.D[0])
        << "HandleBusError must not revert D registers when bus error occurs during opcode fetch";
}

// アドレスレジスタ版: ADDQ.L #4,A1 → フェッチバスエラー → A[1] が巻き戻されないことを検証
TEST_F(RegisterSnapshotTests, OpcodeFetchBusError_DoesNotRevertAddrRegister)
{
    SetUpBusErrorHandler();

    // ADDQ.L #4, A1: 0101 100 0 10 001 001 = 0x5889
    Memory.WriteWord(0x00FFFFFE, 0x5889);

    Cpu.A[1] = 0x00010000;
    Cpu.PC = 0x00FFFFFE;

    // Execute ADDQ.L #4, A1 — succeeds, A[1] becomes 0x00010004
    Cpu.ExecuteNextFast();
    ASSERT_EQ(0x00010004u, Cpu.A[1]);

    uint32_t a1AfterAddq = Cpu.A[1];

    // Bus error (unmapped fetch) is now surfaced via BusErrorPending and
    // handled inside ExecuteNextFast; verify the flag clears after return.
    Cpu.ExecuteNextFast();
    EXPECT_FALSE(Cpu.BusErrorPending)
        << "BusErrorPending should be cleared after ExecuteNextFast processes the fault";

    EXPECT_FALSE(Cpu.Halted);
    EXPECT_EQ(a1AfterAddq, Cpu.A[1])
        << "HandleBusError must not revert A registers when bus error occurs during opcode fetch";
}

// D レジスタのスナップショットも検証。
// ADDQ.L #1,D0 → MOVE.L D0,-(A0) のシーケンスで、
// D[0] がバスエラー後に ADDQ 前の値に戻ることを確認。
TEST_F(RegisterSnapshotTests, TwoInstructions_BusErrorOnSecond_RestoresAllRegs)
{
    SetUpBusErrorHandler();

    Cpu.PC = 0x00001000;

    // ADDQ.L #1, D0: 0101 000 0 10 000 000 = 0x5280
    Memory.WriteWord(0x00001000, 0x5280);

    // MOVE.L D0, -(A0): 0x2100
    Memory.WriteWord(0x00001002, 0x2100);

    Cpu.D[0] = 0x00000042;
    Cpu.A[0] = 0x01000004;

    // Execute first instruction (ADDQ) — should succeed
    Cpu.ExecuteNextFast();
    EXPECT_EQ(0x00000043u, Cpu.D[0]); // D0 incremented

    // Execute second instruction (MOVE with bus error)
    uint32_t d0BeforeMove = Cpu.D[0]; // 0x43
    uint32_t a0BeforeMove = Cpu.A[0]; // 0x01000004

    // Bus error is now surfaced via BusErrorPending and handled inside
    // ExecuteNextFast; assert the flag was raised and the CPU recovered.
    Cpu.ExecuteNextFast();
    EXPECT_FALSE(Cpu.BusErrorPending)
        << "BusErrorPending should be cleared by ExecuteNextFast's internal HandleBusError call";

    EXPECT_FALSE(Cpu.Halted);

    // Registers should be restored to state BEFORE the MOVE (after ADDQ)
    EXPECT_EQ(d0BeforeMove, Cpu.D[0])
        << "D[0] should retain value from after ADDQ";
    EXPECT_EQ(a0BeforeMove, Cpu.A[0])
        << "A[0] should be restored to pre-MOVE value";
}
