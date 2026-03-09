#include "pch.h"

#include "PccDevice.h"
#include "Wd33c93Device.h"
#include "../Core/MC68030.h"

namespace Em68030::IO {

PccDevice::PccDevice(Core::MC68030& cpu)
    : m_cpu(cpu)
{
    m_dmaRegs.fill(0);
}

void PccDevice::HardwareReset()
{
    // Stop timers and clear their state
    m_timer1Control = 0;
    m_timer2Control = 0;
    m_timer1Icr = 0;
    m_timer2Icr = 0;
    m_timer1OverflowCount = 0;
    m_timer2OverflowCount = 0;
    m_timer1Count = 0;
    m_timer2Count = 0;
    m_timer1Fractional = 0;
    m_timer2Fractional = 0;
    m_lastTimerTimestamp = std::chrono::steady_clock::now();

    // Clear all device ICRs
    m_acFailIcr = 0;
    m_wdogIcr = 0;
    m_printerIcr = 0;
    m_printerControl = 0;
    m_dmaIcr = 0;
    m_dmaControl = 0;
    m_busErrIcr = 0;
    m_dmaStatus = 0;
    m_abortIcr = 0;
    m_sccIcr = 0;
    m_generalControl = 0;
    m_lanceIcr = 0;
    m_scsiIcr = 0;
    m_soft1Icr = 0;
    m_soft2Icr = 0;

    // Clear device assertion state
    m_sccDeviceActive = false;
    m_scsiDeviceActive = false;
    m_lanceDeviceActive = false;

    // Update IPL — all ICRs cleared so this sets IPL to 0
    UpdateIPL();
}

void PccDevice::Tick()
{
    // Calculate elapsed wall-clock time and convert to 160 kHz timer ticks.
    // Real PCC timer runs at a fixed 160,000 Hz crystal, independent of CPU speed.
    auto now = std::chrono::steady_clock::now();
    auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_lastTimerTimestamp).count();
    m_lastTimerTimestamp = now;

    // Clamp: ignore negative or huge jumps (e.g., after pause/resume)
    if (elapsedNs <= 0) return;
    constexpr int64_t maxElapsedNs = 100'000'000; // 100ms max
    if (elapsedNs > maxElapsedNs) elapsedNs = maxElapsedNs;

    // Timer 1: count-up from preload, overflow at 0x10000
    if ((m_timer1Control & 0x04) != 0) // CEN
    {
        m_timer1Fractional += elapsedNs * TimerFreq;
        int ticks = static_cast<int>(m_timer1Fractional / 1'000'000'000LL);
        m_timer1Fractional -= static_cast<int64_t>(ticks) * 1'000'000'000LL;
        if (ticks > 0)
            AdvanceTimer(m_timer1Count, m_timer1Control, m_timer1Preload,
                m_timer1OverflowCount, m_timer1Icr, ticks);
    }

    // Timer 2: same model
    if ((m_timer2Control & 0x04) != 0)
    {
        m_timer2Fractional += elapsedNs * TimerFreq;
        int ticks = static_cast<int>(m_timer2Fractional / 1'000'000'000LL);
        m_timer2Fractional -= static_cast<int64_t>(ticks) * 1'000'000'000LL;
        if (ticks > 0)
            AdvanceTimer(m_timer2Count, m_timer2Control, m_timer2Preload,
                m_timer2OverflowCount, m_timer2Icr, ticks);
    }

    // Fire deferred SCSI interrupts (Level I SEL_ATN follow-up)
    if (m_scsiDevice)
        m_scsiDevice->Tick();
}

uint16_t PccDevice::GetCurrentTimerCount(uint32_t count, int64_t fractional, uint8_t control) const
{
    if ((control & 0x04) == 0) // CEN not set, timer stopped
        return static_cast<uint16_t>(count & 0xFFFF);

    auto now = std::chrono::steady_clock::now();
    auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_lastTimerTimestamp).count();
    if (elapsedNs <= 0) return static_cast<uint16_t>(count & 0xFFFF);

    int64_t totalFrac = fractional + elapsedNs * TimerFreq;
    int additionalTicks = static_cast<int>(totalFrac / 1'000'000'000LL);

    // Clamp: don't cross the 16-bit overflow boundary (0xFFFF).
    // Overflow handling (incrementing overflow count, setting ICR INT) is done
    // in Tick(). If we wrapped here, clock_pcc_getcount() would see a count
    // below preload while the overflow counter hasn't been incremented yet,
    // causing the monotonic clock to go backwards.
    int distToOverflow = 0x10000 - static_cast<int>(count & 0xFFFF);
    if (additionalTicks >= distToOverflow)
        additionalTicks = distToOverflow - 1; // clamp at 0xFFFF

    return static_cast<uint16_t>((count + static_cast<uint32_t>(additionalTicks)) & 0xFFFF);
}

void PccDevice::AdvanceTimer(uint32_t& count, uint8_t control, uint16_t preload,
    uint8_t& overflowCount, uint8_t& icr, int ticks)
{
    bool coc = (control & 0x02) != 0;
    int period = coc ? (0x10000 - preload) : 0x10000;
    if (period <= 0) period = 1;

    int distToOverflow = 0x10000 - static_cast<int>(count & 0xFFFF);

    if (ticks < distToOverflow)
    {
        count += static_cast<uint32_t>(ticks);
        return;
    }

    // At least one overflow
    ticks -= distToOverflow;
    int overflows = 1 + ticks / period;
    int remainder = ticks % period;
    count = static_cast<uint32_t>((coc ? preload : 0) + remainder);

    int newOvf = overflowCount + overflows;
    overflowCount = static_cast<uint8_t>(newOvf > 15 ? 15 : newOvf);
    icr |= 0x80; // Set INT pending
    UpdateIPL();
}

void PccDevice::SetDeviceInterrupt(const std::string& device, bool active)
{
    if (device == "scc") {
        m_sccDeviceActive = active;
        if (active) m_sccIcr |= 0x80; else m_sccIcr &= 0x7F;
    } else if (device == "scsi") {
        m_scsiDeviceActive = active;
        if (active) m_scsiIcr |= 0x80; else m_scsiIcr &= 0x7F;
    } else if (device == "lance") {
        m_lanceDeviceActive = active;
        if (active) m_lanceIcr |= 0x80; else m_lanceIcr &= 0x7F;
    }
    UpdateIPL();
    if (device == "scsi" && active) {
        // WD33C93 SAT commands complete synchronously during WriteByte,
        // but the Linux driver needs a few instructions after the command write
        // to set up hostdata->connected and hostdata->state before the ISR runs.
        // Real hardware takes milliseconds for SCSI selection/transfer.
        m_cpu.SuppressInterrupt(8);
    }
}

void PccDevice::UpdateIPL()
{
    int maxLevel = 0;
    int maxVector = 0;

    // Check all ICRs in priority order
    CheckIcr(m_acFailIcr,  PCCV_ACFAIL,  maxLevel, maxVector);
    CheckIcr(m_busErrIcr,  PCCV_BERR,    maxLevel, maxVector);
    CheckIcr(m_abortIcr,   PCCV_ABORT,   maxLevel, maxVector);
    CheckIcr(m_sccIcr,     PCCV_ZS,      maxLevel, maxVector);
    CheckIcr(m_lanceIcr,   PCCV_LE,      maxLevel, maxVector);
    CheckIcr(m_scsiIcr,    PCCV_SCSI,    maxLevel, maxVector);
    CheckIcr(m_dmaIcr,     PCCV_DMA,     maxLevel, maxVector);
    CheckIcr(m_printerIcr, PCCV_PRINTER, maxLevel, maxVector);
    CheckIcr(m_timer1Icr,  PCCV_TIMER1,  maxLevel, maxVector);
    CheckIcr(m_timer2Icr,  PCCV_TIMER2,  maxLevel, maxVector);
    CheckIcr(m_soft1Icr,   PCCV_SOFT1,   maxLevel, maxVector);
    CheckIcr(m_soft2Icr,   PCCV_SOFT2,   maxLevel, maxVector);

    m_cpu.SetIPL(maxLevel, maxLevel > 0 ? m_vectorBase + maxVector : -1);
}

void PccDevice::CheckIcr(uint8_t icr, int pccVector, int& maxLevel, int& maxVector)
{
    if ((icr & 0x88) == 0x88) // INT (bit 7) + IEN (bit 3)
    {
        int level = icr & 0x07;
        if (level == 0) level = 1;
        if (level > maxLevel)
        {
            maxLevel = level;
            maxVector = pccVector;
        }
    }
}

uint8_t PccDevice::ReadByte(uint32_t address)
{
    uint32_t offset = address - BaseAddress;
    if (offset < 0x10) return m_dmaRegs[offset];
    switch (offset) {
        case 0x10: return static_cast<uint8_t>(m_timer1Preload >> 8);
        case 0x11: return static_cast<uint8_t>(m_timer1Preload & 0xFF);
        case 0x12: return static_cast<uint8_t>((GetCurrentTimerCount(m_timer1Count, m_timer1Fractional, m_timer1Control) >> 8) & 0xFF);
        case 0x13: return static_cast<uint8_t>(GetCurrentTimerCount(m_timer1Count, m_timer1Fractional, m_timer1Control) & 0xFF);
        case 0x14: return static_cast<uint8_t>(m_timer2Preload >> 8);
        case 0x15: return static_cast<uint8_t>(m_timer2Preload & 0xFF);
        case 0x16: return static_cast<uint8_t>((GetCurrentTimerCount(m_timer2Count, m_timer2Fractional, m_timer2Control) >> 8) & 0xFF);
        case 0x17: return static_cast<uint8_t>(GetCurrentTimerCount(m_timer2Count, m_timer2Fractional, m_timer2Control) & 0xFF);
        case 0x18: return m_timer1Icr;
        case 0x19: return static_cast<uint8_t>(m_timer1Control | (m_timer1OverflowCount << 4));
        case 0x1A: return m_timer2Icr;
        case 0x1B: return static_cast<uint8_t>(m_timer2Control | (m_timer2OverflowCount << 4));
        case 0x1C: return m_acFailIcr;
        case 0x1D: return m_wdogIcr;
        case 0x1E: return m_printerIcr;
        case 0x1F: return m_printerControl;
        case 0x20: return m_dmaIcr;
        case 0x21: return m_dmaControl;
        case 0x22: return m_busErrIcr;
        case 0x23: return m_dmaStatus;
        case 0x24: return m_abortIcr;
        case 0x25: return m_tableAddrFc;
        case 0x26: return m_sccIcr;
        case 0x27: return m_generalControl;
        case 0x28: return m_lanceIcr;
        case 0x29: return m_generalStatus;
        case 0x2A: return m_scsiIcr;
        case 0x2B: return m_slaveBaseAddr;
        case 0x2C: return m_soft1Icr;
        case 0x2D: return m_vectorBase;
        case 0x2E: return m_soft2Icr;
        case 0x2F: return m_revision;
        case 0x30: return m_scsiIcr; // SCSI ICR alias (real HW offset; Linux uses this)
        default: return 0;
    }
}

uint16_t PccDevice::ReadWord(uint32_t address)
{
    uint32_t offset = address - BaseAddress;
    switch (offset) {
        case 0x10: return m_timer1Preload;
        case 0x12: return GetCurrentTimerCount(m_timer1Count, m_timer1Fractional, m_timer1Control);
        case 0x14: return m_timer2Preload;
        case 0x16: return GetCurrentTimerCount(m_timer2Count, m_timer2Fractional, m_timer2Control);
        default:
            return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
    }
}

uint32_t PccDevice::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void PccDevice::WriteByte(uint32_t address, uint8_t value)
{
    uint32_t offset = address - BaseAddress;
    if (offset < 0x10) {
        m_dmaRegs[offset] = value;
        return;
    }
    switch (offset) {
        // Timer preload/count
        case 0x10: m_timer1Preload = static_cast<uint16_t>((m_timer1Preload & 0x00FF) | (value << 8)); break;
        case 0x11: m_timer1Preload = static_cast<uint16_t>((m_timer1Preload & 0xFF00) | value); break;
        case 0x12: m_timer1Count = (m_timer1Count & 0x00FFu) | (static_cast<uint32_t>(value) << 8); break;
        case 0x13: m_timer1Count = (m_timer1Count & 0xFF00u) | value; break;
        case 0x14: m_timer2Preload = static_cast<uint16_t>((m_timer2Preload & 0x00FF) | (value << 8)); break;
        case 0x15: m_timer2Preload = static_cast<uint16_t>((m_timer2Preload & 0xFF00) | value); break;
        case 0x16: m_timer2Count = (m_timer2Count & 0x00FFu) | (static_cast<uint32_t>(value) << 8); break;
        case 0x17: m_timer2Count = (m_timer2Count & 0xFF00u) | value; break;

        // Timer ICR and Control
        case 0x18:
            WriteIcr(m_timer1Icr, value);
            break;
        case 0x19:
            WriteTimerControl(m_timer1Control, m_timer1Count, m_timer1Preload, m_timer1OverflowCount, value);
            break;
        case 0x1A: WriteIcr(m_timer2Icr, value); break;
        case 0x1B: WriteTimerControl(m_timer2Control, m_timer2Count, m_timer2Preload, m_timer2OverflowCount, value); break;

        // Device ICR and control registers
        case 0x1C: WriteIcr(m_acFailIcr, value); break;
        case 0x1D:
            if (value == 0xA5) {
                // Watchdog armed with ~100ms timeout — trigger immediate reset in emulation
                if (OnWatchdogReset)
                    OnWatchdogReset();
            } else {
                WriteIcr(m_wdogIcr, value);
            }
            break;
        case 0x1E: WriteIcr(m_printerIcr, value); break;
        case 0x1F: m_printerControl = value; break;
        case 0x20: WriteIcr(m_dmaIcr, value); break;
        case 0x21: m_dmaControl = value; break;
        case 0x22: WriteIcr(m_busErrIcr, value); break;
        case 0x23: m_dmaStatus = value; break;
        case 0x24: WriteIcr(m_abortIcr, value); break;
        case 0x25: m_tableAddrFc = value; break;
        case 0x26: WriteDeviceIcr(m_sccIcr, value, m_sccDeviceActive); break;
        case 0x27: m_generalControl = value; break;
        case 0x28: WriteDeviceIcr(m_lanceIcr, value, m_lanceDeviceActive); break;
        case 0x29: m_generalStatus = value; break;
        case 0x2A: WriteDeviceIcr(m_scsiIcr, value, m_scsiDeviceActive); break;
        case 0x2B: m_slaveBaseAddr = value; break;
        case 0x2C: WriteSoftIcr(m_soft1Icr, value); break;
        case 0x2D: m_vectorBase = value; break;
        case 0x2E: WriteSoftIcr(m_soft2Icr, value); break;
        case 0x2F: m_revision = value; break;
        case 0x30: WriteDeviceIcr(m_scsiIcr, value, m_scsiDeviceActive); break; // SCSI ICR alias (real HW offset; Linux uses this)
    }
}

void PccDevice::WriteWord(uint32_t address, uint16_t value)
{
    uint32_t offset = address - BaseAddress;
    switch (offset) {
        case 0x10: m_timer1Preload = value; break;
        case 0x12: m_timer1Count = value; break;
        case 0x14: m_timer2Preload = value; break;
        case 0x16: m_timer2Count = value; break;
        default:
            WriteByte(address, static_cast<uint8_t>(value >> 8));
            WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
            break;
    }
}

void PccDevice::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

void PccDevice::WriteIcr(uint8_t& icr, uint8_t value)
{
    uint8_t intBit = icr & 0x80; // Current INT state

    // PCC_ICLEAR / PCC_TIMERACK (bit 7): writing 1 clears INT
    if ((value & 0x80) != 0)
        intBit = 0;

    icr = static_cast<uint8_t>(intBit | (value & 0x0F));
    UpdateIPL();
}

void PccDevice::WriteDeviceIcr(uint8_t& icr, uint8_t value, bool deviceActive)
{
    uint8_t intBit = icr & 0x80;

    if ((value & 0x80) != 0)
        intBit = 0;

    // Level-sensitive: if device is still asserting, INT re-latches immediately
    if (deviceActive)
        intBit = 0x80;

    icr = static_cast<uint8_t>(intBit | (value & 0x0F));
    UpdateIPL();
}

void PccDevice::WriteSoftIcr(uint8_t& icr, uint8_t value)
{
    icr = static_cast<uint8_t>(value & 0x8F); // Store bit 7 as-is + bits 3-0
    UpdateIPL();
}

void PccDevice::WriteTimerControl(uint8_t& control, uint32_t& count, uint16_t preload, uint8_t& overflowCount, uint8_t value)
{
    // Bit 0 (CCI): when clear, reset counter to preload value
    if ((value & 0x01) == 0)
        count = preload;

    // Writing the control register clears the overflow counter
    overflowCount = 0;
    control = static_cast<uint8_t>(value & 0x07); // Only store bits 2:0
}

uint32_t PccDevice::GetDmaDataAddress() const
{
    return static_cast<uint32_t>(m_dmaRegs[4] << 24 | m_dmaRegs[5] << 16 | m_dmaRegs[6] << 8 | m_dmaRegs[7]);
}

void PccDevice::SetDmaDataAddress(uint32_t addr)
{
    m_dmaRegs[4] = static_cast<uint8_t>(addr >> 24);
    m_dmaRegs[5] = static_cast<uint8_t>(addr >> 16);
    m_dmaRegs[6] = static_cast<uint8_t>(addr >> 8);
    m_dmaRegs[7] = static_cast<uint8_t>(addr);
}

uint32_t PccDevice::GetDmaByteCount() const
{
    return static_cast<uint32_t>((m_dmaRegs[8] & 0x7F) << 24 | m_dmaRegs[9] << 16 | m_dmaRegs[10] << 8 | m_dmaRegs[11]);
}

void PccDevice::SetDmaDone()
{
    m_dmaControl |= 0x80;  // DMAC_CSR_DONE
    m_dmaIcr |= 0x80;      // DMA INT latch
    UpdateIPL();
}

} // namespace Em68030::IO
