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

#pragma once

#include <cstdint>
#include "IMemoryMappedDevice.h"

namespace Em68030::IO {

/// Framebuffer control register device for Em68030.
/// Mapped at $FFFE8000, 64 bytes.
///
/// VRAM itself is in the main RAM array (fast path), not routed through this device.
/// This device provides identification and palette registers only.
///
/// Register map (offset from base $FFFE8000):
///   $00-$03  R    MAGIC      0x454D4642 ("EMFB")
///   $04-$05  R    WIDTH      Horizontal resolution
///   $06-$07  R    HEIGHT     Vertical resolution
///   $08      R    BPP        Bits per pixel (8/16/32)
///   $0A-$0B  R    STRIDE     Bytes per row
///   $0C-$0F  R    VRAM_BASE  VRAM physical address
///   $10-$13  R    VRAM_SIZE  VRAM size in bytes
///   $14      RW   ENABLE     Display enable (1) / disable (0)
///   $20      W    PAL_INDEX  Palette index (0-255, for 8bpp mode)
///   $21      W    PAL_R      Palette Red
///   $22      W    PAL_G      Palette Green
///   $23      W    PAL_B      Palette Blue (auto-increments index)
class FramebufferDevice : public IMemoryMappedDevice {
public:
    static constexpr uint32_t BASE_ADDRESS = 0xFFFE8000;
    static constexpr uint32_t DEVICE_SIZE = 64;
    static constexpr uint32_t MAGIC = 0x454D4642; // "EMFB"

    FramebufferDevice(int width, int height, int bpp, uint32_t vramBase);

    // IMemoryMappedDevice
    uint8_t  ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    bool Enabled() const { return _enable != 0; }
    int Width() const { return _width; }
    int Height() const { return _height; }
    int Bpp() const { return _bpp; }
    int Stride() const { return _stride; }
    uint32_t VramBase() const { return _vramBase; }
    uint32_t VramSize() const { return _vramSize; }

    struct PaletteEntry { uint8_t r, g, b; };
    PaletteEntry GetPaletteEntry(int index) const;

private:
    int _width;
    int _height;
    int _bpp;
    int _stride;
    uint32_t _vramBase;
    uint32_t _vramSize;
    uint8_t _enable = 1;

    uint8_t _palette[256][3]; // [index][R,G,B]
    uint8_t _paletteIndex = 0;
};

} // namespace Em68030::IO
