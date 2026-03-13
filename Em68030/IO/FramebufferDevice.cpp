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
#include "FramebufferDevice.h"
#include <cstring>

namespace Em68030::IO {

FramebufferDevice::FramebufferDevice(int width, int height, int bpp, uint32_t vramBase)
    : _width(width), _height(height), _bpp(bpp),
      _stride(width * bpp / 8), _vramBase(vramBase),
      _vramSize(static_cast<uint32_t>(_stride * height))
{
    // Initialize default grayscale palette
    for (int i = 0; i < 256; i++) {
        _palette[i][0] = static_cast<uint8_t>(i);
        _palette[i][1] = static_cast<uint8_t>(i);
        _palette[i][2] = static_cast<uint8_t>(i);
    }
}

uint8_t FramebufferDevice::ReadByte(uint32_t address) {
    uint32_t offset = address - BASE_ADDRESS;
    switch (offset) {
        case 0x00: return static_cast<uint8_t>(MAGIC >> 24);
        case 0x01: return static_cast<uint8_t>(MAGIC >> 16);
        case 0x02: return static_cast<uint8_t>(MAGIC >> 8);
        case 0x03: return static_cast<uint8_t>(MAGIC);
        case 0x04: return static_cast<uint8_t>(_width >> 8);
        case 0x05: return static_cast<uint8_t>(_width);
        case 0x06: return static_cast<uint8_t>(_height >> 8);
        case 0x07: return static_cast<uint8_t>(_height);
        case 0x08: return static_cast<uint8_t>(_bpp);
        case 0x0A: return static_cast<uint8_t>(_stride >> 8);
        case 0x0B: return static_cast<uint8_t>(_stride);
        case 0x0C: return static_cast<uint8_t>(_vramBase >> 24);
        case 0x0D: return static_cast<uint8_t>(_vramBase >> 16);
        case 0x0E: return static_cast<uint8_t>(_vramBase >> 8);
        case 0x0F: return static_cast<uint8_t>(_vramBase);
        case 0x10: return static_cast<uint8_t>(_vramSize >> 24);
        case 0x11: return static_cast<uint8_t>(_vramSize >> 16);
        case 0x12: return static_cast<uint8_t>(_vramSize >> 8);
        case 0x13: return static_cast<uint8_t>(_vramSize);
        case 0x14: return _enable;
        default: return 0;
    }
}

void FramebufferDevice::WriteByte(uint32_t address, uint8_t value) {
    uint32_t offset = address - BASE_ADDRESS;
    switch (offset) {
        case 0x14:
            _enable = value & 1;
            break;
        case 0x20:
            _paletteIndex = value;
            break;
        case 0x21:
            _palette[_paletteIndex][0] = value; // R
            break;
        case 0x22:
            _palette[_paletteIndex][1] = value; // G
            break;
        case 0x23:
            _palette[_paletteIndex][2] = value; // B
            _paletteIndex++; // auto-increment after B write
            break;
    }
}

uint16_t FramebufferDevice::ReadWord(uint32_t address) {
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t FramebufferDevice::ReadLong(uint32_t address) {
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void FramebufferDevice::WriteWord(uint32_t address, uint16_t value) {
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void FramebufferDevice::WriteLong(uint32_t address, uint32_t value) {
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

FramebufferDevice::PaletteEntry FramebufferDevice::GetPaletteEntry(int index) const {
    if (index < 0 || index > 255) return {0, 0, 0};
    return {_palette[index][0], _palette[index][1], _palette[index][2]};
}

} // namespace Em68030::IO
