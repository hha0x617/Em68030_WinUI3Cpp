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
#include <array>
#include <memory>
#include <functional>

#include "IMemoryMappedDevice.h"
#include "INetworkHandler.h"

namespace Em68030::Core {
    class Memory;
}

namespace Em68030::IO {

/// AM7990 LANCE ethernet controller emulation for MVME147.
/// Phase 2: RX path + virtual network backend (ARP/ICMP/TCP/UDP echo).
/// Mapped at $FFFE1800, 4 bytes.
///
/// Registers (16-bit):
///   offset $0 = RDP (Register Data Port) -- read/write selected CSR
///   offset $2 = RAP (Register Address Port) -- selects CSR number
class LanceDevice : public IMemoryMappedDevice {
public:
    LanceDevice();

    void SetNetworkHandler(std::unique_ptr<INetworkHandler> handler);

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    void AttachMemory(Core::Memory* memory);
    void Tick();

    std::function<void(bool)> InterruptOutput;

    const std::array<uint8_t, 6>& GetMacAddress() const { return m_macAddress; }

private:
    static constexpr uint32_t BaseAddress = 0xFFFE1800;

    // CSR0 bit constants
    static constexpr uint16_t CSR0_ERR  = 0x8000;
    static constexpr uint16_t CSR0_BABL = 0x4000;
    static constexpr uint16_t CSR0_CERR = 0x2000;
    static constexpr uint16_t CSR0_MISS = 0x1000;
    static constexpr uint16_t CSR0_MERR = 0x0800;
    static constexpr uint16_t CSR0_RINT = 0x0400;
    static constexpr uint16_t CSR0_TINT = 0x0200;
    static constexpr uint16_t CSR0_IDON = 0x0100;
    static constexpr uint16_t CSR0_INTR = 0x0080;
    static constexpr uint16_t CSR0_INEA = 0x0040;
    static constexpr uint16_t CSR0_RXON = 0x0020;
    static constexpr uint16_t CSR0_TXON = 0x0010;
    static constexpr uint16_t CSR0_TDMD = 0x0008;
    static constexpr uint16_t CSR0_STOP = 0x0004;
    static constexpr uint16_t CSR0_STRT = 0x0002;
    static constexpr uint16_t CSR0_INIT = 0x0001;
    static constexpr uint16_t W1C_MASK  = 0x7F00; // BABL|CERR|MISS|MERR|RINT|TINT|IDON

    uint16_t ReadCsr(int csrNum);
    void WriteCsr(int csrNum, uint16_t value);
    void DoInit();
    void ProcessTxRing();
    void ProcessRxRing();
    void UpdateInterrupt();

    // Memory reference for DMA
    Core::Memory* m_memory = nullptr;

    uint16_t m_rap = 0;           // Selected CSR number
    std::array<uint16_t, 4> m_csr{};

    // Initialization block state
    uint16_t m_mode = 0;
    std::array<uint8_t, 6> m_macAddress{};
    uint32_t m_rxRingAddr = 0;
    uint32_t m_txRingAddr = 0;
    int m_rxRingLen = 0;
    int m_txRingLen = 0;

    // Chip state
    bool m_initialized = false;
    bool m_running = false;
    int m_txRingIndex = 0;
    bool m_txPending = false;
    int m_rxRingIndex = 0;
    std::unique_ptr<INetworkHandler> m_networkHandler;
};

} // namespace Em68030::IO
