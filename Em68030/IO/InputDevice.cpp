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
#include "InputDevice.h"
#include "KeyMapping.h"

namespace Em68030::IO {

InputDevice::InputDevice(uint16_t screenWidth, uint16_t screenHeight)
    : m_screenWidth(screenWidth), m_screenHeight(screenHeight)
{
}

uint8_t InputDevice::ReadByte(uint32_t address) {
    uint32_t offset = address - BASE_ADDRESS;
    std::lock_guard lock(m_mutex);

    switch (offset) {
        // MAGIC ($00-$03)
        case 0x00: return static_cast<uint8_t>(MAGIC >> 24);
        case 0x01: return static_cast<uint8_t>(MAGIC >> 16);
        case 0x02: return static_cast<uint8_t>(MAGIC >> 8);
        case 0x03: return static_cast<uint8_t>(MAGIC);

        // EVENT_COUNT ($04)
        case 0x04: {
            auto count = m_fifo.size();
            return static_cast<uint8_t>(count > 255 ? 255 : count);
        }

        // EVENT_TYPE ($05)
        case 0x05:
            return m_fifo.empty() ? 0 : m_fifo.front().type;

        // EVENT_CODE ($06-$07)
        case 0x06:
            return m_fifo.empty() ? 0 : static_cast<uint8_t>(m_fifo.front().code >> 8);
        case 0x07:
            return m_fifo.empty() ? 0 : static_cast<uint8_t>(m_fifo.front().code);

        // EVENT_VALUE ($08-$09)
        case 0x08:
            return m_fifo.empty() ? 0 : static_cast<uint8_t>(static_cast<uint16_t>(m_fifo.front().value) >> 8);
        case 0x09:
            return m_fifo.empty() ? 0 : static_cast<uint8_t>(m_fifo.front().value);

        // EVENT_VALUE2 ($0A-$0B)
        case 0x0A:
            return m_fifo.empty() ? 0 : static_cast<uint8_t>(static_cast<uint16_t>(m_fifo.front().value2) >> 8);
        case 0x0B:
            return m_fifo.empty() ? 0 : static_cast<uint8_t>(m_fifo.front().value2);

        // IRQ_ENABLE ($10)
        case 0x10:
            return m_irqEnable;

        // IRQ_STATUS ($11)
        case 0x11:
            return m_fifo.empty() ? 0 : 1;

        // MOUSE_ABS_X ($14-$15)
        case 0x14: return static_cast<uint8_t>(m_mouseAbsX >> 8);
        case 0x15: return static_cast<uint8_t>(m_mouseAbsX);

        // MOUSE_ABS_Y ($16-$17)
        case 0x16: return static_cast<uint8_t>(m_mouseAbsY >> 8);
        case 0x17: return static_cast<uint8_t>(m_mouseAbsY);

        // MOUSE_MODE ($18)
        case 0x18:
            return m_mouseMode;

        // SCREEN_WIDTH ($1C-$1D)
        case 0x1C: return static_cast<uint8_t>(m_screenWidth >> 8);
        case 0x1D: return static_cast<uint8_t>(m_screenWidth);

        // SCREEN_HEIGHT ($1E-$1F)
        case 0x1E: return static_cast<uint8_t>(m_screenHeight >> 8);
        case 0x1F: return static_cast<uint8_t>(m_screenHeight);

        default: return 0;
    }
}

void InputDevice::WriteByte(uint32_t address, uint8_t value) {
    uint32_t offset = address - BASE_ADDRESS;
    std::lock_guard lock(m_mutex);

    switch (offset) {
        // EVENT_ACK ($0C) - dequeue front event
        case 0x0C:
            if (!m_fifo.empty())
                m_fifo.pop();
            break;

        // IRQ_ENABLE ($10)
        case 0x10:
            m_irqEnable = value & 1;
            break;

        // MOUSE_MODE ($18)
        case 0x18:
            m_mouseMode = value & 1;
            break;
    }
}

uint16_t InputDevice::ReadWord(uint32_t address) {
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t InputDevice::ReadLong(uint32_t address) {
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void InputDevice::WriteWord(uint32_t address, uint16_t value) {
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void InputDevice::WriteLong(uint32_t address, uint32_t value) {
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

void InputDevice::PushKeyEvent(uint16_t code, uint8_t value) {
    std::lock_guard lock(m_mutex);
    m_fifo.push({ EVENT_KEY, code, static_cast<int16_t>(value), 0 });
}

void InputDevice::PushMouseMoveEvent(int16_t dx, int16_t dy) {
    std::lock_guard lock(m_mutex);
    m_fifo.push({ EVENT_MOUSE_MOVE, 0, dx, dy });
}

void InputDevice::PushMouseAbsEvent(uint16_t x, uint16_t y) {
    std::lock_guard lock(m_mutex);
    m_mouseAbsX = x;
    m_mouseAbsY = y;
    // Also push as event so the guest can detect position changes via polling
    m_fifo.push({ EVENT_MOUSE_MOVE, 0, static_cast<int16_t>(x), static_cast<int16_t>(y) });
}

void InputDevice::PushMouseButtonEvent(uint16_t button, uint8_t value) {
    std::lock_guard lock(m_mutex);
    m_fifo.push({ EVENT_MOUSE_BTN, button, static_cast<int16_t>(value), 0 });
}

void InputDevice::SetScreenSize(uint16_t width, uint16_t height) {
    std::lock_guard lock(m_mutex);
    m_screenWidth = width;
    m_screenHeight = height;
}

void InputDevice::SetMouseAbsPosition(uint16_t x, uint16_t y) {
    std::lock_guard lock(m_mutex);
    m_mouseAbsX = x;
    m_mouseAbsY = y;
}

void InputDevice::PushTextInput(const std::string& text) {
    constexpr uint16_t KEY_LEFTSHIFT = 42;

    for (char ch : text) {
        if (ch == '\r') continue; // Skip CR in CRLF — LF alone produces KEY_ENTER
        auto [keyCode, needShift] = CharToScancode(ch);
        if (keyCode == 0) continue;

        if (needShift)
            PushKeyEvent(KEY_LEFTSHIFT, 1);
        PushKeyEvent(keyCode, 1);
        PushKeyEvent(keyCode, 0);
        if (needShift)
            PushKeyEvent(KEY_LEFTSHIFT, 0);
    }
}

} // namespace Em68030::IO
