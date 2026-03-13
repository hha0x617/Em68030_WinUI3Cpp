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

#include "IO/FramebufferDevice.h"
#include "Core/Memory.h"

namespace Em68030::Tests {

class FramebufferDeviceTest : public ::testing::Test {
protected:
    static constexpr uint32_t Base = IO::FramebufferDevice::BASE_ADDRESS;
    IO::FramebufferDevice device{640, 480, 16, 0x00800000};
};

// ============================================================================
// Register read tests
// ============================================================================

TEST_F(FramebufferDeviceTest, ReadMagic_ReturnsEMFB) {
    uint32_t magic = device.ReadLong(Base + 0x00);
    EXPECT_EQ(0x454D4642u, magic);
}

TEST_F(FramebufferDeviceTest, ReadWidth_Returns640) {
    uint16_t w = device.ReadWord(Base + 0x04);
    EXPECT_EQ(640, w);
}

TEST_F(FramebufferDeviceTest, ReadHeight_Returns480) {
    uint16_t h = device.ReadWord(Base + 0x06);
    EXPECT_EQ(480, h);
}

TEST_F(FramebufferDeviceTest, ReadBpp_Returns16) {
    uint8_t bpp = device.ReadByte(Base + 0x08);
    EXPECT_EQ(16, bpp);
}

TEST_F(FramebufferDeviceTest, ReadStride_Returns1280) {
    uint16_t stride = device.ReadWord(Base + 0x0A);
    EXPECT_EQ(1280, stride); // 640 * 16 / 8
}

TEST_F(FramebufferDeviceTest, ReadVramBase_Returns0x800000) {
    uint32_t vramBase = device.ReadLong(Base + 0x0C);
    EXPECT_EQ(0x00800000u, vramBase);
}

TEST_F(FramebufferDeviceTest, ReadVramSize_Returns614400) {
    uint32_t vramSize = device.ReadLong(Base + 0x10);
    EXPECT_EQ(static_cast<uint32_t>(640 * 480 * 2), vramSize);
}

// ============================================================================
// Enable register tests
// ============================================================================

TEST_F(FramebufferDeviceTest, ReadEnable_DefaultIsEnabled) {
    uint8_t enable = device.ReadByte(Base + 0x14);
    EXPECT_EQ(1, enable);
}

TEST_F(FramebufferDeviceTest, WriteEnable_DisablesDisplay) {
    device.WriteByte(Base + 0x14, 0);
    EXPECT_FALSE(device.Enabled());
}

TEST_F(FramebufferDeviceTest, WriteEnable_ReenablesDisplay) {
    device.WriteByte(Base + 0x14, 0);
    device.WriteByte(Base + 0x14, 1);
    EXPECT_TRUE(device.Enabled());
}

// ============================================================================
// Unknown offset tests
// ============================================================================

TEST_F(FramebufferDeviceTest, ReadUnknownOffset_ReturnsZero) {
    uint8_t val = device.ReadByte(Base + 0x30);
    EXPECT_EQ(0, val);
}

TEST_F(FramebufferDeviceTest, WriteUnknownOffset_DoesNotCrash) {
    device.WriteByte(Base + 0x30, 0xFF);
    // No exception expected
}

// ============================================================================
// Palette tests
// ============================================================================

TEST_F(FramebufferDeviceTest, PaletteWrite_SetsEntry) {
    device.WriteByte(Base + 0x20, 10);   // palette index = 10
    device.WriteByte(Base + 0x21, 0xFF); // R
    device.WriteByte(Base + 0x22, 0x80); // G
    device.WriteByte(Base + 0x23, 0x40); // B (auto-increments index)

    auto [r, g, b] = device.GetPaletteEntry(10);
    EXPECT_EQ(0xFF, r);
    EXPECT_EQ(0x80, g);
    EXPECT_EQ(0x40, b);
}

TEST_F(FramebufferDeviceTest, PaletteWrite_AutoIncrements) {
    device.WriteByte(Base + 0x20, 5);    // start at index 5
    device.WriteByte(Base + 0x21, 0x10); // R for entry 5
    device.WriteByte(Base + 0x22, 0x20); // G for entry 5
    device.WriteByte(Base + 0x23, 0x30); // B for entry 5, index becomes 6

    device.WriteByte(Base + 0x21, 0xA0); // R for entry 6
    device.WriteByte(Base + 0x22, 0xB0); // G for entry 6
    device.WriteByte(Base + 0x23, 0xC0); // B for entry 6

    auto [r5, g5, b5] = device.GetPaletteEntry(5);
    EXPECT_EQ(0x10, r5);
    EXPECT_EQ(0x20, g5);
    EXPECT_EQ(0x30, b5);

    auto [r6, g6, b6] = device.GetPaletteEntry(6);
    EXPECT_EQ(0xA0, r6);
    EXPECT_EQ(0xB0, g6);
    EXPECT_EQ(0xC0, b6);
}

TEST_F(FramebufferDeviceTest, DefaultPalette_IsGrayscale) {
    auto [r, g, b] = device.GetPaletteEntry(128);
    EXPECT_EQ(128, r);
    EXPECT_EQ(128, g);
    EXPECT_EQ(128, b);
}

// ============================================================================
// Property tests
// ============================================================================

TEST_F(FramebufferDeviceTest, Properties_MatchConstructorArgs) {
    EXPECT_EQ(640, device.Width());
    EXPECT_EQ(480, device.Height());
    EXPECT_EQ(16, device.Bpp());
    EXPECT_EQ(1280, device.Stride());
    EXPECT_EQ(0x00800000u, device.VramBase());
    EXPECT_EQ(static_cast<uint32_t>(640 * 480 * 2), device.VramSize());
}

TEST_F(FramebufferDeviceTest, Constructor_8bpp_CorrectStride) {
    IO::FramebufferDevice dev8(800, 600, 8, 0x00800000);
    EXPECT_EQ(800, dev8.Stride());
    EXPECT_EQ(static_cast<uint32_t>(800 * 600), dev8.VramSize());
}

TEST_F(FramebufferDeviceTest, Constructor_32bpp_CorrectStride) {
    IO::FramebufferDevice dev32(640, 480, 32, 0x00800000);
    EXPECT_EQ(2560, dev32.Stride());
    EXPECT_EQ(static_cast<uint32_t>(640 * 480 * 4), dev32.VramSize());
}

// ============================================================================
// VRAM in fast RAM test
// ============================================================================

TEST_F(FramebufferDeviceTest, VramInFastRam_IsAccessible) {
    Core::Memory memory;
    memory.AddRegion(0x00000000, 16 * 1024 * 1024, Core::RegionType::Ram);
    memory.WriteByte(0x00800000, 0xAB);
    memory.WriteByte(0x00800001, 0xCD);
    EXPECT_EQ(0xAB, memory.ReadByte(0x00800000));
    EXPECT_EQ(0xCD, memory.ReadByte(0x00800001));
}

} // namespace Em68030::Tests
