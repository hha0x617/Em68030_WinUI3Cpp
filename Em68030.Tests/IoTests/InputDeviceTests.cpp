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

#include "IO/InputDevice.h"

namespace Em68030::Tests {

class InputDeviceTest : public ::testing::Test {
protected:
    static constexpr uint32_t Base = IO::InputDevice::BASE_ADDRESS;
    IO::InputDevice device{640, 480};
};

// ============================================================================
// Magic register
// ============================================================================

TEST_F(InputDeviceTest, ReadMagic_ReturnsEMKM) {
    uint32_t magic = device.ReadLong(Base + 0x00);
    EXPECT_EQ(0x454D4B4Du, magic);
}

// ============================================================================
// Initial state (empty FIFO)
// ============================================================================

TEST_F(InputDeviceTest, InitialEventCount_IsZero) {
    EXPECT_EQ(0, device.ReadByte(Base + 0x04));
}

TEST_F(InputDeviceTest, InitialEventType_IsZero) {
    EXPECT_EQ(0, device.ReadByte(Base + 0x05));
}

TEST_F(InputDeviceTest, InitialEventCode_IsZero) {
    EXPECT_EQ(0, device.ReadWord(Base + 0x06));
}

TEST_F(InputDeviceTest, InitialIrqEnable_IsZero) {
    EXPECT_EQ(0, device.ReadByte(Base + 0x10));
}

TEST_F(InputDeviceTest, InitialIrqStatus_IsZero) {
    EXPECT_EQ(0, device.ReadByte(Base + 0x11));
}

TEST_F(InputDeviceTest, InitialMouseMode_IsAbsolute) {
    EXPECT_EQ(1, device.ReadByte(Base + 0x18));
}

// ============================================================================
// Screen size registers
// ============================================================================

TEST_F(InputDeviceTest, ReadScreenWidth_Returns640) {
    EXPECT_EQ(640, device.ReadWord(Base + 0x1C));
}

TEST_F(InputDeviceTest, ReadScreenHeight_Returns480) {
    EXPECT_EQ(480, device.ReadWord(Base + 0x1E));
}

TEST_F(InputDeviceTest, SetScreenSize_UpdatesRegisters) {
    device.SetScreenSize(800, 600);
    EXPECT_EQ(800, device.ReadWord(Base + 0x1C));
    EXPECT_EQ(600, device.ReadWord(Base + 0x1E));
}

// ============================================================================
// Key events
// ============================================================================

TEST_F(InputDeviceTest, PushKeyEvent_IncrementsEventCount) {
    device.PushKeyEvent(30, 1); // KEY_A = 30, press
    EXPECT_EQ(1, device.ReadByte(Base + 0x04));
}

TEST_F(InputDeviceTest, PushKeyEvent_SetsEventType) {
    device.PushKeyEvent(30, 1);
    EXPECT_EQ(IO::InputDevice::EVENT_KEY, device.ReadByte(Base + 0x05));
}

TEST_F(InputDeviceTest, PushKeyEvent_SetsEventCode) {
    device.PushKeyEvent(30, 1);
    EXPECT_EQ(30, device.ReadWord(Base + 0x06));
}

TEST_F(InputDeviceTest, PushKeyEvent_Press_SetsValue1) {
    device.PushKeyEvent(30, 1);
    EXPECT_EQ(1, device.ReadWord(Base + 0x08));
}

TEST_F(InputDeviceTest, PushKeyEvent_Release_SetsValue0) {
    device.PushKeyEvent(30, 0);
    EXPECT_EQ(0, device.ReadWord(Base + 0x08));
}

TEST_F(InputDeviceTest, PushKeyEvent_Value2IsZero) {
    device.PushKeyEvent(30, 1);
    EXPECT_EQ(0, device.ReadWord(Base + 0x0A));
}

// ============================================================================
// Event ACK (dequeue)
// ============================================================================

TEST_F(InputDeviceTest, EventAck_DequeueFrontEvent) {
    device.PushKeyEvent(30, 1);
    device.PushKeyEvent(31, 1);
    EXPECT_EQ(2, device.ReadByte(Base + 0x04));

    device.WriteByte(Base + 0x0C, 0xFF); // ACK
    EXPECT_EQ(1, device.ReadByte(Base + 0x04));
    EXPECT_EQ(31, device.ReadWord(Base + 0x06)); // second event is now front
}

TEST_F(InputDeviceTest, EventAck_EmptyFifo_DoesNotCrash) {
    device.WriteByte(Base + 0x0C, 0xFF); // ACK on empty FIFO
    EXPECT_EQ(0, device.ReadByte(Base + 0x04));
}

TEST_F(InputDeviceTest, EventAck_AllEvents_FifoBecomesEmpty) {
    device.PushKeyEvent(30, 1);
    device.PushKeyEvent(30, 0);
    device.WriteByte(Base + 0x0C, 0); // ACK
    device.WriteByte(Base + 0x0C, 0); // ACK
    EXPECT_EQ(0, device.ReadByte(Base + 0x04));
    EXPECT_EQ(0, device.ReadByte(Base + 0x05)); // type=0 when empty
}

// ============================================================================
// Mouse move events
// ============================================================================

TEST_F(InputDeviceTest, PushMouseMoveEvent_SetsType) {
    device.PushMouseMoveEvent(10, -5);
    EXPECT_EQ(IO::InputDevice::EVENT_MOUSE_MOVE, device.ReadByte(Base + 0x05));
}

TEST_F(InputDeviceTest, PushMouseMoveEvent_SetsDeltaX) {
    device.PushMouseMoveEvent(10, -5);
    int16_t dx = static_cast<int16_t>(device.ReadWord(Base + 0x08));
    EXPECT_EQ(10, dx);
}

TEST_F(InputDeviceTest, PushMouseMoveEvent_SetsDeltaY) {
    device.PushMouseMoveEvent(10, -5);
    int16_t dy = static_cast<int16_t>(device.ReadWord(Base + 0x0A));
    EXPECT_EQ(-5, dy);
}

TEST_F(InputDeviceTest, PushMouseMoveEvent_NegativeDelta) {
    device.PushMouseMoveEvent(-100, -200);
    int16_t dx = static_cast<int16_t>(device.ReadWord(Base + 0x08));
    int16_t dy = static_cast<int16_t>(device.ReadWord(Base + 0x0A));
    EXPECT_EQ(-100, dx);
    EXPECT_EQ(-200, dy);
}

// ============================================================================
// Mouse button events
// ============================================================================

TEST_F(InputDeviceTest, PushMouseButtonEvent_SetsType) {
    device.PushMouseButtonEvent(0x110, 1); // BTN_LEFT
    EXPECT_EQ(IO::InputDevice::EVENT_MOUSE_BTN, device.ReadByte(Base + 0x05));
}

TEST_F(InputDeviceTest, PushMouseButtonEvent_SetsButtonCode) {
    device.PushMouseButtonEvent(0x110, 1);
    EXPECT_EQ(0x110, device.ReadWord(Base + 0x06));
}

TEST_F(InputDeviceTest, PushMouseButtonEvent_Press_SetsValue1) {
    device.PushMouseButtonEvent(0x110, 1);
    EXPECT_EQ(1, device.ReadWord(Base + 0x08));
}

TEST_F(InputDeviceTest, PushMouseButtonEvent_Release_SetsValue0) {
    device.PushMouseButtonEvent(0x110, 0);
    EXPECT_EQ(0, device.ReadWord(Base + 0x08));
}

// ============================================================================
// Mouse absolute position
// ============================================================================

TEST_F(InputDeviceTest, PushMouseAbsEvent_UpdatesAbsRegisters) {
    device.PushMouseAbsEvent(320, 240);
    EXPECT_EQ(320, device.ReadWord(Base + 0x14));
    EXPECT_EQ(240, device.ReadWord(Base + 0x16));
}

TEST_F(InputDeviceTest, PushMouseAbsEvent_AlsoPushesEvent) {
    device.PushMouseAbsEvent(100, 200);
    EXPECT_EQ(1, device.ReadByte(Base + 0x04));
    EXPECT_EQ(IO::InputDevice::EVENT_MOUSE_MOVE, device.ReadByte(Base + 0x05));
}

// ============================================================================
// Mouse mode register
// ============================================================================

TEST_F(InputDeviceTest, WriteMouseMode_SetsRelative) {
    device.WriteByte(Base + 0x18, 0);
    EXPECT_EQ(0, device.ReadByte(Base + 0x18));
}

TEST_F(InputDeviceTest, WriteMouseMode_SetsAbsolute) {
    device.WriteByte(Base + 0x18, 0);
    device.WriteByte(Base + 0x18, 1);
    EXPECT_EQ(1, device.ReadByte(Base + 0x18));
}

// ============================================================================
// IRQ registers
// ============================================================================

TEST_F(InputDeviceTest, WriteIrqEnable_SetsFlag) {
    device.WriteByte(Base + 0x10, 1);
    EXPECT_EQ(1, device.ReadByte(Base + 0x10));
}

TEST_F(InputDeviceTest, IrqStatus_ReflectsEventCount) {
    EXPECT_EQ(0, device.ReadByte(Base + 0x11));
    device.PushKeyEvent(30, 1);
    EXPECT_EQ(1, device.ReadByte(Base + 0x11));
    device.WriteByte(Base + 0x0C, 0); // ACK
    EXPECT_EQ(0, device.ReadByte(Base + 0x11));
}

// ============================================================================
// Event count clamping
// ============================================================================

TEST_F(InputDeviceTest, EventCount_ClampsAt255) {
    for (int i = 0; i < 300; i++)
        device.PushKeyEvent(30, 1);
    EXPECT_EQ(255, device.ReadByte(Base + 0x04));
}

// ============================================================================
// Multiple event types in sequence
// ============================================================================

TEST_F(InputDeviceTest, MixedEvents_FifoOrder) {
    device.PushKeyEvent(30, 1);                 // event 0: key press
    device.PushMouseMoveEvent(5, -3);           // event 1: mouse move
    device.PushMouseButtonEvent(0x110, 1);      // event 2: mouse button

    // Event 0: key
    EXPECT_EQ(IO::InputDevice::EVENT_KEY, device.ReadByte(Base + 0x05));
    EXPECT_EQ(30, device.ReadWord(Base + 0x06));
    device.WriteByte(Base + 0x0C, 0); // ACK

    // Event 1: mouse move
    EXPECT_EQ(IO::InputDevice::EVENT_MOUSE_MOVE, device.ReadByte(Base + 0x05));
    int16_t dx = static_cast<int16_t>(device.ReadWord(Base + 0x08));
    int16_t dy = static_cast<int16_t>(device.ReadWord(Base + 0x0A));
    EXPECT_EQ(5, dx);
    EXPECT_EQ(-3, dy);
    device.WriteByte(Base + 0x0C, 0); // ACK

    // Event 2: mouse button
    EXPECT_EQ(IO::InputDevice::EVENT_MOUSE_BTN, device.ReadByte(Base + 0x05));
    EXPECT_EQ(0x110, device.ReadWord(Base + 0x06));
    device.WriteByte(Base + 0x0C, 0); // ACK

    EXPECT_EQ(0, device.ReadByte(Base + 0x04));
}

// ============================================================================
// Unknown offset
// ============================================================================

TEST_F(InputDeviceTest, ReadUnknownOffset_ReturnsZero) {
    EXPECT_EQ(0, device.ReadByte(Base + 0x09));
}

TEST_F(InputDeviceTest, WriteUnknownOffset_DoesNotCrash) {
    device.WriteByte(Base + 0x1F, 0xFF);
}

} // namespace Em68030::Tests
