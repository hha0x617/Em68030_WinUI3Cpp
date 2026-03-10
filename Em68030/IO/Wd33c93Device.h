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
#include <vector>
#include <string>
#include <functional>

#include "IMemoryMappedDevice.h"
#include "ScsiDisk.h"

namespace Em68030::Core {
class Memory;
}

namespace Em68030::IO {

class PccDevice;
class ScsiDisk;

/// WD33C93 SCSI controller emulation for MVME147.
/// Implements Level I protocol (phase-by-phase transfers) as expected by
/// the NetBSD sbic driver (sbic.c).
///
/// Mapped at $FFFE4000, 2 ports:
///   offset $0 write = Address Register (selects internal register)
///   offset $0 read  = Auxiliary Status Register (ASR)
///   offset $1 read/write = Data port (indirect register access, or PIO SCSI data)
class Wd33c93Device : public IMemoryMappedDevice {
public:
    Wd33c93Device();

    // IMemoryMappedDevice
    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadWord(uint32_t address) override;
    uint32_t ReadLong(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteWord(uint32_t address, uint16_t value) override;
    void WriteLong(uint32_t address, uint32_t value) override;

    // External device attachment
    void AttachMemory(Core::Memory* memory);
    void AttachPcc(PccDevice* pcc);
    void AttachTarget(int scsiId, IScsiTarget* target);
    void DetachTarget(int scsiId);
    void AttachDisk(int scsiId, ScsiDisk* disk); // backward-compatible alias

    /// Called periodically from PCC::Tick() to fire deferred interrupts.
    void Tick();

    /// Reset all SCSI bus state without triggering an interrupt.
    /// Used when hot-swapping SCSI devices via settings.
    void ResetBusState();

    // Diagnostics
    std::function<void(bool)> InterruptOutput;
    std::function<void(const std::string&)> DiagLog;
    int GetCommandCount() const { return m_commandCount; }
    int GetReadCount() const { return m_readCount; }
    int GetWriteCount() const { return m_writeCount; }

private:
    static constexpr uint32_t BaseAddress = 0xFFFE4000;

    enum class ScsiPhase { Idle, MsgOut, Command, DataIn, DataOut, Status, MsgIn };

    // Register access
    uint8_t GetAsr();
    uint8_t ReadDataPort();
    void WriteDataPort(uint8_t value);

    // Transfer count helpers
    int GetTransferCount();
    void SetTransferCount(int count);

    // Interrupt signalling
    void SetCsrAndInterrupt(uint8_t csr);

    // Command handler
    void HandleCommand(uint8_t cmd);
    void HandleReset();
    void HandleAbort();
    void HandleDisconnect();
    void HandleSelectAtn();
    void HandleSelAtnXfer();
    void CompleteSat();
    void HandleXferInfo(bool sbt = false);

    // SBT (Single Byte Transfer)
    void HandleSbtTransfer();
    void CompleteSbtOutput(uint8_t value);
    void CompleteSbtInput();

    bool IsDmaMode();

    // PIO Transfer
    void StartPioTransfer(int tc);
    uint8_t HandlePioDataRead();
    void HandlePioDataWrite(uint8_t value);
    void DecrementTcAndCheck();

    // Phase completion
    void CompletePhaseTransfer();

    // Execute SCSI command
    void ExecuteScsiCommand();
    void SaveWriteParams();
    void CompleteDataOut();

    // DMA transfers
    void DoDmaDataIn();
    void DoDmaDataOut();

    // Diagnostics
    std::string FormatCdb();
    void LogDataBuffer(const std::string& label, const uint8_t* data, int length);

    // Internal registers
    uint8_t m_addressReg = 0;
    std::array<uint8_t, 0x20> m_regs{};

    // External references
    Core::Memory* m_memory = nullptr;
    PccDevice* m_pcc = nullptr;
    std::array<IScsiTarget*, 8> m_targets{};

    // SCSI bus state machine
    ScsiPhase m_phase = ScsiPhase::Idle;
    bool m_pioTransferActive = false;
    bool m_satInProgress = false; // Select-and-Transfer (Level II) in progress

    // CDB length from SCSI opcode group
    static int GetCdbLength(uint8_t opcode);

    // SBT handshake state
    bool m_sbtPending = false;
    bool m_sbtOutput = false;

    // CDB accumulation
    std::array<uint8_t, 12> m_cdb{};
    int m_cdbLength = 0;
    int m_cdbOffset = 0;

    // Data transfer buffer
    std::vector<uint8_t> m_dataBuffer;
    int m_dataOffset = 0;
    int m_dataLength = 0;

    // SCSI result state
    uint8_t m_statusByte = 0;
    ScsiResult m_currentResult;
    int m_selectedTarget = -1;
    int m_selectedLun = 0;

    // Write command tracking
    uint32_t m_writeLba = 0;
    int m_writeSectorCount = 0;

    // Deferred interrupt: Level I SEL_ATN follow-up (CSR=0x8E after CSR=0x11)
    // Set in HandleSelectAtn, fired in Tick(), cancelled by new command execution.
    uint8_t m_deferredInterruptCsr = 0;

    // Diagnostic counters
    int m_commandCount = 0;
    int m_readCount = 0;
    int m_writeCount = 0;
    int m_scsiCmdLogCount = 0;
};

} // namespace Em68030::IO
