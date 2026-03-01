#pragma once

#include <cstdint>
#include <array>
#include <queue>
#include <mutex>
#include <functional>

#include "IMemoryMappedDevice.h"

namespace Em68030::IO {

/// Z8530 SCC channel -- register model with RX FIFO, TX output, and interrupt support.
class Z8530Channel {
public:
    Z8530Channel();

    /// Hardware receive -- adds to RX FIFO and triggers interrupt evaluation.
    void ReceiveChar(uint8_t ch);

    /// Queue user console input (thread-safe).
    void QueueInput(uint8_t ch);

    uint8_t ReadControl();
    void WriteControl(uint8_t value);
    uint8_t ReadData();
    void WriteData(uint8_t value);

    /// Called periodically to simulate TX/RX serial timing.
    void Tick(bool cpuStopped);

    // Interrupt state queries
    bool IsTxIntPending() const { return m_txIntPending; }
    bool IsRxIntPending() const { return m_rxFifo.size() > 0; }
    bool IsTxInterruptEnabled() const { return (m_writeRegs[1] & 0x02) != 0; }
    bool IsRxInterruptEnabled() const { return (m_writeRegs[1] & 0x18) != 0; }
    bool IsRxEnabled() const { return (m_writeRegs[3] & 0x01) != 0; }
    bool IsTxEnabled() const { return (m_writeRegs[5] & 0x08) != 0; }
    bool IsTxInProgress() const { return m_txInProgress; }

    // Callbacks
    std::function<void(uint8_t)> CharTransmitted;
    std::function<void()> InterruptStateChanged;
    std::function<uint8_t()> GetRR3;

private:
    uint8_t m_registerPointer = 0;
    std::array<uint8_t, 16> m_writeRegs{};
    std::array<uint8_t, 16> m_readRegs{};
    std::queue<uint8_t> m_rxFifo;
    bool m_txIntPending = false;
    bool m_txInProgress = false;
    int m_txIdleTicks = 0;

    // Thread-safe staging queue for user console input
    std::queue<uint8_t> m_pendingInput;
    mutable std::mutex m_pendingInputMutex;

    bool HasPendingInput() const;
    bool TryDequeuePendingInput(uint8_t& out);
    bool IsPendingInputEmpty() const;
};

/// Z8530 SCC dual-channel serial controller for MVME147.
/// Channel A = console, Channel B = auxiliary.
/// Mapped at $FFFE3000, 4 bytes.
///
/// Address map:
///   $0 = Channel B Control
///   $1 = Channel B Data
///   $2 = Channel A Control
///   $3 = Channel A Data
class Z8530Device : public IMemoryMappedDevice {
public:
    Z8530Device();

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    /// Tick both channels for TX/RX simulation.
    void Tick(bool cpuStopped);

    Z8530Channel& GetChannelA() { return m_channelA; }
    Z8530Channel& GetChannelB() { return m_channelB; }

    std::function<void(bool)> InterruptOutput;

private:
    static constexpr uint32_t BaseAddress = 0xFFFE3000;

    void UpdateCompositeInterrupt();
    uint8_t ComputeRR3();

    Z8530Channel m_channelA;
    Z8530Channel m_channelB;
};

} // namespace Em68030::IO
