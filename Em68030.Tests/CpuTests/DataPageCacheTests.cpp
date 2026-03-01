#include "pch.h"
#include <gtest/gtest.h>
#include "Core/Memory.h"
#include "Core/MC68030.h"

using namespace Em68030::Core;

// データページキャッシュの FC (Function Code) 分離テスト。
//
// 背景: MC68030 エミュレータのデータアクセス用ページキャッシュは、
// VA → PA のマッピングを 1 エントリだけキャッシュする。
// MOVES 命令は FunctionCodeOverride を設定して、スーパーバイザーモードでも
// ユーザー空間 (FC=1) にアクセスする。SRE=1 の場合、FC に応じて CRP と
// SRP を使い分けるため、同じ VA でも異なる PA にマッピングされる。
// キャッシュが FC を無視すると、スーパーバイザーの変換結果を
// ユーザー空間アクセスに返してしまう。

class DataPageCacheTests : public ::testing::Test {
protected:
    Memory Mem{ 64 * 1024 * 1024 }; // 64MB (needs space for user PA 0x01000000 and super PA 0x02000000)
    MC68030 Cpu{ Mem };

    // Page table base addresses
    static constexpr uint32_t CrpLevelABase = 0x00100000;
    static constexpr uint32_t CrpLevelBBase = 0x00101000;
    static constexpr uint32_t SrpLevelABase = 0x00200000;
    static constexpr uint32_t SrpLevelBBase = 0x00201000;

    // Physical addresses for user and supervisor data
    static constexpr uint32_t UserDataPA   = 0x01000000;
    static constexpr uint32_t SuperDataPA  = 0x02000000;
    static constexpr uint32_t TestVA       = 0x10000000;

    DataPageCacheTests()
    {
        Cpu.SR = 0x2700; // Supervisor mode
        Cpu.A[7] = 0x00800000;
        Cpu.SSP = 0x00800000;

        // TC: Enable=1, SRE=1, PS=12(4KB), IS=0, TIA=4, TIB=4
        // 1000 0010 1100 0000 0100 0100 0000 0000 = 0x82C04400
        Cpu.GetMmu().SetTC(0x82C04400);

        // --- CRP page tables (user space: FC bit 2 = 0) ---
        Cpu.GetMmu().CRP = (uint64_t(2) << 32) | CrpLevelABase;

        // Level A entry 1 → Level B table
        uint32_t crpLevelBTable = CrpLevelBBase;
        Mem.WriteLong(CrpLevelABase + 1 * 4, (crpLevelBTable & 0xFFFFFFF0) | 0x02);

        // Level B entry 0 → page descriptor at UserDataPA
        Mem.WriteLong(crpLevelBTable, (UserDataPA & 0xFF000000) | 0x01);

        // Also map VA 0x00000000 identity (for code/vectors)
        uint32_t crpLevelB0 = CrpLevelBBase + 0x40; // separate Level B for index 0
        Mem.WriteLong(CrpLevelABase + 0 * 4, (crpLevelB0 & 0xFFFFFFF0) | 0x02);
        Mem.WriteLong(crpLevelB0, (0x00000000 & 0xFF000000) | 0x01);

        // --- SRP page tables (supervisor space: FC bit 2 = 1) ---
        Cpu.GetMmu().SRP = (uint64_t(2) << 32) | SrpLevelABase;

        // Level A entry 1 → Level B table
        uint32_t srpLevelBTable = SrpLevelBBase;
        Mem.WriteLong(SrpLevelABase + 1 * 4, (srpLevelBTable & 0xFFFFFFF0) | 0x02);

        // Level B entry 0 → page descriptor at SuperDataPA
        Mem.WriteLong(srpLevelBTable, (SuperDataPA & 0xFF000000) | 0x01);

        // Also map VA 0x00000000 identity
        uint32_t srpLevelB0 = SrpLevelBBase + 0x40;
        Mem.WriteLong(SrpLevelABase + 0 * 4, (srpLevelB0 & 0xFFFFFFF0) | 0x02);
        Mem.WriteLong(srpLevelB0, (0x00000000 & 0xFF000000) | 0x01);

        // Flush ATC to start clean
        Cpu.GetMmu().FlushAll();

        // Write distinguishable data at each physical address
        Mem.WriteByte(UserDataPA,  0xAA);
        Mem.WriteByte(SuperDataPA, 0x55);
        Mem.WriteWord(UserDataPA,  0xAABB);
        Mem.WriteWord(SuperDataPA, 0x5566);
        Mem.WriteLong(UserDataPA,  0xAABBCCDD);
        Mem.WriteLong(SuperDataPA, 0x55667788);
    }
};

// スーパーバイザー読み込み後に FunctionCodeOverride で
// ユーザー空間を読むと、正しい PA にマッピングされることを確認。
TEST_F(DataPageCacheTests, ReadByte_CacheBypassedWhenFCOverridden)
{
    // Supervisor read (FC=5) → should get 0x55 from SuperDataPA
    uint8_t superVal = Cpu.ReadByte(TestVA);
    EXPECT_EQ(0x55u, superVal);

    // Now override FC to user data (FC=1)
    // MOVES decoder calls InvalidateDataCache() before setting FunctionCodeOverride
    Cpu.InvalidateDataCache();
    Cpu.FunctionCodeOverride = 1;
    uint8_t userVal = Cpu.ReadByte(TestVA);
    Cpu.FunctionCodeOverride = -1;

    // Must get 0xAA from UserDataPA, NOT 0x55 from cache
    EXPECT_EQ(0xAAu, userVal)
        << "Data page cache must not return supervisor mapping when FC is overridden to user";
}

TEST_F(DataPageCacheTests, ReadWord_CacheBypassedWhenFCOverridden)
{
    uint16_t superVal = Cpu.ReadWord(TestVA);
    EXPECT_EQ(0x5566u, superVal);

    Cpu.InvalidateDataCache();
    Cpu.FunctionCodeOverride = 1;
    uint16_t userVal = Cpu.ReadWord(TestVA);
    Cpu.FunctionCodeOverride = -1;

    EXPECT_EQ(0xAABBu, userVal)
        << "Data page cache must not return supervisor mapping for word read with FC override";
}

TEST_F(DataPageCacheTests, ReadLong_CacheBypassedWhenFCOverridden)
{
    uint32_t superVal = Cpu.ReadLong(TestVA);
    EXPECT_EQ(0x55667788u, superVal);

    Cpu.InvalidateDataCache();
    Cpu.FunctionCodeOverride = 1;
    uint32_t userVal = Cpu.ReadLong(TestVA);
    Cpu.FunctionCodeOverride = -1;

    EXPECT_EQ(0xAABBCCDDu, userVal)
        << "Data page cache must not return supervisor mapping for long read with FC override";
}

// FC オーバーライドが無い場合は、キャッシュが正常に機能することを確認。
TEST_F(DataPageCacheTests, ReadByte_CacheWorksNormally)
{
    // First read populates cache
    uint8_t val1 = Cpu.ReadByte(TestVA);
    EXPECT_EQ(0x55u, val1);

    // Second read should hit cache and return same value
    uint8_t val2 = Cpu.ReadByte(TestVA);
    EXPECT_EQ(0x55u, val2);
}

// MOVES 命令実行テスト。
// MOVES.B (A0),D0 で SFC=1 (ユーザーデータ) を使い、
// ユーザー空間からデータを読むことを検証。
TEST_F(DataPageCacheTests, MovesInstruction_ReadsFromUserSpace)
{
    // Set up vectors (bus error handler) for safety
    Cpu.VBR = 0x00000000;
    Mem.WriteLong(0x00000008, 0x00002000); // Bus error handler
    Mem.WriteWord(0x00002000, 0x4E73);     // RTE

    // Place MOVES.B (A0), D0 at PC = 0x00001000
    // Opcode: 0000 1110 00 010 000 = 0x0E10
    // Extension: A/D=0, Rn=000(D0), dr=0 (EA→Rn): 0x0000
    Cpu.PC = 0x00001000;
    Mem.WriteWord(0x00001000, 0x0E10);
    Mem.WriteWord(0x00001002, 0x0000);

    Cpu.SFC = 1;    // Source FC = user data
    Cpu.A[0] = TestVA;
    Cpu.D[0] = 0;

    // Pre-populate cache with supervisor mapping
    uint8_t supervisorByte = Cpu.ReadByte(TestVA);
    EXPECT_EQ(0x55u, supervisorByte);

    // Execute MOVES instruction
    Cpu.ExecuteStep();

    // D[0] should contain user data (0xAA), not supervisor data (0x55)
    EXPECT_EQ(0xAAu, Cpu.D[0] & 0xFF)
        << "MOVES should read from user space via SFC, not from supervisor cache";
}
