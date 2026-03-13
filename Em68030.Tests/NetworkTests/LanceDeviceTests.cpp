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
#include "IO/LanceDevice.h"
#include "IO/VirtualNetworkHandler.h"
#include "Core/Memory.h"

using namespace Em68030::IO;
using namespace Em68030::Core;

// ============================================================================
// Test fixture
// ============================================================================

class LanceDeviceTest : public ::testing::Test {
protected:
    Memory memory;
    LanceDevice lance;
    bool interruptActive = false;

    static constexpr uint32_t BaseAddr      = 0xFFFE1800;
    static constexpr uint32_t InitBlockAddr = 0x00010000;
    static constexpr uint32_t TxRingAddr    = 0x00011000;
    static constexpr uint32_t RxRingAddr    = 0x00012000;

    void SetUp() override {
        memory.AddRegion(0x00000000, 4 * 1024 * 1024, RegionType::Ram);
        memory.AddRegion(0xFFFE0000, 0x10000, RegionType::Ram);

        lance.AttachMemory(&memory);
        lance.InterruptOutput = [this](bool active) { interruptActive = active; };
        memory.RegisterDevice(BaseAddr, 4, &lance);
    }

    void WriteInitBlock() {
        memory.PokeWord(InitBlockAddr, 0x0000); // Mode
        // PADR (MAC): 08:00:3E:21:00:00
        // AM7990 LANCE is little-endian: bytes are swapped within each 16-bit word.
        // 08:00 → word 0x0008, 3E:21 → word 0x213E, 00:00 → word 0x0000
        memory.PokeWord(InitBlockAddr + 0x02, 0x0008);
        memory.PokeWord(InitBlockAddr + 0x04, 0x213E);
        memory.PokeWord(InitBlockAddr + 0x06, 0x0000);
        for (uint32_t i = 0x08; i < 0x10; i += 2)
            memory.PokeWord(InitBlockAddr + i, 0x0000);
        memory.PokeWord(InitBlockAddr + 0x10, static_cast<uint16_t>(RxRingAddr & 0xFFFF));
        memory.PokeWord(InitBlockAddr + 0x12, static_cast<uint16_t>((0 << 13) | ((RxRingAddr >> 16) & 0xFF)));
        memory.PokeWord(InitBlockAddr + 0x14, static_cast<uint16_t>(TxRingAddr & 0xFFFF));
        memory.PokeWord(InitBlockAddr + 0x16, static_cast<uint16_t>((0 << 13) | ((TxRingAddr >> 16) & 0xFF)));
    }

    void SetCSRAddress(uint32_t addr) {
        lance.WriteWord(BaseAddr + 2, 1);
        lance.WriteWord(BaseAddr, static_cast<uint16_t>(addr & 0xFFFF));
        lance.WriteWord(BaseAddr + 2, 2);
        lance.WriteWord(BaseAddr, static_cast<uint16_t>((addr >> 16) & 0xFF));
    }

    void SetupAndStartLance() {
        WriteInitBlock();
        SetCSRAddress(InitBlockAddr);
        lance.WriteWord(BaseAddr + 2, 0);
        lance.WriteWord(BaseAddr, 0x0001); // INIT
        lance.WriteWord(BaseAddr, 0x0002); // STRT
    }
};

// ============================================================================
// CSR Register Access
// ============================================================================

TEST_F(LanceDeviceTest, InitialState_CSR0HasStopBit)
{
    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0004); // STOP
}

TEST_F(LanceDeviceTest, WriteRAP_SelectsCSR)
{
    lance.WriteWord(BaseAddr + 2, 1);
    EXPECT_EQ(1, lance.ReadWord(BaseAddr + 2));
}

TEST_F(LanceDeviceTest, CSR1_WritableInStopState)
{
    lance.WriteWord(BaseAddr + 2, 1);
    lance.WriteWord(BaseAddr, 0x1234);
    EXPECT_EQ(0x1234, lance.ReadWord(BaseAddr));
}

TEST_F(LanceDeviceTest, CSR1_NotWritableWhenRunning)
{
    SetupAndStartLance();

    lance.WriteWord(BaseAddr + 2, 1);
    uint16_t before = lance.ReadWord(BaseAddr);
    lance.WriteWord(BaseAddr, 0xFFFF);
    uint16_t after = lance.ReadWord(BaseAddr);
    EXPECT_EQ(before, after);
}

TEST_F(LanceDeviceTest, CSR0_W1C_ClearsStatusBits)
{
    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);

    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0100); // IDON

    lance.WriteWord(BaseAddr, 0x0100); // W1C IDON
    csr0 = lance.ReadWord(BaseAddr);
    EXPECT_EQ(0, csr0 & 0x0100); // IDON cleared
}

TEST_F(LanceDeviceTest, CSR0_Stop_ResetsChip)
{
    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);

    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0020); // RXON

    lance.WriteWord(BaseAddr, 0x0004); // STOP
    csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0004); // STOP
    EXPECT_EQ(0, csr0 & 0x0020); // RXON cleared
}

// ============================================================================
// Initialization
// ============================================================================

TEST_F(LanceDeviceTest, Init_SetsIDON)
{
    WriteInitBlock();
    SetCSRAddress(InitBlockAddr);

    lance.WriteWord(BaseAddr + 2, 0);
    lance.WriteWord(BaseAddr, 0x0001); // INIT

    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0100); // IDON
}

TEST_F(LanceDeviceTest, Init_ThenStart_SetsRunning)
{
    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);

    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0020); // RXON
    EXPECT_NE(0, csr0 & 0x0010); // TXON
}

TEST_F(LanceDeviceTest, Init_ParsesMacAddress_LittleEndianWordOrder)
{
    // AM7990 init block stores MAC byte-swapped within 16-bit words.
    // MAC 08:00:3E:21:00:00 → words 0x0008, 0x213E, 0x0000
    SetupAndStartLance();

    auto mac = lance.GetMacAddress();
    EXPECT_EQ(0x08, mac[0]);
    EXPECT_EQ(0x00, mac[1]);
    EXPECT_EQ(0x3E, mac[2]);
    EXPECT_EQ(0x21, mac[3]);
    EXPECT_EQ(0x00, mac[4]);
    EXPECT_EQ(0x00, mac[5]);
}

// ============================================================================
// Interrupt
// ============================================================================

TEST_F(LanceDeviceTest, Interrupt_INEA_EnablesInterrupt)
{
    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);

    EXPECT_FALSE(interruptActive);

    lance.WriteWord(BaseAddr, 0x0040); // INEA
    EXPECT_TRUE(interruptActive);
}

TEST_F(LanceDeviceTest, Interrupt_ClearStatus_ClearsInterrupt)
{
    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);

    lance.WriteWord(BaseAddr, 0x0040); // INEA
    EXPECT_TRUE(interruptActive);

    lance.WriteWord(BaseAddr, 0x0140); // IDON + INEA
    EXPECT_FALSE(interruptActive);
}

TEST_F(LanceDeviceTest, INEA_NotClearedByWritingZero)
{
    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);

    // Set INEA
    lance.WriteWord(BaseAddr, 0x0040);
    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0040); // INEA set

    // Write CSR0 without INEA (e.g. TDMD only) — per AM7990 spec, INEA must NOT be cleared
    lance.WriteWord(BaseAddr, 0x0008); // TDMD only
    csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0040); // INEA still set
}

// ============================================================================
// TX Ring
// ============================================================================

TEST_F(LanceDeviceTest, TxRing_ProcessesSinglePacket)
{
    SetupAndStartLance();

    uint32_t bufAddr = 0x00020000;
    for (int i = 0; i < 64; i++)
        memory.PokeByte(bufAddr + static_cast<uint32_t>(i), static_cast<uint8_t>(i & 0xFF));

    // TMD0
    memory.PokeWord(TxRingAddr, static_cast<uint16_t>(bufAddr & 0xFFFF));
    // TMD1: OWN=1, STP=1, ENP=1
    uint8_t flags = 0x80 | 0x02 | 0x01;
    memory.PokeWord(TxRingAddr + 2, static_cast<uint16_t>((flags << 8) | ((bufAddr >> 16) & 0xFF)));
    // TMD2: byte count = -64
    memory.PokeWord(TxRingAddr + 4, static_cast<uint16_t>((-64) & 0x0FFF | 0xF000));
    memory.PokeWord(TxRingAddr + 6, 0);

    // Trigger TDMD
    lance.WriteWord(BaseAddr + 2, 0);
    lance.WriteWord(BaseAddr, 0x0008);
    lance.Tick();

    // OWN cleared
    uint16_t tmd1 = memory.PeekWord(TxRingAddr + 2);
    EXPECT_EQ(0, (tmd1 >> 8) & 0x80);

    // TINT set
    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0200);
}

// ============================================================================
// RX Ring
// ============================================================================

TEST_F(LanceDeviceTest, RxRing_ReceivesPacketFromHandler)
{
    SetupAndStartLance();

    // Prepare RX descriptor
    uint32_t bufAddr = 0x00030000;
    memory.PokeWord(RxRingAddr, static_cast<uint16_t>(bufAddr & 0xFFFF));
    uint8_t rxFlags = 0x80; // OWN=1
    memory.PokeWord(RxRingAddr + 2, static_cast<uint16_t>((rxFlags << 8) | ((bufAddr >> 16) & 0xFF)));
    memory.PokeWord(RxRingAddr + 4, static_cast<uint16_t>((-256) & 0x0FFF | 0xF000));
    memory.PokeWord(RxRingAddr + 6, 0);

    // Build ARP request to generate a reply
    uint8_t guestMac[] = { 0x08, 0x00, 0x3E, 0x21, 0x00, 0x00 };
    uint8_t guestIp[] = { 10, 0, 2, 15 };
    uint8_t gatewayIp[] = { 10, 0, 2, 2 };
    uint8_t arpReq[60] = {};
    std::memset(arpReq, 0xFF, 6);
    std::memcpy(arpReq + 6, guestMac, 6);
    arpReq[12] = 0x08; arpReq[13] = 0x06;
    arpReq[14] = 0x00; arpReq[15] = 0x01;
    arpReq[16] = 0x08; arpReq[17] = 0x00;
    arpReq[18] = 6; arpReq[19] = 4;
    arpReq[20] = 0x00; arpReq[21] = 0x01;
    std::memcpy(arpReq + 22, guestMac, 6);
    std::memcpy(arpReq + 28, guestIp, 4);
    std::memcpy(arpReq + 38, gatewayIp, 4);

    // Write to TX buffer
    uint32_t txBuf = 0x00020000;
    for (int i = 0; i < 60; i++)
        memory.PokeByte(txBuf + static_cast<uint32_t>(i), arpReq[i]);
    memory.PokeWord(TxRingAddr, static_cast<uint16_t>(txBuf & 0xFFFF));
    memory.PokeWord(TxRingAddr + 2, static_cast<uint16_t>(((0x80 | 0x02 | 0x01) << 8) | ((txBuf >> 16) & 0xFF)));
    memory.PokeWord(TxRingAddr + 4, static_cast<uint16_t>((-60) & 0x0FFF | 0xF000));

    // Process TX
    lance.WriteWord(BaseAddr + 2, 0);
    lance.WriteWord(BaseAddr, 0x0008);
    lance.Tick();

    // RX should pick up ARP reply
    lance.Tick();

    // Verify RX descriptor
    uint16_t rmd1 = memory.PeekWord(RxRingAddr + 2);
    EXPECT_EQ(0, (rmd1 >> 8) & 0x80);    // OWN cleared
    EXPECT_NE(0, (rmd1 >> 8) & 0x03);    // STP+ENP

    // RINT set
    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0400);

    // Verify ARP reply ethertype
    EXPECT_EQ(0x08, memory.PeekByte(bufAddr + 12));
    EXPECT_EQ(0x06, memory.PeekByte(bufAddr + 13));

    // Verify ARP reply destination MAC matches guest MAC (08:00:3E:21:00:00)
    EXPECT_EQ(0x08, memory.PeekByte(bufAddr + 0));
    EXPECT_EQ(0x00, memory.PeekByte(bufAddr + 1));
    EXPECT_EQ(0x3E, memory.PeekByte(bufAddr + 2));
    EXPECT_EQ(0x21, memory.PeekByte(bufAddr + 3));
    EXPECT_EQ(0x00, memory.PeekByte(bufAddr + 4));
    EXPECT_EQ(0x00, memory.PeekByte(bufAddr + 5));
}

// ============================================================================
// SetNetworkHandler
// ============================================================================

TEST_F(LanceDeviceTest, SetNetworkHandler_SwapsHandler)
{
    lance.SetNetworkHandler(std::make_unique<VirtualNetworkHandler>());

    SetupAndStartLance();
    lance.WriteWord(BaseAddr + 2, 0);
    uint16_t csr0 = lance.ReadWord(BaseAddr);
    EXPECT_NE(0, csr0 & 0x0020); // RXON
}
