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
#include <mutex>
#include <queue>

#include "IMemoryMappedDevice.h"

namespace Em68030::IO {

/// Virtual keyboard/mouse input device for Em68030.
/// Mapped at $FFFE9000, 32 bytes.
///
/// The host UI pushes input events into a thread-safe FIFO.
/// The guest driver reads events from the front and acknowledges them.
///
/// Register map (offset from base $FFFE9000):
///   $00-$03  R    MAGIC         0x454D4B4D ("EMKM")
///   $04      R    EVENT_COUNT   Number of pending events (0-255, clamped)
///   $05      R    EVENT_TYPE    Front event type: 1=key, 2=mouse-move, 3=mouse-btn
///   $06-$07  R    EVENT_CODE    Scancode (Linux KEY_* code) or mouse button ID
///   $08-$09  R    EVENT_VALUE   Key: 0=release,1=press. Mouse-move: signed delta-X
///   $0A-$0B  R    EVENT_VALUE2  Mouse-move: signed delta-Y. Mouse-btn: 0=rel,1=press
///   $0C      W    EVENT_ACK     Write any value to dequeue front event
///   $10      RW   IRQ_ENABLE    Bit 0: enable interrupt on event available
///   $11      R    IRQ_STATUS    Bit 0: event available (EVENT_COUNT > 0)
///   $14-$15  R    MOUSE_ABS_X   Absolute mouse X
///   $16-$17  R    MOUSE_ABS_Y   Absolute mouse Y
///   $18      RW   MOUSE_MODE    0=relative, 1=absolute (default)
///   $1C-$1D  R    SCREEN_WIDTH  Framebuffer width
///   $1E-$1F  R    SCREEN_HEIGHT Framebuffer height
class InputDevice : public IMemoryMappedDevice {
public:
    static constexpr uint32_t BASE_ADDRESS = 0xFFFE9000;
    static constexpr uint32_t DEVICE_SIZE = 32;
    static constexpr uint32_t MAGIC = 0x454D4B4D; // "EMKM"

    // Event types
    static constexpr uint8_t EVENT_KEY       = 1;
    static constexpr uint8_t EVENT_MOUSE_MOVE = 2;
    static constexpr uint8_t EVENT_MOUSE_BTN  = 3;

    InputDevice(uint16_t screenWidth, uint16_t screenHeight);

    // IMemoryMappedDevice
    uint8_t  ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    /// Push a key press/release event. code = Linux KEY_* code, value: 0=release, 1=press.
    void PushKeyEvent(uint16_t code, uint8_t value);

    /// Push a relative mouse move event.
    void PushMouseMoveEvent(int16_t dx, int16_t dy);

    /// Push an absolute mouse position update.
    void PushMouseAbsEvent(uint16_t x, uint16_t y);

    /// Push a mouse button press/release. button: Linux BTN_* code, value: 0=release, 1=press.
    void PushMouseButtonEvent(uint16_t button, uint8_t value);

    void SetScreenSize(uint16_t width, uint16_t height);

    /// Update absolute mouse position registers without pushing a FIFO event.
    /// Used alongside PushMouseMoveEvent so absolute-mode guest drivers can
    /// read the position directly from registers.
    void SetMouseAbsPosition(uint16_t x, uint16_t y);

    /// Push a string as a sequence of key press/release events (for paste).
    void PushTextInput(const std::string& text);

private:
    struct InputEvent {
        uint8_t  type;       // EVENT_KEY, EVENT_MOUSE_MOVE, EVENT_MOUSE_BTN
        uint16_t code;       // Scancode or button code
        int16_t  value;      // Key: 0/1, Mouse-move: delta-X, Mouse-btn: 0/1
        int16_t  value2;     // Mouse-move: delta-Y, otherwise 0
    };

    std::queue<InputEvent> m_fifo;
    std::mutex m_mutex;

    uint16_t m_screenWidth;
    uint16_t m_screenHeight;
    uint16_t m_mouseAbsX = 0;
    uint16_t m_mouseAbsY = 0;
    uint8_t  m_mouseMode = 1; // 1 = absolute (default)
    uint8_t  m_irqEnable = 0;
};

} // namespace Em68030::IO
