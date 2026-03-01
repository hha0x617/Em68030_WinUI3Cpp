#pragma once

#include <cstdint>
#include <array>
#include <string>

#include "IMemoryMappedDevice.h"

namespace Em68030::Core {
class MC68030;
}

namespace Em68030::IO {

/// PCC (Peripheral Channel Controller) for MVME147.
/// Central I/O controller managing timers and interrupt routing.
/// Mapped at $FFFE1000, 48 bytes ($00-$2F).
///
/// Register map (from NetBSD pccreg.h):
///   $00-$03  DMA Table Address (32-bit)
///   $04-$07  DMA Data Address (32-bit)
///   $08-$0B  DMA Byte Count (32-bit)
///   $0C-$0F  DMA Data Hold (32-bit)
///   $10-$11  Timer 1 Preload (16-bit)
///   $12-$13  Timer 1 Count (16-bit)
///   $14-$15  Timer 2 Preload (16-bit)
///   $16-$17  Timer 2 Count (16-bit)
///   $18      Timer 1 Interrupt Control
///   $19      Timer 1 Control
///   $1A      Timer 2 Interrupt Control
///   $1B      Timer 2 Control
///   $1C-$2F  Device ICR and control registers
///
/// Timer model (PCC_TIMERFREQ = 160,000 Hz):
///   Counter counts UP from preload value.
///   On 16-bit overflow (0xFFFF -> 0x0000), interrupt is generated
///   and counter is reloaded with the preload value.
///
/// All ICR bit layout:
///   Bit 7: INT (write 1 to clear = PCC_ICLEAR / PCC_TIMERACK = 0x80)
///   Bit 3: IEN (interrupt enable = PCC_IENABLE = 0x08)
///   Bits 2-0: IL (interrupt level = PCC_IMASK = 0x07)
class PccDevice : public IMemoryMappedDevice {
public:
    explicit PccDevice(Core::MC68030& cpu);

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    void Tick();

    /// Hardware reset: called when the CPU executes RESET instruction (0x4E70).
    /// Resets all PCC registers to power-on defaults.
    void HardwareReset();

    /// Called by external devices (SCC, SCSI, LANCE) to signal interrupt state.
    void SetDeviceInterrupt(const std::string& device, bool active);

    /// PCC DMA Data Address ($04-$07) as a 32-bit value.
    uint32_t GetDmaDataAddress() const;
    /// Update PCC DMA Data Address after a DMA transfer (auto-increment).
    void SetDmaDataAddress(uint32_t addr);
    /// PCC DMA Byte Count ($08-$0B), lower 24 bits.
    uint32_t GetDmaByteCount() const;
    /// Signal DMA completion (sets DMAC_CSR_DONE bit in DMA control).
    void SetDmaDone();

private:
    static constexpr uint32_t BaseAddress = 0xFFFE1000;

    // PCC interrupt vector offsets
    static constexpr int PCCV_ACFAIL  = 0;
    static constexpr int PCCV_BERR    = 1;
    static constexpr int PCCV_ABORT   = 2;
    static constexpr int PCCV_ZS      = 3;
    static constexpr int PCCV_LE      = 4;
    static constexpr int PCCV_SCSI    = 5;
    static constexpr int PCCV_DMA     = 6;
    static constexpr int PCCV_PRINTER = 7;
    static constexpr int PCCV_TIMER1  = 8;
    static constexpr int PCCV_TIMER2  = 9;
    static constexpr int PCCV_SOFT1   = 10;
    static constexpr int PCCV_SOFT2   = 11;

    void UpdateIPL();
    static void CheckIcr(uint8_t icr, int pccVector, int& maxLevel, int& maxVector);
    void WriteIcr(uint8_t& icr, uint8_t value);
    void WriteDeviceIcr(uint8_t& icr, uint8_t value, bool deviceActive);
    void WriteSoftIcr(uint8_t& icr, uint8_t value);
    void WriteTimerControl(uint8_t& control, uint32_t& count, uint16_t preload, uint8_t& overflowCount, uint8_t value);

    Core::MC68030& m_cpu;

    // DMA registers ($00-$0F)
    std::array<uint8_t, 16> m_dmaRegs{};

    // Timer registers (16-bit)
    uint16_t m_timer1Preload = 0;
    uint32_t m_timer1Count = 0;
    uint16_t m_timer2Preload = 0;
    uint32_t m_timer2Count = 0;

    // Timer ICR and Control registers
    uint8_t m_timer1Icr = 0;
    uint8_t m_timer1Control = 0;
    uint8_t m_timer2Icr = 0;
    uint8_t m_timer2Control = 0;

    // Timer overflow counters
    uint8_t m_timer1OverflowCount = 0;
    uint8_t m_timer2OverflowCount = 0;

    // Device ICR and control registers ($1C-$2F)
    uint8_t m_acFailIcr = 0;
    uint8_t m_wdogIcr = 0;
    uint8_t m_printerIcr = 0;
    uint8_t m_printerControl = 0;
    uint8_t m_dmaIcr = 0;
    uint8_t m_dmaControl = 0;
    uint8_t m_busErrIcr = 0;
    uint8_t m_dmaStatus = 0;
    uint8_t m_abortIcr = 0;
    uint8_t m_tableAddrFc = 0;
    uint8_t m_sccIcr = 0;
    uint8_t m_generalControl = 0;
    uint8_t m_lanceIcr = 0;
    uint8_t m_generalStatus = 0;
    uint8_t m_scsiIcr = 0;
    uint8_t m_slaveBaseAddr = 0;
    uint8_t m_soft1Icr = 0;
    uint8_t m_vectorBase = 0x40;
    uint8_t m_soft2Icr = 0;
    uint8_t m_revision = 0;

    // Level-sensitive device assertion state
    bool m_sccDeviceActive = false;
    bool m_scsiDeviceActive = false;
    bool m_lanceDeviceActive = false;
};

} // namespace Em68030::IO
