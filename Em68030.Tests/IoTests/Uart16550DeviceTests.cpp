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

#include "IO/Uart16550Device.h"

namespace Em68030::Tests {

// ============================================================================
// Uart16550Device Tests
// ============================================================================

class Uart16550DeviceTest : public ::testing::Test {
protected:
    static constexpr uint32_t Base = 0xFFFE4000;
    IO::Uart16550Device uart{Base};
    std::vector<uint8_t> transmitted;
    int interruptCallCount = 0;
    bool lastInterruptState = false;

    void SetUp() override {
        transmitted.clear();
        interruptCallCount = 0;
        lastInterruptState = false;
        uart.OnTransmit = [this](uint8_t ch) { transmitted.push_back(ch); };
        uart.InterruptOutput = [this](bool active) {
            interruptCallCount++;
            lastInterruptState = active;
        };
    }

    uint8_t ReadReg(uint32_t reg) { return uart.ReadByte(Base + reg); }
    void WriteReg(uint32_t reg, uint8_t val) { uart.WriteByte(Base + reg, val); }

    // Enable DLAB to access divisor latch registers
    void EnableDLAB() { WriteReg(3, ReadReg(3) | 0x80); }
    void DisableDLAB() { WriteReg(3, ReadReg(3) & ~0x80); }
};

// ============================================================================
// LSR default state
// ============================================================================

TEST_F(Uart16550DeviceTest, LSR_DefaultState_TxReadyNoRxData) {
    uint8_t lsr = ReadReg(5);
    EXPECT_EQ(lsr & 0x01, 0);     // DR = 0 (no data)
    EXPECT_NE(lsr & 0x20, 0);     // THRE = 1 (TX holding empty)
    EXPECT_NE(lsr & 0x40, 0);     // TEMT = 1 (transmitter empty)
}

// ============================================================================
// THR / RBR (transmit / receive)
// ============================================================================

TEST_F(Uart16550DeviceTest, WriteTHR_InvokesOnTransmit) {
    WriteReg(0, 'A');
    ASSERT_EQ(transmitted.size(), 1u);
    EXPECT_EQ(transmitted[0], 'A');
}

TEST_F(Uart16550DeviceTest, WriteTHR_MultipleChars) {
    WriteReg(0, 'H');
    WriteReg(0, 'i');
    ASSERT_EQ(transmitted.size(), 2u);
    EXPECT_EQ(transmitted[0], 'H');
    EXPECT_EQ(transmitted[1], 'i');
}

TEST_F(Uart16550DeviceTest, ReceiveChar_SetsDataReady) {
    uart.ReceiveChar('X');
    uint8_t lsr = ReadReg(5);
    EXPECT_NE(lsr & 0x01, 0); // DR = 1
}

TEST_F(Uart16550DeviceTest, ReadRBR_ReturnsReceivedChar) {
    uart.ReceiveChar('Z');
    uint8_t ch = ReadReg(0);
    EXPECT_EQ(ch, 'Z');
}

TEST_F(Uart16550DeviceTest, ReadRBR_ClearsDataReady) {
    uart.ReceiveChar('A');
    ReadReg(0); // consume
    uint8_t lsr = ReadReg(5);
    EXPECT_EQ(lsr & 0x01, 0); // DR = 0
}

TEST_F(Uart16550DeviceTest, ReadRBR_EmptyFifo_ReturnsZero) {
    uint8_t ch = ReadReg(0);
    EXPECT_EQ(ch, 0);
}

TEST_F(Uart16550DeviceTest, ReceiveChar_FIFO_PreservesOrder) {
    uart.ReceiveChar('A');
    uart.ReceiveChar('B');
    uart.ReceiveChar('C');
    EXPECT_EQ(ReadReg(0), 'A');
    EXPECT_EQ(ReadReg(0), 'B');
    EXPECT_EQ(ReadReg(0), 'C');
}

TEST_F(Uart16550DeviceTest, ReceiveChar_FifoFull_DropsCharacter) {
    // Fill the 64-byte FIFO
    for (int i = 0; i < 64; i++)
        uart.ReceiveChar(static_cast<uint8_t>(i));
    // 65th character should be dropped
    uart.ReceiveChar(0xFF);
    // Read all 64 — last should be 63, not 0xFF
    uint8_t last = 0;
    for (int i = 0; i < 64; i++)
        last = ReadReg(0);
    EXPECT_EQ(last, 63);
    // FIFO should now be empty
    EXPECT_EQ(ReadReg(5) & 0x01, 0);
}

// ============================================================================
// IER (Interrupt Enable Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, IER_DefaultIsZero) {
    EXPECT_EQ(ReadReg(1), 0);
}

TEST_F(Uart16550DeviceTest, IER_WriteAndReadBack) {
    WriteReg(1, 0x0F);
    EXPECT_EQ(ReadReg(1), 0x0F);
}

TEST_F(Uart16550DeviceTest, IER_MasksUpperBits) {
    WriteReg(1, 0xFF);
    EXPECT_EQ(ReadReg(1), 0x0F);
}

// ============================================================================
// IIR (Interrupt Identification Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, IIR_Default_NoInterrupt) {
    uint8_t iir = ReadReg(2);
    EXPECT_NE(iir & 0x01, 0); // Bit 0 = 1 means no interrupt pending
}

TEST_F(Uart16550DeviceTest, IIR_RxDataInterrupt) {
    WriteReg(1, 0x01); // Enable RDI
    uart.ReceiveChar('A');
    uint8_t iir = ReadReg(2);
    EXPECT_EQ(iir & 0x01, 0);  // Interrupt pending
    EXPECT_EQ(iir & 0x0E, 0x04); // RX data available (priority 2)
}

TEST_F(Uart16550DeviceTest, IIR_TxEmptyInterrupt) {
    WriteReg(1, 0x02); // Enable THRI
    WriteReg(0, 'A');  // THR write sets thrEmpty
    uint8_t iir = ReadReg(2);
    EXPECT_EQ(iir & 0x01, 0);  // Interrupt pending
    EXPECT_EQ(iir & 0x0E, 0x02); // TX empty (priority 3)
}

TEST_F(Uart16550DeviceTest, IIR_RxPriorityOverTx) {
    WriteReg(1, 0x03); // Enable both RDI and THRI
    uart.ReceiveChar('A');
    WriteReg(0, 'B');  // Also trigger TX empty
    uint8_t iir = ReadReg(2);
    EXPECT_EQ(iir & 0x0E, 0x04); // RX has higher priority
}

TEST_F(Uart16550DeviceTest, IIR_FifoEnabledBits) {
    WriteReg(2, 0x01); // Enable FIFO via FCR
    uint8_t iir = ReadReg(2);
    EXPECT_EQ(iir & 0xC0, 0xC0); // FIFO enabled bits set
}

TEST_F(Uart16550DeviceTest, IIR_FifoDisabledBits) {
    uint8_t iir = ReadReg(2);
    EXPECT_EQ(iir & 0xC0, 0x00); // FIFO not enabled
}

// ============================================================================
// InterruptOutput callback
// ============================================================================

TEST_F(Uart16550DeviceTest, InterruptOutput_FiredOnRxData) {
    WriteReg(1, 0x01); // Enable RDI
    uart.ReceiveChar('A');
    EXPECT_TRUE(lastInterruptState);
}

TEST_F(Uart16550DeviceTest, InterruptOutput_ClearedWhenRxConsumed) {
    WriteReg(1, 0x01); // Enable RDI
    uart.ReceiveChar('A');
    ReadReg(0); // consume
    EXPECT_FALSE(lastInterruptState);
}

TEST_F(Uart16550DeviceTest, InterruptOutput_NotFiredWhenDisabled) {
    // IER = 0, no interrupts enabled
    interruptCallCount = 0;
    uart.ReceiveChar('A');
    // UpdateInterrupt was called but should report no interrupt
    EXPECT_FALSE(lastInterruptState);
}

// ============================================================================
// DLAB (Divisor Latch Access Bit)
// ============================================================================

TEST_F(Uart16550DeviceTest, DLAB_AccessDivisorLatch) {
    EnableDLAB();
    WriteReg(0, 0x60); // DLL
    WriteReg(1, 0x00); // DLM
    EXPECT_EQ(ReadReg(0), 0x60);
    EXPECT_EQ(ReadReg(1), 0x00);
}

TEST_F(Uart16550DeviceTest, DLAB_SwitchBackToNormal) {
    EnableDLAB();
    WriteReg(0, 0x60); // DLL
    DisableDLAB();
    // Now reg 0 is RBR/THR again
    WriteReg(0, 'A');
    ASSERT_EQ(transmitted.size(), 1u);
    EXPECT_EQ(transmitted[0], 'A');
}

TEST_F(Uart16550DeviceTest, DLAB_DefaultDivisor) {
    EnableDLAB();
    EXPECT_EQ(ReadReg(0), 0x01); // DLL default
    EXPECT_EQ(ReadReg(1), 0x00); // DLM default
}

// ============================================================================
// LCR (Line Control Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, LCR_WriteAndReadBack) {
    WriteReg(3, 0x1B); // 8N1 + break
    EXPECT_EQ(ReadReg(3), 0x1B);
}

// ============================================================================
// MCR (Modem Control Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, MCR_WriteAndReadBack) {
    WriteReg(4, 0x0B); // DTR + RTS + OUT1
    EXPECT_EQ(ReadReg(4), 0x0B);
}

// ============================================================================
// MSR (Modem Status Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, MSR_Default_CTSAndDSRAsserted) {
    uint8_t msr = ReadReg(6);
    EXPECT_NE(msr & 0x10, 0); // CTS
    EXPECT_NE(msr & 0x20, 0); // DSR
}

// ============================================================================
// Loopback mode
// ============================================================================

TEST_F(Uart16550DeviceTest, Loopback_THRFeedsBackToRBR) {
    WriteReg(4, 0x10); // MCR bit 4 = loopback
    WriteReg(0, 'L');
    // Should NOT call OnTransmit
    EXPECT_TRUE(transmitted.empty());
    // Should appear in RX FIFO
    uint8_t lsr = ReadReg(5);
    EXPECT_NE(lsr & 0x01, 0); // DR = 1
    EXPECT_EQ(ReadReg(0), 'L');
}

TEST_F(Uart16550DeviceTest, Loopback_MSR_ReflectsMCR) {
    WriteReg(4, 0x13); // Loopback + DTR + RTS
    uint8_t msr = ReadReg(6);
    EXPECT_NE(msr & 0x10, 0); // RTS -> CTS
    EXPECT_NE(msr & 0x20, 0); // DTR -> DSR
}

TEST_F(Uart16550DeviceTest, Loopback_MSR_NoDTR_NoDSR) {
    WriteReg(4, 0x10); // Loopback only, no DTR/RTS
    uint8_t msr = ReadReg(6);
    EXPECT_EQ(msr & 0x10, 0); // No CTS
    EXPECT_EQ(msr & 0x20, 0); // No DSR
}

TEST_F(Uart16550DeviceTest, Loopback_MultipleChars) {
    WriteReg(4, 0x10); // Loopback
    WriteReg(0, 'A');
    WriteReg(0, 'B');
    EXPECT_EQ(ReadReg(0), 'A');
    EXPECT_EQ(ReadReg(0), 'B');
}

// ============================================================================
// SCR (Scratch Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, SCR_WriteAndReadBack) {
    WriteReg(7, 0xA5);
    EXPECT_EQ(ReadReg(7), 0xA5);
    WriteReg(7, 0x5A);
    EXPECT_EQ(ReadReg(7), 0x5A);
}

// ============================================================================
// FCR (FIFO Control Register)
// ============================================================================

TEST_F(Uart16550DeviceTest, FCR_ClearRxFifo) {
    uart.ReceiveChar('A');
    uart.ReceiveChar('B');
    EXPECT_NE(ReadReg(5) & 0x01, 0); // DR = 1
    WriteReg(2, 0x02); // Clear RX FIFO
    EXPECT_EQ(ReadReg(5) & 0x01, 0); // DR = 0
}

// ============================================================================
// Read-only registers ignore writes
// ============================================================================

TEST_F(Uart16550DeviceTest, LSR_IgnoresWrites) {
    WriteReg(5, 0x00); // Try to clear LSR
    uint8_t lsr = ReadReg(5);
    EXPECT_NE(lsr & 0x20, 0); // THRE still set
}

TEST_F(Uart16550DeviceTest, MSR_IgnoresWrites) {
    WriteReg(6, 0x00); // Try to clear MSR
    uint8_t msr = ReadReg(6);
    EXPECT_NE(msr & 0x30, 0); // CTS+DSR still set
}

// ============================================================================
// Word / Long access
// ============================================================================

TEST_F(Uart16550DeviceTest, ReadWord_CombinesTwoBytes) {
    WriteReg(7, 0xAB); // SCR
    // ReadWord at Base+6 -> MSR(hi) | SCR(lo)
    uint16_t word = uart.ReadWord(Base + 6);
    EXPECT_EQ(word & 0xFF, 0xAB);
}

TEST_F(Uart16550DeviceTest, OutOfRange_ReturnsZero) {
    EXPECT_EQ(uart.ReadByte(Base + 8), 0);
}

} // namespace Em68030::Tests
