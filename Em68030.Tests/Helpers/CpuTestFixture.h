#pragma once

#include <gtest/gtest.h>
#include "Core/Memory.h"
#include "Core/MC68030.h"

namespace Em68030::Tests {

/// CPU テスト用セットアップ。
/// 16MB RAM、Supervisor モード、SSP 設定済みの MC68030 を提供する。
class CpuTestFixture : public ::testing::Test {
protected:
    Em68030::Core::Memory Memory{ 16 * 1024 * 1024 }; // 16MB
    Em68030::Core::MC68030 Cpu{ Memory };

    CpuTestFixture()
    {
        // Write initial SSP and PC at physical address 0 (reset vectors)
        Memory.WriteLong(0x00000000, 0x00800000); // SSP
        Memory.WriteLong(0x00000004, 0x00001000); // PC

        Cpu.SR = 0x2700; // Supervisor mode, interrupt mask 7
        Cpu.A[7] = 0x00800000; // SSP
        Cpu.SSP = 0x00800000;
    }

    /// VBR + ベクタテーブルをセットアップする。
    void SetupVectorTable(uint32_t vbr, uint32_t busErrorHandler)
    {
        Cpu.VBR = vbr;
        // Vector 2 = Bus Error
        Memory.WriteLong(vbr + 2 * 4, busErrorHandler);
    }

    /// 指定アドレスに RTE 命令 (0x4E73) を配置する。
    void PlaceRteAt(uint32_t address)
    {
        Memory.WriteWord(address, 0x4E73);
    }

    /// 指定アドレスに NOP 命令 (0x4E71) を配置する。
    void PlaceNopAt(uint32_t address)
    {
        Memory.WriteWord(address, 0x4E71);
    }
};

} // namespace Em68030::Tests
