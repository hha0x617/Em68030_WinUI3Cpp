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

#include "Z8530Device.h"

namespace Em68030::IO {

// ============================================================================
// Z8530Channel
// ============================================================================

Z8530Channel::Z8530Channel()
{
    m_writeRegs.fill(0);
    m_readRegs.fill(0);
}

void Z8530Channel::ReceiveChar(uint8_t ch)
{
    m_rxFifo.push(ch);
    if (InterruptStateChanged)
        InterruptStateChanged();
}

void Z8530Channel::QueueInput(uint8_t ch)
{
    std::lock_guard<std::mutex> lock(m_pendingInputMutex);
    m_pendingInput.push(ch);
}

bool Z8530Channel::HasPendingInput() const
{
    std::lock_guard<std::mutex> lock(m_pendingInputMutex);
    return !m_pendingInput.empty();
}

bool Z8530Channel::TryDequeuePendingInput(uint8_t& out)
{
    std::lock_guard<std::mutex> lock(m_pendingInputMutex);
    if (m_pendingInput.empty())
        return false;
    out = m_pendingInput.front();
    m_pendingInput.pop();
    return true;
}

bool Z8530Channel::IsPendingInputEmpty() const
{
    std::lock_guard<std::mutex> lock(m_pendingInputMutex);
    return m_pendingInput.empty();
}

uint8_t Z8530Channel::ReadControl()
{
    uint8_t reg = m_registerPointer;
    m_registerPointer = 0; // Reset pointer after read

    switch (reg) {
        case 0:
            // RR0: Bit 0=RxAvail, Bit 2=TxEmpty, Bit 3=DCD, Bit 5=CTS
            return static_cast<uint8_t>(
                ((!m_rxFifo.empty() || HasPendingInput()) ? 0x01 : 0x00)
                | (m_txInProgress ? 0x00 : 0x04)
                | 0x08   // DCD always active
                | 0x20); // CTS always active
        case 1:
            return 0x01; // RR1: All Sent
        case 2:
            return m_readRegs[2]; // RR2: Interrupt vector
        case 3:
            return GetRR3 ? GetRR3() : 0; // RR3: Interrupt pending
        default:
            return m_readRegs[reg];
    }
}

void Z8530Channel::WriteControl(uint8_t value)
{
    if (m_registerPointer == 0) {
        uint8_t regSelect = value & 0x07;
        // WR0 command handling (bits 5-3)
        uint8_t cmd = (value >> 3) & 0x07;

        switch (cmd) {
            case 0: break; // Null command
            case 1: break; // Point High (not implemented — Linux uses 16550 UART)
            case 2: break; // Reset Ext/Status interrupts
            case 5: // Reset Tx interrupt pending
                m_txIntPending = false;
                if (InterruptStateChanged)
                    InterruptStateChanged();
                break;
            case 6: break; // Error reset
            case 7: // Reset highest IUS
                if (InterruptStateChanged)
                    InterruptStateChanged();
                break;
        }
        if (regSelect != 0) {
            m_registerPointer = regSelect;
            return;
        }
        m_writeRegs[0] = value;
    } else {
        uint8_t reg = m_registerPointer;
        m_registerPointer = 0;

        // WR8 = Transmit Buffer — treat as data write
        if (reg == 8) {
            WriteData(value);
            return;
        }

        m_writeRegs[reg] = value;

        // WR1 affects interrupt enables
        if (reg == 1) {
            if (IsTxInterruptEnabled() && !m_txInProgress && !m_txIntPending) {
                m_txIntPending = true;
            }
            if (InterruptStateChanged)
                InterruptStateChanged();
        }
    }
}

uint8_t Z8530Channel::ReadData()
{
    // Read from hardware RX FIFO first
    if (!m_rxFifo.empty()) {
        uint8_t ch = m_rxFifo.front();
        m_rxFifo.pop();
        if (m_rxFifo.empty()) {
            if (InterruptStateChanged)
                InterruptStateChanged();
        }
        return ch;
    }
    // Polled-mode fallback: read from user input staging queue
    uint8_t pending;
    if (TryDequeuePendingInput(pending))
        return pending;
    return 0;
}

void Z8530Channel::WriteData(uint8_t value)
{
    if (CharTransmitted)
        CharTransmitted(value);

    m_txInProgress = true;
    m_txIntPending = false;
    m_txIdleTicks = 0;
    if (InterruptStateChanged)
        InterruptStateChanged();
}

void Z8530Channel::Tick(bool cpuStopped)
{
    if (m_txInProgress) {
        m_txInProgress = false;
        m_txIntPending = true;
        m_txIdleTicks = 0;
        if (InterruptStateChanged)
            InterruptStateChanged();
    } else if (IsTxInterruptEnabled() && !m_txIntPending) {
        // TX prod: reassert TxIntPending periodically
        if (++m_txIdleTicks >= 16) {
            m_txIdleTicks = 0;
            m_txIntPending = true;
            if (InterruptStateChanged)
                InterruptStateChanged();
        }
    } else if (cpuStopped && m_txIntPending && !IsTxInterruptEnabled()) {
        // Force TIE on when CPU is stopped
        m_writeRegs[1] |= 0x02;
        m_txIdleTicks = 0;
        if (InterruptStateChanged)
            InterruptStateChanged();
    } else {
        m_txIdleTicks = 0;
    }

    // Promote pending user-input to the hardware RX FIFO when the kernel
    // has enabled RX interrupts (= interrupt-driven mode, not polled boot).
    // Previously this also required cpuStopped, but that made input
    // dependent on the CPU entering STOP, which is fragile.
    if (IsRxInterruptEnabled() && !IsPendingInputEmpty()) {
        uint8_t b;
        while (TryDequeuePendingInput(b))
            m_rxFifo.push(b);
        if (InterruptStateChanged)
            InterruptStateChanged();
    }
}

// ============================================================================
// Z8530Device
// ============================================================================

Z8530Device::Z8530Device()
{
    m_channelA.InterruptStateChanged = [this]() { UpdateCompositeInterrupt(); };
    m_channelB.InterruptStateChanged = [this]() { UpdateCompositeInterrupt(); };
    m_channelA.GetRR3 = [this]() -> uint8_t { return ComputeRR3(); };
}

void Z8530Device::Tick(bool cpuStopped)
{
    m_channelA.Tick(cpuStopped);
    m_channelB.Tick(cpuStopped);
}

void Z8530Device::UpdateCompositeInterrupt()
{
    bool active = (m_channelA.IsTxIntPending() && m_channelA.IsTxInterruptEnabled())
               || (m_channelA.IsRxIntPending() && m_channelA.IsRxInterruptEnabled())
               || (m_channelB.IsTxIntPending() && m_channelB.IsTxInterruptEnabled())
               || (m_channelB.IsRxIntPending() && m_channelB.IsRxInterruptEnabled());
    if (InterruptOutput)
        InterruptOutput(active);
}

uint8_t Z8530Device::ComputeRR3()
{
    uint8_t rr3 = 0;
    if (m_channelA.IsRxIntPending() && m_channelA.IsRxInterruptEnabled()) rr3 |= 0x20;
    if (m_channelA.IsTxIntPending() && m_channelA.IsTxInterruptEnabled()) rr3 |= 0x10;
    if (m_channelB.IsRxIntPending() && m_channelB.IsRxInterruptEnabled()) rr3 |= 0x04;
    if (m_channelB.IsTxIntPending() && m_channelB.IsTxInterruptEnabled()) rr3 |= 0x02;
    return rr3;
}

uint8_t Z8530Device::ReadByte(uint32_t address)
{
    uint32_t offset = address - BaseAddress;
    switch (offset) {
        case 0: return m_channelB.ReadControl();
        case 1: return m_channelB.ReadData();
        case 2: return m_channelA.ReadControl();
        case 3: return m_channelA.ReadData();
        default: return 0;
    }
}

uint16_t Z8530Device::ReadWord(uint32_t address)
{
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t Z8530Device::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void Z8530Device::WriteByte(uint32_t address, uint8_t value)
{
    uint32_t offset = address - BaseAddress;
    switch (offset) {
        case 0: m_channelB.WriteControl(value); break;
        case 1: m_channelB.WriteData(value); break;
        case 2: m_channelA.WriteControl(value); break;
        case 3: m_channelA.WriteData(value); break;
    }
}

void Z8530Device::WriteWord(uint32_t address, uint16_t value)
{
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void Z8530Device::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

} // namespace Em68030::IO
