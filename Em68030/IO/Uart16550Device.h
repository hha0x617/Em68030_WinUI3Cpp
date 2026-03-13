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
#include <functional>
#include <mutex>
#include <queue>

#include "IMemoryMappedDevice.h"

namespace Em68030::IO {

/// Virtual 16550A UART for Linux console support on MVME147.
/// Memory-mapped at a configurable address, 8 bytes.
///
/// Register map (byte offsets, DLAB=0):
///   +0  RBR (read) / THR (write) - Receive Buffer / Transmit Holding
///   +1  IER - Interrupt Enable Register
///   +2  IIR (read) / FCR (write) - Interrupt ID / FIFO Control
///   +3  LCR - Line Control Register
///   +4  MCR - Modem Control Register
///   +5  LSR - Line Status Register (read-only)
///   +6  MSR - Modem Status Register (read-only)
///   +7  SCR - Scratch Register
///
/// Register map (DLAB=1, LCR bit 7 set):
///   +0  DLL - Divisor Latch Low
///   +1  DLM - Divisor Latch High
class Uart16550Device : public IMemoryMappedDevice {
public:
    explicit Uart16550Device(uint32_t baseAddress);

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    /// Called when a character is transmitted (THR write).
    std::function<void(uint8_t)> OnTransmit;

    /// Push a received character into the RX FIFO.
    void ReceiveChar(uint8_t ch);

    /// Interrupt output callback (active high).
    std::function<void(bool)> InterruptOutput;

private:
    uint32_t m_baseAddress;

    // LSR bit definitions
    static constexpr uint8_t LSR_DR   = 0x01; // Data Ready
    static constexpr uint8_t LSR_THRE = 0x20; // Transmit Holding Register Empty
    static constexpr uint8_t LSR_TEMT = 0x40; // Transmitter Empty

    // IER bit definitions
    static constexpr uint8_t IER_RDI  = 0x01; // Receive Data Available
    static constexpr uint8_t IER_THRI = 0x02; // Transmitter Holding Register Empty

    // IIR values
    static constexpr uint8_t IIR_NO_INT    = 0x01; // No interrupt pending
    static constexpr uint8_t IIR_RDI       = 0x04; // Receive Data Available
    static constexpr uint8_t IIR_THRI      = 0x02; // Transmitter Holding Register Empty
    static constexpr uint8_t IIR_FIFO_MASK = 0xC0; // FIFO enabled bits

    // RX FIFO (unbounded queue to avoid dropping characters on paste)
    std::queue<uint8_t> m_rxFifo;

    // Registers
    uint8_t m_ier = 0;
    uint8_t m_fcr = 0;
    uint8_t m_lcr = 0;
    uint8_t m_mcr = 0;
    uint8_t m_scr = 0;
    uint8_t m_dll = 0x01; // Default divisor = 1
    uint8_t m_dlm = 0;
    bool m_fifoEnabled = false;
    bool m_thrEmpty = true; // THR is empty (just transmitted)

    void UpdateInterrupt();
    uint8_t ComputeIIR() const;

    std::mutex m_rxMutex;
};

} // namespace Em68030::IO
