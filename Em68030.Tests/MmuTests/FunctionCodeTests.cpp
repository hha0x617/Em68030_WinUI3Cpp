#include "pch.h"
#include <gtest/gtest.h>
#include "Core/Memory.h"
#include "Core/Mmu.h"
#include "Core/MC68030.h"

using namespace Em68030::Core;

// FC (Function Code) ベースのルートポインタ選択テスト。

TEST(FunctionCodeTests, FC_UserData_UsesCRP)
{
    Memory mem(16 * 1024 * 1024);
    Mmu mmu(mem);

    uint32_t crpTableBase = 0x00100000;
    uint32_t srpTableBase = 0x00200000;

    mmu.SetTC(0x80C04400); // Enable, PS=12, TIA=4, TIB=4

    // CRP: DT=2, table at crpTableBase
    mmu.CRP = (uint64_t(2) << 32) | crpTableBase;
    // SRP: DT=2, table at srpTableBase
    mmu.SRP = (uint64_t(2) << 32) | srpTableBase;

    // Setup page table in CRP tree: VA 0x10000000 → PA 0x01000000
    uint32_t crpLevelAEntry = crpTableBase + (1 * 4); // Level A index 1
    uint32_t crpLevelBTable = 0x00110000;
    mem.WriteLong(crpLevelAEntry, (crpLevelBTable & 0xFFFFFFF0) | 0x02);
    mem.WriteLong(crpLevelBTable, (0x01000000 & 0xFF000000) | 0x01); // page desc

    mmu.FlushAll();

    // FC=1 (user data) → FC2=0 → uses CRP
    uint32_t pa = mmu.Translate(0x10000000, false, false, 1);
    EXPECT_EQ(0x01000000u, pa);
}

TEST(FunctionCodeTests, FC_SupervisorData_UsesSRP_WhenSREEnabled)
{
    Memory mem(16 * 1024 * 1024);
    Mmu mmu(mem);

    // TC with SRE=1 (bit 25)
    mmu.SetTC(0x82C04400); // Enable + SRE + PS=12 + TIA=4 + TIB=4

    uint32_t crpTableBase = 0x00100000;
    uint32_t srpTableBase = 0x00200000;

    mmu.CRP = (uint64_t(2) << 32) | crpTableBase;
    mmu.SRP = (uint64_t(2) << 32) | srpTableBase;

    // Setup page table ONLY in SRP tree
    uint32_t srpLevelAEntry = srpTableBase + (1 * 4);
    uint32_t srpLevelBTable = 0x00210000;
    mem.WriteLong(srpLevelAEntry, (srpLevelBTable & 0xFFFFFFF0) | 0x02);
    mem.WriteLong(srpLevelBTable, (0x05000000 & 0xFF000000) | 0x01);

    mmu.FlushAll();

    // FC=5 (supervisor data), SRE=1 → uses SRP
    uint32_t pa = mmu.Translate(0x10000000, true, false, 5);
    EXPECT_EQ(0x05000000u, pa);
}

TEST(FunctionCodeTests, FC_SupervisorData_UsesCRP_WhenSREDisabled)
{
    Memory mem(16 * 1024 * 1024);
    Mmu mmu(mem);

    // TC without SRE (bit 25 = 0)
    mmu.SetTC(0x80C04400); // Enable, PS=12, TIA=4, TIB=4, SRE=0

    uint32_t crpTableBase = 0x00100000;

    mmu.CRP = (uint64_t(2) << 32) | crpTableBase;

    // Setup page table in CRP tree
    uint32_t crpLevelAEntry = crpTableBase + (1 * 4);
    uint32_t crpLevelBTable = 0x00110000;
    mem.WriteLong(crpLevelAEntry, (crpLevelBTable & 0xFFFFFFF0) | 0x02);
    mem.WriteLong(crpLevelBTable, (0x01000000 & 0xFF000000) | 0x01);

    mmu.FlushAll();

    // FC=5 (supervisor data), SRE=0 → uses CRP (not SRP)
    uint32_t pa = mmu.Translate(0x10000000, true, false, 5);
    EXPECT_EQ(0x01000000u, pa);
}

TEST(FunctionCodeTests, FC_UserInSupervisorMode_StillUsesCRP)
{
    // MOVES instruction: CPU is in supervisor mode but FC override = 1 (user data)
    Memory mem(16 * 1024 * 1024);
    MC68030 cpu(mem);

    cpu.SR = 0x2700; // Supervisor mode

    // TC with SRE=1
    cpu.GetMmu().SetTC(0x82C04400);

    uint32_t crpTableBase = 0x00100000;
    uint32_t srpTableBase = 0x00200000;

    cpu.GetMmu().CRP = (uint64_t(2) << 32) | crpTableBase;
    cpu.GetMmu().SRP = (uint64_t(2) << 32) | srpTableBase;

    // Setup page table in CRP tree only
    uint32_t crpLevelAEntry = crpTableBase + (1 * 4);
    uint32_t crpLevelBTable = 0x00110000;
    mem.WriteLong(crpLevelAEntry, (crpLevelBTable & 0xFFFFFFF0) | 0x02);
    mem.WriteLong(crpLevelBTable, (0x01000000 & 0xFF000000) | 0x01);

    cpu.GetMmu().FlushAll();

    // FC=1 → FC2=0 → CRP
    uint32_t pa = cpu.GetMmu().Translate(0x10000000, true, false, 1);
    EXPECT_EQ(0x01000000u, pa);
}
