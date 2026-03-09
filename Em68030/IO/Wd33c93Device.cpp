#include "pch.h"

#include "Wd33c93Device.h"
#include "PccDevice.h"
#include "../Core/Memory.h"
#include <algorithm>
#include <sstream>

namespace Em68030::IO {

Wd33c93Device::Wd33c93Device()
{
    m_regs.fill(0);
    m_regs[0x1F] = 0x00; // ASR: not busy, no interrupt
    m_targets.fill(nullptr);
    m_cdb.fill(0);
}

// --- External device attachment ---

void Wd33c93Device::AttachMemory(Core::Memory* memory) { m_memory = memory; }
void Wd33c93Device::AttachPcc(PccDevice* pcc) {
    m_pcc = pcc;
}

void Wd33c93Device::Tick()
{
    // Only fire deferred interrupt after the host has read the previous CSR
    // (INT cleared). Otherwise, the deferred CSR overwrites the unread one.
    if (m_deferredInterruptCsr != 0 && (m_regs[0x1F] & 0x80) == 0) {
        uint8_t csr = m_deferredInterruptCsr;
        m_deferredInterruptCsr = 0;
        SetCsrAndInterrupt(csr);
    }
}

void Wd33c93Device::AttachTarget(int scsiId, IScsiTarget* target)
{
    if (scsiId >= 0 && scsiId < 8)
        m_targets[scsiId] = target;
}

void Wd33c93Device::DetachTarget(int scsiId)
{
    if (scsiId >= 0 && scsiId < 8)
        m_targets[scsiId] = nullptr;
}

void Wd33c93Device::AttachDisk(int scsiId, ScsiDisk* disk)
{
    AttachTarget(scsiId, disk);
}

// --- Register access ---

uint8_t Wd33c93Device::ReadByte(uint32_t address)
{
    m_readCount++;
    uint32_t offset = address - BaseAddress;
    uint8_t val = 0;
    switch (offset) {
        case 0: val = GetAsr(); break;
        case 1: val = ReadDataPort(); break;
        default: break;
    }
    if (m_readCount <= 50 && DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] R off=" << offset << " addr=$" << std::hex << static_cast<int>(m_addressReg)
           << " val=$" << static_cast<int>(val) << " (#" << std::dec << m_readCount << ")";
        DiagLog(ss.str());
    }
    return val;
}

uint16_t Wd33c93Device::ReadWord(uint32_t address)
{
    return static_cast<uint16_t>((ReadByte(address) << 8) | ReadByte(address + 1));
}

uint32_t Wd33c93Device::ReadLong(uint32_t address)
{
    return (static_cast<uint32_t>(ReadWord(address)) << 16) | ReadWord(address + 2);
}

void Wd33c93Device::WriteByte(uint32_t address, uint8_t value)
{
    m_writeCount++;
    uint32_t offset = address - BaseAddress;
    if (m_writeCount <= 50 && DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] W off=" << offset << " val=$" << std::hex << static_cast<int>(value)
           << " addr=$" << static_cast<int>(m_addressReg) << " (#" << std::dec << m_writeCount << ")";
        DiagLog(ss.str());
    }
    switch (offset) {
        case 0:
            m_addressReg = value;
            break;
        case 1:
            WriteDataPort(value);
            break;
    }
}

void Wd33c93Device::WriteWord(uint32_t address, uint16_t value)
{
    WriteByte(address, static_cast<uint8_t>(value >> 8));
    WriteByte(address + 1, static_cast<uint8_t>(value & 0xFF));
}

void Wd33c93Device::WriteLong(uint32_t address, uint32_t value)
{
    WriteWord(address, static_cast<uint16_t>(value >> 16));
    WriteWord(address + 2, static_cast<uint16_t>(value & 0xFFFF));
}

// --- ASR (Auxiliary Status Register) ---

uint8_t Wd33c93Device::GetAsr()
{
    uint8_t asr = 0;
    if ((m_regs[0x1F] & 0x80) != 0) asr |= 0x80; // INT
    if (m_pioTransferActive || m_sbtPending) asr |= 0x01; // DBR (bit 0)
    return asr;
}

// --- Data port read ---

uint8_t Wd33c93Device::ReadDataPort()
{
    if (m_pioTransferActive) {
        return HandlePioDataRead();
    }

    uint8_t idx = m_addressReg & 0x1F;
    uint8_t val = m_regs[idx];

    // Auto-increment address register
    m_addressReg = (m_addressReg + 1) & 0x1F;

    if (idx == 0x17) {
        // Reading CSR clears INT and deasserts interrupt
        m_regs[0x1F] &= 0x7F;
        if (InterruptOutput)
            InterruptOutput(false);
    }

    // SBT input completion: driver has read the data byte from $19
    if (m_sbtPending && !m_sbtOutput && idx == 0x19) {
        CompleteSbtInput();
    }

    return val;
}

// --- Data port write ---

void Wd33c93Device::WriteDataPort(uint8_t value)
{
    if (m_pioTransferActive) {
        HandlePioDataWrite(value);
        return;
    }

    uint8_t reg = m_addressReg & 0x1F;
    m_regs[reg] = value;

    // Auto-increment address register
    m_addressReg = (m_addressReg + 1) & 0x1F;

    // SBT output completion: driver has written the data byte to $19
    if (m_sbtPending && m_sbtOutput && reg == 0x19) {
        CompleteSbtOutput(value);
        return;
    }

    if (reg == 0x18)
        HandleCommand(value);
}

// --- Transfer Count helpers ---

int Wd33c93Device::GetTransferCount()
{
    return (m_regs[0x12] << 16) | (m_regs[0x13] << 8) | m_regs[0x14];
}

void Wd33c93Device::SetTransferCount(int count)
{
    m_regs[0x12] = static_cast<uint8_t>((count >> 16) & 0xFF);
    m_regs[0x13] = static_cast<uint8_t>((count >> 8) & 0xFF);
    m_regs[0x14] = static_cast<uint8_t>(count & 0xFF);
}

// --- Interrupt signalling ---

void Wd33c93Device::SetCsrAndInterrupt(uint8_t csr)
{
    m_regs[0x17] = csr;
    m_regs[0x1F] |= 0x80; // ASR.INT = 1
    if (InterruptOutput)
        InterruptOutput(true);
}

// --- Command handler ---

void Wd33c93Device::HandleCommand(uint8_t cmd)
{
    // Cancel any deferred follow-up interrupt — the host is manually managing phases
    m_deferredInterruptCsr = 0;

    m_commandCount++;
    bool sbt = (cmd & 0x80) != 0;
    uint8_t baseCmd = cmd & 0x7F;

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] cmd=$" << std::hex << static_cast<int>(cmd)
           << " phase=" << static_cast<int>(m_phase)
           << " target=" << std::dec << (m_regs[0x15] & 0x07)
           << " sbt=" << sbt << " (#" << m_commandCount << ")";
        DiagLog(ss.str());
    }

    switch (baseCmd) {
        case 0x00: HandleReset(); break;
        case 0x01: HandleAbort(); break;
        case 0x04: HandleDisconnect(); break;
        case 0x06: HandleSelectAtn(); break;
        case 0x08: HandleSelAtnXfer(); break;
        case 0x20: HandleXferInfo(sbt); break;
        default:
            if (DiagLog) {
                std::ostringstream ss;
                ss << "[SCSI] Unknown cmd $" << std::hex << static_cast<int>(cmd) << ", returning SEL_TIMEO";
                DiagLog(ss.str());
            }
            SetCsrAndInterrupt(0x42);
            break;
    }
}

// --- ABORT / DISCONNECT ---

void Wd33c93Device::HandleAbort()
{
    m_phase = ScsiPhase::Idle;
    m_pioTransferActive = false;
    m_sbtPending = false;
    m_satInProgress = false;

    m_selectedTarget = -1;
    SetCsrAndInterrupt(0x41);
}

void Wd33c93Device::HandleDisconnect()
{
    m_phase = ScsiPhase::Idle;
    m_pioTransferActive = false;
    m_sbtPending = false;
    m_satInProgress = false;

    m_selectedTarget = -1;
    SetCsrAndInterrupt(0x41);
}

// --- RESET ---

void Wd33c93Device::HandleReset()
{
    m_phase = ScsiPhase::Idle;
    m_pioTransferActive = false;
    m_sbtPending = false;
    m_satInProgress = false;

    m_selectedTarget = -1;
    m_cdbOffset = 0;
    m_dataOffset = 0;
    m_dataLength = 0;
    SetCsrAndInterrupt(0x01);
}

void Wd33c93Device::ResetBusState()
{
    m_phase = ScsiPhase::Idle;
    m_pioTransferActive = false;
    m_sbtPending = false;
    m_satInProgress = false;
    m_selectedTarget = -1;
    m_cdbOffset = 0;
    m_dataOffset = 0;
    m_dataLength = 0;
    m_deferredInterruptCsr = 0;
    m_dataBuffer.clear();
    m_currentResult = {};
    // Clear INT flag in ASR without triggering interrupt callback
    m_regs[0x1F] &= ~0x80;
}

// --- SEL_ATN ---

void Wd33c93Device::HandleSelectAtn()
{
    int target = m_regs[0x15] & 0x07;

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] SEL_ATN target=" << target
           << " hasTarget=" << (m_targets[target] != nullptr)
           << " ready=" << (m_targets[target] ? m_targets[target]->IsReady() : false);
        DiagLog(ss.str());
    }

    if (m_targets[target] == nullptr || !m_targets[target]->IsReady()) {
        m_selectedTarget = -1;
        m_phase = ScsiPhase::Idle;
        SetCsrAndInterrupt(0x42); // SEL_TIMEO
    } else {
        m_selectedTarget = target;
        m_selectedLun = 0;
        m_phase = ScsiPhase::MsgOut;
        m_cdbOffset = 0;
        m_cdbLength = 0;
        SetCsrAndInterrupt(0x11); // CSR_SELECT — selection complete

        // Level I follow-up: after host reads CSR_SELECT (0x11) and returns
        // from ISR, fire CSR=0x8E (SRV_REQ|MSG_OUT) on next Tick() to signal
        // the target is requesting MSG_OUT phase for IDENTIFY message.
        // Cancelled if host issues a command (e.g. XFER_INFO) before Tick.
        m_deferredInterruptCsr = 0x8E;
    }
}

// --- SEL_ATN_XFER (Level II) ---
// Performs selection, IDENTIFY message, CDB, and optionally data transfer.
// For Level II (L2_BASIC), the chip interrupts with SRV_REQ when data
// transfer is needed, allowing the driver to set up DMA and issue XFER_INFO.
// After all phases complete, returns CSR=0x16 (SEL_XFER_DONE).

int Wd33c93Device::GetCdbLength(uint8_t opcode)
{
    switch (opcode >> 5) {
        case 0: return 6;   // Group 0
        case 1: return 10;  // Group 1
        case 2: return 10;  // Group 2
        case 5: return 12;  // Group 5
        default: return 10; // Default
    }
}

void Wd33c93Device::HandleSelAtnXfer()
{
    int target = m_regs[0x15] & 0x07;
    uint8_t cmdPhase = m_regs[0x10];

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] SEL_ATN_XFER target=" << target
           << " hasTarget=" << (m_targets[target] != nullptr)
           << " ready=" << (m_targets[target] ? m_targets[target]->IsReady() : false);
        DiagLog(ss.str());
    }

    if (m_targets[target] == nullptr || !m_targets[target]->IsReady()) {
        m_selectedTarget = -1;
        m_phase = ScsiPhase::Idle;
        SetCsrAndInterrupt(0x42); // SEL_TIMEO
        return;
    }

    // Command phase 0x45 = resume data transfer (scatter/gather continuation).
    // The driver re-issues SAT after a partial DMA segment completes.
    // Don't re-execute the CDB — just continue transferring from the existing buffer.
    if (cmdPhase == 0x45 && m_satInProgress &&
        (m_phase == ScsiPhase::DataIn || m_phase == ScsiPhase::DataOut) &&
        m_dataOffset < m_dataLength) {
        if (DiagLog) {
            std::ostringstream ss;
            ss << "[SCSI] SAT resume cmdPhase=$45 offset=" << m_dataOffset
               << " remaining=" << (m_dataLength - m_dataOffset);
            DiagLog(ss.str());
        }
        // Driver has set up PCC DMA and TC for the next segment — do the transfer.
        if (m_pcc && m_memory) {
            if (m_phase == ScsiPhase::DataIn)
                DoDmaDataIn();
            else
                DoDmaDataOut();
        } else {
            SetCsrAndInterrupt(m_phase == ScsiPhase::DataIn ? 0x89 : 0x88);
        }
        return;
    }

    // Command phase 0x50 = status phase (after L2_BASIC reads status byte).
    // Re-issue SAT to handle message-in and complete the command.
    if (cmdPhase == 0x50 && m_satInProgress) {
        if (DiagLog)
            DiagLog("[SCSI] SAT resume cmdPhase=$50 → CompleteSat");
        CompleteSat();
        return;
    }

    m_selectedTarget = target;
    m_selectedLun = m_regs[0x0F] & 0x07;

    // Read CDB from registers 0x03-0x0E
    uint8_t opcode = m_regs[0x03];
    m_cdbLength = GetCdbLength(opcode);
    for (int i = 0; i < m_cdbLength && i < static_cast<int>(m_cdb.size()); i++)
        m_cdb[i] = m_regs[0x03 + i];
    m_cdbOffset = m_cdbLength;

    if (m_scsiCmdLogCount < 200 && DiagLog) {
        m_scsiCmdLogCount++;
        std::ostringstream ss;
        ss << "[SCSI] SAT CDB[" << m_cdbLength << "]: " << FormatCdb()
           << " target=" << m_selectedTarget << " lun=" << m_selectedLun;
        DiagLog(ss.str());
    }

    // Execute the SCSI command
    auto result = m_targets[m_selectedTarget]->ProcessCommand(m_cdb.data(), m_cdbLength, m_selectedLun);
    m_currentResult = result;
    m_statusByte = result.StatusByte;
    m_satInProgress = true;

    if (result.HasDataIn) {
        m_dataBuffer = std::move(result.DataIn);
        m_dataOffset = 0;
        m_dataLength = result.DataInLength;
        m_phase = ScsiPhase::DataIn;

        if (m_scsiCmdLogCount <= 200 && DiagLog && (m_cdb[0] == 0x08 || m_cdb[0] == 0x28 || m_cdb[0] == 0x25 || m_cdb[0] == 0x12)) {
            LogDataBuffer("SAT DataIn", m_dataBuffer.data(), m_dataLength);
        }

        // The Linux driver sets up PCC DMA before issuing SAT.
        // If PCC/memory are available, do immediate DMA transfer.
        // Otherwise, signal SRV_REQ for the driver to set up transfer.
        if (m_pcc && m_memory)
            DoDmaDataIn();
        else
            SetCsrAndInterrupt(0x89); // SRV_REQ | DATA_IN (fallback)
    } else if (result.HasDataOut) {
        m_dataBuffer = result.DataOut.empty() ? std::vector<uint8_t>(result.DataOutLength, 0) : std::move(result.DataOut);
        m_dataOffset = 0;
        m_dataLength = result.DataOutLength;
        SaveWriteParams();
        m_phase = ScsiPhase::DataOut;

        if (m_pcc && m_memory)
            DoDmaDataOut();
        else
            SetCsrAndInterrupt(0x88); // SRV_REQ | DATA_OUT (fallback)
    } else {
        // No data phase — complete SAT immediately
        CompleteSat();
    }
}

// --- SAT completion ---

void Wd33c93Device::CompleteSat()
{
    m_satInProgress = false;
    m_phase = ScsiPhase::Idle;
    m_selectedTarget = -1;
    m_regs[0x0F] = m_statusByte;  // Status byte (read by driver)
    m_regs[0x19] = 0x00;          // Command Complete message
    m_regs[0x10] = 0x60;          // Command Phase: all phases done
    // TC already reflects remaining bytes (set by DoDmaDataIn/Out)
    SetCsrAndInterrupt(0x16);     // SEL_XFER_DONE
}

// --- XFER_INFO ---

void Wd33c93Device::HandleXferInfo(bool sbt)
{
    int tc = GetTransferCount();
    if (sbt) {
        tc = 1;
        SetTransferCount(1);
    }

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] XFER_INFO phase=" << static_cast<int>(m_phase)
           << " TC=" << tc << " ctrl=$" << std::hex << static_cast<int>(m_regs[0x01])
           << " sbt=" << std::dec << sbt;
        DiagLog(ss.str());
    }

    if (sbt) {
        HandleSbtTransfer();
        return;
    }

    switch (m_phase) {
        case ScsiPhase::MsgOut:
            StartPioTransfer(tc);
            break;
        case ScsiPhase::Command:
            StartPioTransfer(tc);
            break;
        case ScsiPhase::DataIn:
            if (IsDmaMode())
                DoDmaDataIn();
            else
                StartPioTransfer(tc);
            break;
        case ScsiPhase::DataOut:
            if (IsDmaMode())
                DoDmaDataOut();
            else
                StartPioTransfer(tc);
            break;
        case ScsiPhase::Status:
            m_dataBuffer = { m_statusByte };
            m_dataOffset = 0;
            m_dataLength = 1;
            StartPioTransfer(tc);
            break;
        case ScsiPhase::MsgIn:
            m_dataBuffer = { 0x00 };
            m_dataOffset = 0;
            m_dataLength = 1;
            StartPioTransfer(tc);
            break;
        default:
            if (DiagLog) {
                std::ostringstream ss;
                ss << "[SCSI] XFER_INFO unexpected phase " << static_cast<int>(m_phase);
                DiagLog(ss.str());
            }
            SetCsrAndInterrupt(0x42);
            break;
    }
}

// --- SBT handling ---

void Wd33c93Device::HandleSbtTransfer()
{
    switch (m_phase) {
        case ScsiPhase::MsgOut:
        case ScsiPhase::Command:
        case ScsiPhase::DataOut:
            m_sbtPending = true;
            m_sbtOutput = true;
            break;

        case ScsiPhase::DataIn:
            if (!m_dataBuffer.empty() && m_dataOffset < m_dataLength)
                m_regs[0x19] = m_dataBuffer[m_dataOffset++];
            else
                m_regs[0x19] = 0;
            m_sbtPending = true;
            m_sbtOutput = false;
            break;

        case ScsiPhase::Status:
            m_regs[0x19] = m_statusByte;
            if (DiagLog) {
                std::ostringstream ss;
                ss << "[SCSI] SBT Status: $" << std::hex << static_cast<int>(m_statusByte);
                DiagLog(ss.str());
            }
            m_sbtPending = true;
            m_sbtOutput = false;
            break;

        case ScsiPhase::MsgIn:
            m_regs[0x19] = 0x00;
            m_sbtPending = true;
            m_sbtOutput = false;
            break;

        default:
            if (DiagLog) {
                std::ostringstream ss;
                ss << "[SCSI] SBT unexpected phase " << static_cast<int>(m_phase);
                DiagLog(ss.str());
            }
            SetCsrAndInterrupt(0x42);
            break;
    }
}

void Wd33c93Device::CompleteSbtOutput(uint8_t value)
{
    m_sbtPending = false;

    switch (m_phase) {
        case ScsiPhase::MsgOut:
            if ((value & 0x80) != 0)
                m_selectedLun = value & 0x07;
            if (DiagLog) {
                std::ostringstream ss;
                ss << "[SCSI] SBT MsgOut: $" << std::hex << static_cast<int>(value)
                   << " (IDENTIFY lun=" << std::dec << m_selectedLun << ")";
                DiagLog(ss.str());
            }
            break;

        case ScsiPhase::Command:
            if (m_cdbOffset < static_cast<int>(m_cdb.size()))
                m_cdb[m_cdbOffset++] = value;
            break;

        case ScsiPhase::DataOut:
            if (!m_dataBuffer.empty() && m_dataOffset < m_dataLength)
                m_dataBuffer[m_dataOffset++] = value;
            break;

        default:
            break;
    }

    SetTransferCount(0);
    CompletePhaseTransfer();
}

void Wd33c93Device::CompleteSbtInput()
{
    m_sbtPending = false;
    SetTransferCount(0);
    CompletePhaseTransfer();
}

bool Wd33c93Device::IsDmaMode()
{
    return (m_regs[0x01] & 0x80) != 0;
}

// --- PIO Transfer ---

void Wd33c93Device::StartPioTransfer(int /*tc*/)
{
    m_pioTransferActive = true;
}

uint8_t Wd33c93Device::HandlePioDataRead()
{
    uint8_t val = 0;
    if (m_dataOffset < m_dataLength && !m_dataBuffer.empty()) {
        val = m_dataBuffer[m_dataOffset++];
    }
    DecrementTcAndCheck();
    return val;
}

void Wd33c93Device::HandlePioDataWrite(uint8_t value)
{
    switch (m_phase) {
        case ScsiPhase::MsgOut:
            if ((value & 0x80) != 0)
                m_selectedLun = value & 0x07;
            DecrementTcAndCheck();
            break;

        case ScsiPhase::Command:
            if (m_cdbOffset < static_cast<int>(m_cdb.size()))
                m_cdb[m_cdbOffset++] = value;
            DecrementTcAndCheck();
            break;

        case ScsiPhase::DataOut:
            if (!m_dataBuffer.empty() && m_dataOffset < m_dataLength)
                m_dataBuffer[m_dataOffset++] = value;
            DecrementTcAndCheck();
            break;

        default:
            DecrementTcAndCheck();
            break;
    }
}

void Wd33c93Device::DecrementTcAndCheck()
{
    int tc = GetTransferCount();
    tc--;
    if (tc < 0) tc = 0;
    SetTransferCount(tc);

    if (tc == 0) {
        m_pioTransferActive = false;
        CompletePhaseTransfer();
    }
}

// --- Phase completion ---

void Wd33c93Device::CompletePhaseTransfer()
{
    switch (m_phase) {
        case ScsiPhase::MsgOut:
            m_phase = ScsiPhase::Command;
            m_cdbOffset = 0;
            SetCsrAndInterrupt(0x1A); // XFER_DONE | COMMAND phase
            break;

        case ScsiPhase::Command:
            ExecuteScsiCommand();
            break;

        case ScsiPhase::DataIn:
            if (m_dataOffset < m_dataLength) {
                SetCsrAndInterrupt(m_satInProgress ? 0x89 : 0x19);
            } else if (m_satInProgress) {
                CompleteSat();
            } else {
                m_phase = ScsiPhase::Status;
                SetCsrAndInterrupt(0x1B); // XFER_DONE | STATUS
            }
            break;

        case ScsiPhase::DataOut:
            if (m_dataOffset < m_dataLength) {
                SetCsrAndInterrupt(m_satInProgress ? 0x88 : 0x18);
            } else {
                CompleteDataOut();
                if (m_satInProgress) {
                    CompleteSat();
                } else {
                    m_phase = ScsiPhase::Status;
                    SetCsrAndInterrupt(0x1B); // XFER_DONE | STATUS
                }
            }
            break;

        case ScsiPhase::Status:
            m_phase = ScsiPhase::MsgIn;
            m_dataBuffer = { 0x00 }; // COMMAND COMPLETE
            m_dataOffset = 0;
            m_dataLength = 1;
            SetCsrAndInterrupt(0x1F); // XFER_DONE | MSG_IN phase
            break;

        case ScsiPhase::MsgIn:
            m_phase = ScsiPhase::Idle;
            m_selectedTarget = -1;
            SetCsrAndInterrupt(0x41); // DISC
            break;

        default:
            break;
    }
}

// --- Execute SCSI command ---

void Wd33c93Device::ExecuteScsiCommand()
{
    m_cdbLength = m_cdbOffset;

    if (m_scsiCmdLogCount < 200 && DiagLog) {
        m_scsiCmdLogCount++;
        std::ostringstream ss;
        ss << "[SCSI] Exec CDB[" << m_cdbLength << "]: " << FormatCdb()
           << " target=" << m_selectedTarget << " lun=" << m_selectedLun;
        DiagLog(ss.str());
    }

    if (m_selectedTarget < 0 || m_targets[m_selectedTarget] == nullptr) {
        m_statusByte = 0x02;
        m_phase = ScsiPhase::Status;
        SetCsrAndInterrupt(0x1B); // XFER_DONE | STATUS
        return;
    }

    auto result = m_targets[m_selectedTarget]->ProcessCommand(m_cdb.data(), m_cdbLength, m_selectedLun);
    m_currentResult = result;
    m_statusByte = result.StatusByte;

    if (result.HasDataIn) {
        m_dataBuffer = std::move(result.DataIn);
        m_dataOffset = 0;
        m_dataLength = result.DataInLength;
        m_phase = ScsiPhase::DataIn;

        if (m_scsiCmdLogCount <= 200 && DiagLog && (m_cdb[0] == 0x08 || m_cdb[0] == 0x28 || m_cdb[0] == 0x25 || m_cdb[0] == 0x12)) {
            LogDataBuffer("DataIn", m_dataBuffer.data(), m_dataLength);
        }

        SetCsrAndInterrupt(0x19); // XFER_DONE | DATA_IN phase
    } else if (result.HasDataOut) {
        m_dataBuffer = result.DataOut.empty() ? std::vector<uint8_t>(result.DataOutLength, 0) : std::move(result.DataOut);
        m_dataOffset = 0;
        m_dataLength = result.DataOutLength;
        SaveWriteParams();
        m_phase = ScsiPhase::DataOut;
        SetCsrAndInterrupt(0x18); // XFER_DONE | DATA_OUT phase
    } else {
        m_phase = ScsiPhase::Status;
        SetCsrAndInterrupt(0x1B); // XFER_DONE | STATUS phase
    }
}

void Wd33c93Device::SaveWriteParams()
{
    uint8_t opcode = m_cdb[0];
    if (opcode == 0x0A) { // WRITE(6)
        m_writeLba = static_cast<uint32_t>((m_cdb[1] & 0x1F) << 16 | m_cdb[2] << 8 | m_cdb[3]);
        m_writeSectorCount = m_cdb[4];
        if (m_writeSectorCount == 0) m_writeSectorCount = 256;
    } else if (opcode == 0x2A) { // WRITE(10)
        m_writeLba = static_cast<uint32_t>(m_cdb[2] << 24 | m_cdb[3] << 16 | m_cdb[4] << 8 | m_cdb[5]);
        m_writeSectorCount = m_cdb[7] << 8 | m_cdb[8];
    }
}

void Wd33c93Device::CompleteDataOut()
{
    uint8_t opcode = m_cdb[0];
    if (opcode == 0x0A || opcode == 0x2A) { // WRITE(6) or WRITE(10)
        if (m_selectedTarget >= 0 && m_targets[m_selectedTarget] != nullptr && !m_dataBuffer.empty()) {
            m_targets[m_selectedTarget]->CompleteWrite(m_writeLba, m_dataBuffer.data(), m_dataLength);
        }
    }
}

// --- DMA transfers ---

void Wd33c93Device::DoDmaDataIn()
{
    if (!m_memory || !m_pcc) {
        if (DiagLog)
            DiagLog("[SCSI] DMA DataIn: no memory/pcc attached");
        SetCsrAndInterrupt(0x42);
        return;
    }

    uint32_t dmaAddr = m_pcc->GetDmaDataAddress();
    int tc = GetTransferCount();
    int transferLen = std::min(tc, m_dataLength - m_dataOffset);

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] DMA DataIn: addr=$" << std::hex << dmaAddr
           << " tc=" << std::dec << tc << " dataLen=" << m_dataLength << " xferLen=" << transferLen;
        DiagLog(ss.str());
    }

    for (int i = 0; i < transferLen; i++) {
        uint8_t b = (!m_dataBuffer.empty() && m_dataOffset < m_dataLength) ? m_dataBuffer[m_dataOffset++] : 0;
        m_memory->PokeByte(dmaAddr++, b);
    }

    if (m_scsiCmdLogCount <= 200 && transferLen > 0 && DiagLog) {
        uint32_t verifyAddr = dmaAddr - static_cast<uint32_t>(transferLen);
        std::ostringstream ss;
        int show = std::min(32, transferLen);
        for (int i = 0; i < show; i++) {
            ss << std::hex << static_cast<int>(m_memory->PeekByte(verifyAddr + static_cast<uint32_t>(i))) << " ";
        }
        std::ostringstream msg;
        msg << "[SCSI] DMA verify @$" << std::hex << verifyAddr << ": " << ss.str();
        DiagLog(msg.str());
    }

    SetTransferCount(tc - transferLen);
    m_pcc->SetDmaDataAddress(dmaAddr);
    m_pcc->SetDmaDone();

    if (m_dataOffset < m_dataLength) {
        // Partial transfer — SAT: SRV_REQ for next segment; SEL_ATN: MIS
        SetCsrAndInterrupt(m_satInProgress ? 0x89 : 0x19);
    } else if (m_satInProgress) {
        CompleteSat();
    } else {
        m_phase = ScsiPhase::Status;
        SetCsrAndInterrupt(0x1B); // XFER_DONE | STATUS
    }
}

void Wd33c93Device::DoDmaDataOut()
{
    if (!m_memory || !m_pcc) {
        if (DiagLog)
            DiagLog("[SCSI] DMA DataOut: no memory/pcc attached");
        SetCsrAndInterrupt(0x42);
        return;
    }

    uint32_t dmaAddr = m_pcc->GetDmaDataAddress();
    int tc = GetTransferCount();
    int transferLen = std::min(tc, m_dataLength - m_dataOffset);

    if (DiagLog) {
        std::ostringstream ss;
        ss << "[SCSI] DMA DataOut: addr=$" << std::hex << dmaAddr
           << " tc=" << std::dec << tc << " len=" << transferLen;
        DiagLog(ss.str());
    }

    for (int i = 0; i < transferLen; i++) {
        uint8_t b = m_memory->PeekByte(dmaAddr++);
        if (!m_dataBuffer.empty() && m_dataOffset < m_dataLength)
            m_dataBuffer[m_dataOffset++] = b;
    }

    SetTransferCount(tc - transferLen);
    m_pcc->SetDmaDataAddress(dmaAddr);
    m_pcc->SetDmaDone();

    if (m_dataOffset < m_dataLength) {
        SetCsrAndInterrupt(m_satInProgress ? 0x88 : 0x18);
    } else {
        CompleteDataOut();
        if (m_satInProgress) {
            CompleteSat();
        } else {
            m_phase = ScsiPhase::Status;
            SetCsrAndInterrupt(0x1B); // XFER_DONE | STATUS
        }
    }
}

// --- Diagnostics ---

std::string Wd33c93Device::FormatCdb()
{
    std::ostringstream ss;
    for (int i = 0; i < m_cdbLength; i++) {
        if (i > 0) ss << " ";
        ss << std::hex << static_cast<int>(m_cdb[i]);
    }
    return ss.str();
}

void Wd33c93Device::LogDataBuffer(const std::string& label, const uint8_t* data, int length)
{
    int show = std::min(32, length);
    std::ostringstream ss;
    for (int i = 0; i < show; i++) {
        ss << std::hex << static_cast<int>(data[i]) << " ";
    }
    std::ostringstream msg;
    msg << "[SCSI] " << label << "[" << std::dec << length << "]: " << ss.str();
    if (DiagLog)
        DiagLog(msg.str());
}

} // namespace Em68030::IO
