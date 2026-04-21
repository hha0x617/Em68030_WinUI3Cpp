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

#include "Uart16550Device.h"

namespace Em68030::IO {

Uart16550Device::Uart16550Device(uint32_t baseAddress)
    : m_baseAddress(baseAddress)
{
}

uint8_t Uart16550Device::ReadByte(uint32_t address)
{
    uint32_t reg = address - m_baseAddress;

    switch (reg) {
    case 0: // RBR or DLL
        if (m_lcr & 0x80) {
            return m_dll;
        } else {
            // Read from RX FIFO
            std::lock_guard lock(m_rxMutex);
            if (!m_rxFifo.empty()) {
                uint8_t ch = m_rxFifo.front();
                m_rxFifo.pop();
                UpdateInterrupt();
                return ch;
            }
            return 0;
        }

    case 1: // IER or DLM
        if (m_lcr & 0x80)
            return m_dlm;
        return m_ier;

    case 2: // IIR (read)
        return ComputeIIR();

    case 3: // LCR
        return m_lcr;

    case 4: // MCR
        return m_mcr;

    case 5: { // LSR
        uint8_t lsr = LSR_THRE | LSR_TEMT; // TX always ready
        {
            std::lock_guard lock(m_rxMutex);
            if (!m_rxFifo.empty())
                lsr |= LSR_DR;
        }
        return lsr;
    }

    case 6: // MSR - report CTS and DSR asserted (loopback aware)
        if (m_mcr & 0x10) { // Loopback mode
            uint8_t msr = 0;
            if (m_mcr & 0x02) msr |= 0x10; // RTS -> CTS
            if (m_mcr & 0x01) msr |= 0x20; // DTR -> DSR
            return msr;
        }
        return 0x30; // CTS + DSR asserted

    case 7: // SCR
        return m_scr;

    default:
        return 0;
    }
}

uint16_t Uart16550Device::ReadWord(uint32_t address)
{
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t Uart16550Device::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void Uart16550Device::WriteByte(uint32_t address, uint8_t value)
{
    uint32_t reg = address - m_baseAddress;

    switch (reg) {
    case 0: // THR or DLL
        if (m_lcr & 0x80) {
            m_dll = value;
        } else {
            if (m_mcr & 0x10) {
                // Loopback mode: feed THR data back to RX FIFO
                // (used by 8250 autoconf FIFO size detection)
                // Limit to 16 entries to match real 16550A FIFO size.
                std::lock_guard lock(m_rxMutex);
                if (m_rxFifo.size() < 16)
                    m_rxFifo.push(value);
            } else {
                // Normal transmit
                if (OnTransmit)
                    OnTransmit(value);
            }
            m_thrEmpty = true;
            UpdateInterrupt();
        }
        break;

    case 1: // IER or DLM
        if (m_lcr & 0x80) {
            m_dlm = value;
        } else {
            m_ier = value & 0x0F;
            UpdateInterrupt();
        }
        break;

    case 2: // FCR (write)
        m_fcr = value;
        m_fifoEnabled = (value & 0x01) != 0;
        if (value & 0x02) {
            // Clear RX FIFO
            std::lock_guard lock(m_rxMutex);
            std::queue<uint8_t>().swap(m_rxFifo);
        }
        // Bit 2: clear TX FIFO (no-op, we transmit immediately)
        break;

    case 3: // LCR
        m_lcr = value;
        break;

    case 4: // MCR
        m_mcr = value;
        break;

    case 5: // LSR (read-only, ignore writes)
        break;

    case 6: // MSR (read-only, ignore writes)
        break;

    case 7: // SCR
        m_scr = value;
        break;
    }
}

void Uart16550Device::WriteWord(uint32_t address, uint16_t value)
{
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void Uart16550Device::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

void Uart16550Device::ReceiveChar(uint8_t ch)
{
    {
        std::lock_guard lock(m_rxMutex);
        if (m_rxFifo.size() >= 64) return; // FIFO full — drop
        m_rxFifo.push(ch);
    }
    UpdateInterrupt();
}

size_t Uart16550Device::GetRxFifoFreeSpace()
{
    std::lock_guard lock(m_rxMutex);
    return m_rxFifo.size() >= 64 ? 0u : 64u - m_rxFifo.size();
}

uint8_t Uart16550Device::ComputeIIR() const
{
    uint8_t fifoFlag = m_fifoEnabled ? IIR_FIFO_MASK : 0;

    // Priority: RX data > TX empty
    if ((m_ier & IER_RDI) && !m_rxFifo.empty())
        return IIR_RDI | fifoFlag; // 0x04 = RX data available

    if ((m_ier & IER_THRI) && m_thrEmpty)
        return IIR_THRI | fifoFlag; // 0x02 = TX empty

    return IIR_NO_INT | fifoFlag; // 0x01 = no interrupt
}

void Uart16550Device::UpdateInterrupt()
{
    if (!InterruptOutput)
        return;

    uint8_t iir = ComputeIIR();
    bool active = !(iir & IIR_NO_INT); // bit 0 clear = interrupt pending
    InterruptOutput(active);
}

} // namespace Em68030::IO
