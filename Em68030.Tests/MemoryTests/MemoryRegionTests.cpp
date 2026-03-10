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
#include "Core/Memory.h"
#include "Core/BusErrorException.h"

using namespace Em68030::Core;

// byte/word/long の読み書き、未マッピングアドレス、ROM 動作を検証する。

TEST(MemoryRegionTests, ReadWrite_Byte)
{
    Memory mem(1024);

    mem.WriteByte(0x00, 0xAB);
    uint8_t val = mem.ReadByte(0x00);

    EXPECT_EQ(0xAB, val);
}

TEST(MemoryRegionTests, ReadWrite_Word)
{
    Memory mem(1024);

    mem.WriteWord(0x00, 0xBEEF);
    uint16_t val = mem.ReadWord(0x00);

    EXPECT_EQ(0xBEEF, val);
}

TEST(MemoryRegionTests, ReadWrite_Long)
{
    Memory mem(1024);

    mem.WriteLong(0x00, 0xDEADBEEF);
    uint32_t val = mem.ReadLong(0x00);

    EXPECT_EQ(0xDEADBEEFu, val);
}

TEST(MemoryRegionTests, ReadWrite_BigEndian)
{
    Memory mem(1024);

    // Write a long and verify byte order (big-endian)
    mem.WriteLong(0x10, 0x01020304);

    EXPECT_EQ(0x01, mem.ReadByte(0x10));
    EXPECT_EQ(0x02, mem.ReadByte(0x11));
    EXPECT_EQ(0x03, mem.ReadByte(0x12));
    EXPECT_EQ(0x04, mem.ReadByte(0x13));

    // Word reads should also be big-endian
    EXPECT_EQ(0x0102, mem.ReadWord(0x10));
    EXPECT_EQ(0x0304, mem.ReadWord(0x12));
}

TEST(MemoryRegionTests, UnmappedAddress_ThrowsBusError)
{
    Memory mem; // Empty memory, no regions

    EXPECT_THROW(mem.ReadByte(0x00000000), BusErrorException);
    EXPECT_THROW(mem.ReadWord(0x00000000), BusErrorException);
    EXPECT_THROW(mem.ReadLong(0x00000000), BusErrorException);
    EXPECT_THROW(mem.WriteByte(0x00000000, 0), BusErrorException);
    EXPECT_THROW(mem.WriteWord(0x00000000, 0), BusErrorException);
    EXPECT_THROW(mem.WriteLong(0x00000000, 0), BusErrorException);
}

TEST(MemoryRegionTests, Rom_WriteIgnored)
{
    Memory mem;
    mem.AddRegion(0xFF800000, 0x80000, RegionType::Rom); // 512KB ROM

    // Pre-fill ROM via Poke (debugger/loader path)
    mem.PokeByte(0xFF800000, 0xAA);

    // Read should return the poked value
    EXPECT_EQ(0xAA, mem.ReadByte(0xFF800000));

    // Write via normal path should be ignored (ROM)
    mem.WriteByte(0xFF800000, 0x55);

    // Value should still be original
    EXPECT_EQ(0xAA, mem.ReadByte(0xFF800000));
}

TEST(MemoryRegionTests, MultipleRegions_IndependentAccess)
{
    Memory mem;
    mem.AddRegion(0x00000000, 0x100000, RegionType::Ram);  // 1MB RAM at 0
    mem.AddRegion(0xFF800000, 0x80000, RegionType::Rom);    // 512KB ROM at FF800000

    // Write to RAM
    mem.WriteLong(0x00000100, 0x12345678);

    // Write to ROM (via Poke, since normal Write is ignored for ROM)
    mem.PokeLong(0xFF800000, 0xAABBCCDD);

    // Read back independently
    EXPECT_EQ(0x12345678u, mem.ReadLong(0x00000100));
    EXPECT_EQ(0xAABBCCDDu, mem.ReadLong(0xFF800000));

    // Ensure they don't interfere
    mem.WriteLong(0x00000100, 0x00000000);
    EXPECT_EQ(0x00000000u, mem.ReadLong(0x00000100));
    EXPECT_EQ(0xAABBCCDDu, mem.ReadLong(0xFF800000)); // ROM unchanged
}

TEST(MemoryRegionTests, Peek_UnmappedAddress_ReturnsDefault)
{
    Memory mem; // Empty

    // Peek should return default values, not throw
    EXPECT_EQ(0xFF, mem.PeekByte(0x00000000));
    EXPECT_EQ(0xFFFF, mem.PeekWord(0x00000000));
    EXPECT_EQ(0xFFFFFFFFu, mem.PeekLong(0x00000000));
}

TEST(MemoryRegionTests, LoadData_And_GetRange)
{
    Memory mem(1024);

    std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    mem.LoadData(0x00000100, data);

    std::vector<uint8_t> result = mem.GetRange(0x00000100, 5);
    EXPECT_EQ(data, result);
}

TEST(MemoryRegionTests, Poke_CanWriteToRom)
{
    Memory mem;
    mem.AddRegion(0xFF800000, 0x80000, RegionType::Rom);

    // Poke (debugger/loader) should be able to write to ROM
    mem.PokeLong(0xFF800000, 0xDEADCAFE);

    EXPECT_EQ(0xDEADCAFEu, mem.ReadLong(0xFF800000));
}
