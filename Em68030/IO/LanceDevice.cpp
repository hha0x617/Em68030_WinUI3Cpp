#include "pch.h"

#include "LanceDevice.h"
#include "../Core/Memory.h"

namespace Em68030::IO {

LanceDevice::LanceDevice()
{
    m_csr.fill(0);
    m_csr[0] = CSR0_STOP;
}

void LanceDevice::AttachMemory(Core::Memory* memory)
{
    m_memory = memory;
}

void LanceDevice::Tick()
{
    if (m_txPending)
        ProcessTxRing();
    if (m_running && m_networkHandler.HasPendingPacket())
        ProcessRxRing();
}

uint8_t LanceDevice::ReadByte(uint32_t address)
{
    uint32_t offset = address - BaseAddress;
    uint16_t word = ReadWord(address & 0xFFFFFFFE);
    return (offset & 1) == 0 ? static_cast<uint8_t>(word >> 8) : static_cast<uint8_t>(word & 0xFF);
}

uint16_t LanceDevice::ReadWord(uint32_t address)
{
    uint32_t offset = address - BaseAddress;
    switch (offset) {
        case 0: return ReadCsr(m_rap & 0x03);
        case 2: return m_rap;
        default: return 0;
    }
}

uint32_t LanceDevice::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void LanceDevice::WriteByte(uint32_t address, uint8_t /*value*/)
{
    // LANCE is 16-bit only; byte writes are unusual but handle gracefully
}

void LanceDevice::WriteWord(uint32_t address, uint16_t value)
{
    uint32_t offset = address - BaseAddress;
    switch (offset) {
        case 0: // RDP -- write to selected CSR
            WriteCsr(m_rap & 0x03, value);
            break;
        case 2: // RAP
            m_rap = value;
            break;
    }
}

void LanceDevice::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

uint16_t LanceDevice::ReadCsr(int csrNum)
{
    if (csrNum != 0)
        return m_csr[csrNum];

    // CSR0: compute dynamic bits on top of stored bits
    uint16_t val = m_csr[0];

    // ERR (bit 15) = BABL | CERR | MISS | MERR
    if ((val & (CSR0_BABL | CSR0_CERR | CSR0_MISS | CSR0_MERR)) != 0)
        val |= CSR0_ERR;
    else
        val = static_cast<uint16_t>(val & ~CSR0_ERR);

    // RXON (bit 5) and TXON (bit 4) reflect running state
    if (m_running)
        val |= (CSR0_RXON | CSR0_TXON);
    else
        val = static_cast<uint16_t>(val & ~(CSR0_RXON | CSR0_TXON));

    // INTR (bit 7) = (any W1C bit set) AND INEA
    if (((val & W1C_MASK) != 0) && ((val & CSR0_INEA) != 0))
        val |= CSR0_INTR;
    else
        val = static_cast<uint16_t>(val & ~CSR0_INTR);

    return val;
}

void LanceDevice::WriteCsr(int csrNum, uint16_t value)
{
    if (csrNum != 0)
    {
        // CSR1/2/3: only writable in STOP state (AM7990 spec)
        if ((m_csr[0] & CSR0_STOP) != 0)
            m_csr[csrNum] = value;
        return;
    }

    // CSR0 write -- process in priority order

    // 1. STOP: full reset
    if ((value & CSR0_STOP) != 0)
    {
        m_csr[0] = CSR0_STOP;
        m_running = false;
        m_initialized = false;
        m_txPending = false;
        m_networkHandler.Reset();
        UpdateInterrupt();
        return;
    }

    // 2. W1C: clear status bits where write value has 1
    uint16_t w1cBits = static_cast<uint16_t>(value & W1C_MASK);
    m_csr[0] = static_cast<uint16_t>(m_csr[0] & ~w1cBits);

    // 3. INEA: set or clear from write value directly
    if ((value & CSR0_INEA) != 0)
        m_csr[0] |= CSR0_INEA;
    else
        m_csr[0] = static_cast<uint16_t>(m_csr[0] & ~CSR0_INEA);

    // 4. TDMD: trigger transmit demand
    if ((value & CSR0_TDMD) != 0)
        m_txPending = true;

    // 5. INIT: start initialization
    if ((value & CSR0_INIT) != 0)
        DoInit();

    // 6. STRT: start if initialized
    if ((value & CSR0_STRT) != 0)
    {
        if (m_initialized)
        {
            m_running = true;
            m_csr[0] = static_cast<uint16_t>(m_csr[0] & ~(CSR0_STOP | CSR0_INIT));
        }
    }

    UpdateInterrupt();
}

void LanceDevice::DoInit()
{
    if (m_memory == nullptr) return;

    // Build 24-bit physical address from CSR1 (low 16) + CSR2 (low 8)
    uint32_t iadr = static_cast<uint32_t>(m_csr[1] & 0xFFFF) |
                    (static_cast<uint32_t>(m_csr[2] & 0x00FF) << 16);

    // Read initialization block (24 bytes at offset 0x00-0x17)
    // +0x00: mode (16-bit)
    m_mode = m_memory->PeekWord(iadr + 0x00);

    // +0x02: padr[0..2] (3 x 16-bit) -> MAC address 6 bytes
    uint16_t padr0 = m_memory->PeekWord(iadr + 0x02);
    uint16_t padr1 = m_memory->PeekWord(iadr + 0x04);
    uint16_t padr2 = m_memory->PeekWord(iadr + 0x06);
    m_macAddress[0] = static_cast<uint8_t>(padr0 >> 8);
    m_macAddress[1] = static_cast<uint8_t>(padr0 & 0xFF);
    m_macAddress[2] = static_cast<uint8_t>(padr1 >> 8);
    m_macAddress[3] = static_cast<uint8_t>(padr1 & 0xFF);
    m_macAddress[4] = static_cast<uint8_t>(padr2 >> 8);
    m_macAddress[5] = static_cast<uint8_t>(padr2 & 0xFF);

    // +0x08: ladrf[0..3] (4 x 16-bit) -> multicast filter (not used in Phase 1)

    // +0x10: rdra (16-bit) + rlen|rhi (16-bit) -> RX ring address & size
    uint16_t rdraLo = m_memory->PeekWord(iadr + 0x10);
    uint16_t rlenRhi = m_memory->PeekWord(iadr + 0x12);
    m_rxRingAddr = static_cast<uint32_t>(rdraLo & 0xFFFF) |
                   (static_cast<uint32_t>(rlenRhi & 0x00FF) << 16);
    int rxLog2 = (rlenRhi >> 13) & 0x07;
    m_rxRingLen = 1 << rxLog2;

    // +0x14: tdra (16-bit) + tlen|thi (16-bit) -> TX ring address & size
    uint16_t tdraLo = m_memory->PeekWord(iadr + 0x14);
    uint16_t tlenThi = m_memory->PeekWord(iadr + 0x16);
    m_txRingAddr = static_cast<uint32_t>(tdraLo & 0xFFFF) |
                   (static_cast<uint32_t>(tlenThi & 0x00FF) << 16);
    int txLog2 = (tlenThi >> 13) & 0x07;
    m_txRingLen = 1 << txLog2;

    // Init successful: clear STOP, set IDON
    m_csr[0] = static_cast<uint16_t>(m_csr[0] & ~CSR0_STOP);
    m_csr[0] |= CSR0_IDON;
    m_initialized = true;
    m_txRingIndex = 0;
    m_rxRingIndex = 0;
    m_networkHandler.SetGuestMac(m_macAddress);
}

void LanceDevice::ProcessTxRing()
{
    if (m_memory == nullptr || !m_running) return;

    bool processed = false;

    while (true)
    {
        uint32_t descAddr = m_txRingAddr + static_cast<uint32_t>(m_txRingIndex * 8);

        uint16_t tmd0 = m_memory->PeekWord(descAddr);
        uint16_t tmd1 = m_memory->PeekWord(descAddr + 2);
        uint16_t tmd2 = m_memory->PeekWord(descAddr + 4);
        uint8_t tmd1Flags = static_cast<uint8_t>(tmd1 >> 8);

        // Check OWN bit
        if ((tmd1Flags & 0x80) == 0)
            break;

        // Extract buffer address and byte count
        uint32_t bufAddr = static_cast<uint32_t>(tmd0 & 0xFFFF) |
                           (static_cast<uint32_t>(tmd1 & 0xFF) << 16);
        int byteCount = -static_cast<int16_t>(tmd2 | 0xF000);

        // Read packet data from DMA
        if (byteCount > 0 && byteCount <= 1536)
        {
            std::vector<uint8_t> packet(byteCount);
            for (int i = 0; i < byteCount; i++)
                packet[i] = m_memory->PeekByte(bufAddr + static_cast<uint32_t>(i));

            // Check STP/ENP flags (single-buffer packet)
            bool stp = (tmd1Flags & 0x02) != 0;
            bool enp = (tmd1Flags & 0x01) != 0;
            if (stp && enp)
                m_networkHandler.ProcessPacket(packet.data(), byteCount);
        }

        // Clear OWN bit, write back tmd1 and clear tmd3
        tmd1Flags = static_cast<uint8_t>(tmd1Flags & ~0x80);
        uint16_t newTmd1 = static_cast<uint16_t>((tmd1Flags << 8) | (tmd1 & 0xFF));
        m_memory->PokeWord(descAddr + 2, newTmd1);
        m_memory->PokeWord(descAddr + 6, 0x0000);

        m_txRingIndex = (m_txRingIndex + 1) % m_txRingLen;
        processed = true;
    }

    m_txPending = false;

    if (processed)
    {
        m_csr[0] |= CSR0_TINT;
        UpdateInterrupt();
    }
}

void LanceDevice::ProcessRxRing()
{
    if (m_memory == nullptr || !m_running) return;

    while (m_networkHandler.HasPendingPacket())
    {
        uint32_t descAddr = m_rxRingAddr + static_cast<uint32_t>(m_rxRingIndex * 8);

        uint16_t rmd1 = m_memory->PeekWord(descAddr + 2);
        uint8_t rmd1Flags = static_cast<uint8_t>(rmd1 >> 8);

        // Check OWN bit -- must be 1 (LANCE owns = empty buffer)
        if ((rmd1Flags & 0x80) == 0)
        {
            m_csr[0] |= CSR0_MISS;
            UpdateInterrupt();
            return;
        }

        // Get buffer address and size
        uint16_t rmd0 = m_memory->PeekWord(descAddr);
        uint16_t rmd2 = m_memory->PeekWord(descAddr + 4);
        uint32_t bufAddr = static_cast<uint32_t>(rmd0 & 0xFFFF) |
                           (static_cast<uint32_t>(rmd1 & 0xFF) << 16);
        int bufSize = -static_cast<int16_t>(rmd2 | 0xF000);

        auto packet = m_networkHandler.DequeuePacket();

        if (static_cast<int>(packet.size()) > bufSize)
        {
            // Packet too large -- set ERR + BUFF, clear OWN
            uint8_t errFlags = 0x40 | 0x04; // ERR | BUFF
            uint16_t newRmd1 = static_cast<uint16_t>((errFlags << 8) | (rmd1 & 0xFF));
            m_memory->PokeWord(descAddr + 2, newRmd1);
            m_memory->PokeWord(descAddr + 6, 0);
        }
        else
        {
            // Write packet data via DMA
            for (size_t i = 0; i < packet.size(); i++)
                m_memory->PokeByte(bufAddr + static_cast<uint32_t>(i), packet[i]);

            // Write back RMD1: OWN=0, STP=1, ENP=1
            uint16_t newRmd1 = static_cast<uint16_t>(0x03 << 8 | (rmd1 & 0xFF));
            m_memory->PokeWord(descAddr + 2, newRmd1);

            // RMD3: message byte count (packet length + 4 for FCS)
            uint16_t rmd3 = static_cast<uint16_t>((packet.size() + 4) & 0x0FFF);
            m_memory->PokeWord(descAddr + 6, rmd3);
        }

        m_rxRingIndex = (m_rxRingIndex + 1) % m_rxRingLen;

        m_csr[0] |= CSR0_RINT;
        UpdateInterrupt();
    }
}

void LanceDevice::UpdateInterrupt()
{
    bool intr = ((m_csr[0] & W1C_MASK) != 0) && ((m_csr[0] & CSR0_INEA) != 0);
    if (InterruptOutput)
        InterruptOutput(intr);
}

} // namespace Em68030::IO
