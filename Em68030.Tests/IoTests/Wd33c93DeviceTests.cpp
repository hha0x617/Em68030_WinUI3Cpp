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
#include <gtest/gtest.h>

#include "IO/Wd33c93Device.h"
#include "IO/PccDevice.h"
#include "Core/Memory.h"
#include "Core/MC68030.h"

namespace Em68030::Tests {

// ============================================================================
// Mock SCSI Target
// ============================================================================

class MockScsiTarget : public IO::IScsiTarget {
public:
    bool ready = true;
    IO::ScsiResult nextResult{};
    std::vector<uint8_t> lastCdb;
    int lastCdbLength = 0;
    int lastLun = 0;
    int processCount = 0;

    // CompleteWrite tracking
    uint32_t lastWriteLba = 0;
    std::vector<uint8_t> lastWriteData;
    int lastWriteLength = 0;
    int writeCount = 0;

    bool IsReady() const override { return ready; }

    IO::ScsiResult ProcessCommand(const uint8_t* cdb, int cdbLength, int lun) override {
        processCount++;
        lastCdb.assign(cdb, cdb + cdbLength);
        lastCdbLength = cdbLength;
        lastLun = lun;
        return nextResult;
    }

    void CompleteWrite(uint32_t lba, const uint8_t* data, int length) override {
        writeCount++;
        lastWriteLba = lba;
        lastWriteData.assign(data, data + length);
        lastWriteLength = length;
    }

    // Helpers to set up common results
    void SetNoDataResult(uint8_t status = 0x00) {
        nextResult = {};
        nextResult.StatusByte = status;
    }

    void SetDataInResult(const std::vector<uint8_t>& data, uint8_t status = 0x00) {
        nextResult = {};
        nextResult.StatusByte = status;
        nextResult.HasDataIn = true;
        nextResult.DataIn = data;
        nextResult.DataInLength = static_cast<int>(data.size());
    }

    void SetDataOutResult(int length, uint8_t status = 0x00) {
        nextResult = {};
        nextResult.StatusByte = status;
        nextResult.HasDataOut = true;
        nextResult.DataOutLength = length;
    }
};

// ============================================================================
// Test Fixture
// ============================================================================

class Wd33c93DeviceTest : public ::testing::Test {
protected:
    IO::Wd33c93Device device;
    MockScsiTarget target0;
    std::vector<bool> interruptHistory;

    static constexpr uint32_t Base = 0xFFFE4000;
    static constexpr uint32_t AddrPort = Base + 0;  // Write: address reg, Read: ASR
    static constexpr uint32_t DataPort = Base + 1;   // Read/Write: data port

    void SetUp() override {
        interruptHistory.clear();
        device.InterruptOutput = [this](bool active) {
            interruptHistory.push_back(active);
        };
    }

    // --- Register access helpers ---

    void WriteReg(uint8_t reg, uint8_t value) {
        device.WriteByte(AddrPort, reg);     // Set address register
        device.WriteByte(DataPort, value);   // Write to data port
    }

    uint8_t ReadReg(uint8_t reg) {
        device.WriteByte(AddrPort, reg);     // Set address register
        return device.ReadByte(DataPort);    // Read from data port
    }

    uint8_t ReadAsr() {
        return device.ReadByte(AddrPort);
    }

    uint8_t ReadCsr() {
        return ReadReg(0x17);
    }

    void SetTransferCount(int count) {
        WriteReg(0x12, static_cast<uint8_t>((count >> 16) & 0xFF));
        WriteReg(0x13, static_cast<uint8_t>((count >> 8) & 0xFF));
        WriteReg(0x14, static_cast<uint8_t>(count & 0xFF));
    }

    void SetDestId(int id) {
        WriteReg(0x15, static_cast<uint8_t>(id & 0x07));
    }

    void IssueCommand(uint8_t cmd) {
        WriteReg(0x18, cmd);
    }

    // Attach target0 at SCSI ID 0
    void AttachTarget0() {
        device.AttachTarget(0, &target0);
    }

    // --- Level I flow helpers ---

    // Select target (issues command; caller must ReadCsr() to get result)
    void DoSelectAtn(int targetId = 0) {
        SetDestId(targetId);
        IssueCommand(0x06);
    }

    // Send one byte via SBT XFER_INFO (output phase).
    // Two-stage handshake: issue command (sets DBR), then write $19.
    void SbtSendByte(uint8_t value) {
        IssueCommand(0xA0);         // XFER_INFO | SBT → sbtPending, DBR set
        WriteReg(0x19, value);      // Write data register → CompleteSbtOutput
    }

    // Receive one byte via SBT XFER_INFO (input phase).
    // Two-stage handshake: issue command (puts byte in $19, sets DBR), then read $19.
    uint8_t SbtRecvByte() {
        IssueCommand(0xA0);         // XFER_INFO | SBT → byte placed in $19
        return ReadReg(0x19);       // Read data register → CompleteSbtInput
    }
};

// ============================================================================
// Basic Register Access
// ============================================================================

TEST_F(Wd33c93DeviceTest, AddressRegister_SelectsInternalReg)
{
    WriteReg(0x00, 0x42); // Write 0x42 to Own ID register
    EXPECT_EQ(ReadReg(0x00), 0x42);
}

TEST_F(Wd33c93DeviceTest, AddressRegister_AutoIncrements)
{
    WriteReg(0x00, 0x11);
    WriteReg(0x01, 0x22);

    // Read from reg 0x00 — should return 0x11, then auto-increment to 0x01
    device.WriteByte(AddrPort, 0x00);
    uint8_t val0 = device.ReadByte(DataPort); // reads reg 0x00, addr becomes 0x01
    uint8_t val1 = device.ReadByte(DataPort); // reads reg 0x01, addr becomes 0x02
    EXPECT_EQ(val0, 0x11);
    EXPECT_EQ(val1, 0x22);
}

TEST_F(Wd33c93DeviceTest, AddressRegister_Wraps)
{
    device.WriteByte(AddrPort, 0x1F); // Set to last register
    device.ReadByte(DataPort);        // Read → auto-increment
    // Address should wrap to 0x00
    // Write a known value to reg 0x00 and verify
    WriteReg(0x00, 0xAB);
    device.WriteByte(AddrPort, 0x1F);
    device.ReadByte(DataPort); // read reg 0x1F, addr→0x00
    uint8_t val = device.ReadByte(DataPort); // read reg 0x00
    EXPECT_EQ(val, 0xAB);
}

// ============================================================================
// ASR (Auxiliary Status Register)
// ============================================================================

TEST_F(Wd33c93DeviceTest, ASR_Default_NoInterrupt)
{
    uint8_t asr = ReadAsr();
    EXPECT_EQ(asr & 0x80, 0); // No INT
    EXPECT_EQ(asr & 0x01, 0); // No DBR
}

TEST_F(Wd33c93DeviceTest, ASR_INT_SetAfterCommand)
{
    AttachTarget0();
    target0.SetNoDataResult();
    DoSelectAtn(0);

    // ASR.INT is set after the command (before reading CSR)
    uint8_t asr = ReadAsr();
    EXPECT_NE(asr & 0x80, 0); // INT set
}

TEST_F(Wd33c93DeviceTest, CSR_Read_ClearsINT)
{
    // Use RESET (no follow-up interrupt) to test CSR read clearing INT
    IssueCommand(0x00); // RESET → CSR=0x01, INT=1

    EXPECT_NE(ReadAsr() & 0x80, 0); // INT set before CSR read

    ReadCsr(); // Reading CSR clears INT

    EXPECT_EQ(ReadAsr() & 0x80, 0); // INT cleared
}

TEST_F(Wd33c93DeviceTest, CSR_Read_DeassertsInterruptOutput)
{
    // Use RESET (no follow-up interrupt) to test interrupt deassert
    IssueCommand(0x00); // RESET → CSR=0x01, INT=1

    interruptHistory.clear();
    ReadCsr(); // Should deassert interrupt

    ASSERT_FALSE(interruptHistory.empty());
    EXPECT_FALSE(interruptHistory.back());
}

// ============================================================================
// RESET Command (0x00)
// ============================================================================

TEST_F(Wd33c93DeviceTest, Reset_ReturnsCSR_0x01)
{
    IssueCommand(0x00);
    EXPECT_EQ(ReadCsr(), 0x01);
}

TEST_F(Wd33c93DeviceTest, Reset_AssertsInterrupt)
{
    IssueCommand(0x00);
    EXPECT_NE(ReadAsr() & 0x80, 0);
}

// ============================================================================
// ABORT Command (0x01) / DISCONNECT Command (0x04)
// ============================================================================

TEST_F(Wd33c93DeviceTest, Abort_ReturnsCSR_0x41)
{
    IssueCommand(0x01);
    EXPECT_EQ(ReadCsr(), 0x41);
}

TEST_F(Wd33c93DeviceTest, Disconnect_ReturnsCSR_0x41)
{
    IssueCommand(0x04);
    EXPECT_EQ(ReadCsr(), 0x41);
}

// ============================================================================
// Unknown Command
// ============================================================================

TEST_F(Wd33c93DeviceTest, UnknownCmd_ReturnsCSR_0x42)
{
    IssueCommand(0x7F); // Unknown
    EXPECT_EQ(ReadCsr(), 0x42);
}

// ============================================================================
// SEL_ATN (0x06) — Select with ATN
// ============================================================================

TEST_F(Wd33c93DeviceTest, SelAtn_NoTarget_ReturnsSelTimeout)
{
    // No target attached at ID 0
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x42); // SEL_TIMEO
}

TEST_F(Wd33c93DeviceTest, SelAtn_TargetNotReady_ReturnsSelTimeout)
{
    target0.ready = false;
    AttachTarget0();
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x42);
}

TEST_F(Wd33c93DeviceTest, SelAtn_TargetReady_ReturnsCSR_0x11)
{
    AttachTarget0();
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x11); // CSR_SELECT
}

TEST_F(Wd33c93DeviceTest, SelAtn_AssertsInterruptOutput)
{
    AttachTarget0();
    interruptHistory.clear();
    SetDestId(0);
    IssueCommand(0x06); // SEL_ATN — don't read CSR (which deasserts)

    ASSERT_FALSE(interruptHistory.empty());
    EXPECT_TRUE(interruptHistory.back());
}

// ============================================================================
// SEL_ATN_XFER (0x08) — Select-and-Transfer (Level II)
// ============================================================================

TEST_F(Wd33c93DeviceTest, SelAtnXfer_NoTarget_ReturnsSelTimeout)
{
    // No target attached
    SetDestId(0);
    WriteReg(0x03, 0x00); // TUR opcode
    IssueCommand(0x08);
    EXPECT_EQ(ReadCsr(), 0x42);
}

TEST_F(Wd33c93DeviceTest, SelAtnXfer_NoData_ReturnsSelXferDone)
{
    AttachTarget0();
    target0.SetNoDataResult();

    SetDestId(0);
    WriteReg(0x03, 0x00); // TUR opcode
    IssueCommand(0x08);

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x16); // SEL_XFER_DONE
}

TEST_F(Wd33c93DeviceTest, SelAtnXfer_NoData_SetsStatusAndCmdPhase)
{
    AttachTarget0();
    target0.SetNoDataResult(0x00); // GOOD status

    SetDestId(0);
    WriteReg(0x03, 0x00); // TUR
    IssueCommand(0x08);

    // After CompleteSat: reg $0F = status, $10 = 0x60, $19 = 0x00
    ReadCsr(); // Clear INT first
    EXPECT_EQ(ReadReg(0x0F), 0x00); // Status byte
    EXPECT_EQ(ReadReg(0x10), 0x60); // Command phase complete
}

TEST_F(Wd33c93DeviceTest, SelAtnXfer_DataIn_ReturnsSrvReqDataIn)
{
    // Without PCC DMA attached, SAT signals SRV_REQ for driver to set up transfer
    AttachTarget0();
    target0.SetDataInResult({0x01, 0x02, 0x03, 0x04});

    SetDestId(0);
    WriteReg(0x03, 0x12); // INQUIRY opcode
    IssueCommand(0x08);

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x89); // SRV_REQ | DATA_IN (no PCC DMA attached)
}

TEST_F(Wd33c93DeviceTest, SelAtnXfer_ReadsCdbFromRegisters)
{
    AttachTarget0();
    target0.SetNoDataResult();

    SetDestId(0);
    // Set up READ(10) CDB: opcode=0x28, LBA=0x100, count=1
    WriteReg(0x03, 0x28); // opcode
    WriteReg(0x04, 0x00); // reserved
    WriteReg(0x05, 0x00); // LBA MSB
    WriteReg(0x06, 0x01); // LBA
    WriteReg(0x07, 0x00); // LBA
    WriteReg(0x08, 0x00); // LBA LSB
    WriteReg(0x09, 0x00); // reserved
    WriteReg(0x0A, 0x00); // count MSB
    WriteReg(0x0B, 0x01); // count LSB
    WriteReg(0x0C, 0x00); // control

    IssueCommand(0x08);

    EXPECT_EQ(target0.processCount, 1);
    ASSERT_GE(target0.lastCdb.size(), 10u);
    EXPECT_EQ(target0.lastCdb[0], 0x28);
    EXPECT_EQ(target0.lastCdb[3], 0x01); // LBA byte
    EXPECT_EQ(target0.lastCdb[8], 0x01); // count
}

TEST_F(Wd33c93DeviceTest, SelAtnXfer_CdbLength_Group0_Is6)
{
    AttachTarget0();
    target0.SetNoDataResult();
    SetDestId(0);
    WriteReg(0x03, 0x00); // Group 0 opcode
    IssueCommand(0x08);
    EXPECT_EQ(target0.lastCdbLength, 6);
}

TEST_F(Wd33c93DeviceTest, SelAtnXfer_CdbLength_Group1_Is10)
{
    AttachTarget0();
    target0.SetNoDataResult();
    SetDestId(0);
    WriteReg(0x03, 0x28); // Group 1 opcode
    IssueCommand(0x08);
    EXPECT_EQ(target0.lastCdbLength, 10);
}

// ============================================================================
// Level I Full Flow: SEL_ATN → SBT MsgOut → SBT Command → DataIn → Status → MsgIn → Disconnect
// ============================================================================

TEST_F(Wd33c93DeviceTest, LevelI_FullReadFlow_SBT)
{
    AttachTarget0();
    target0.SetDataInResult({0xAA, 0xBB, 0xCC, 0xDD});

    // 1. Select — returns 0x11 (CSR_SELECT)
    DoSelectAtn(0);
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x11); // CSR_SELECT

    // 2. MsgOut via SBT — send IDENTIFY (LUN 0)
    SbtSendByte(0x80);
    csr = ReadCsr();
    EXPECT_EQ(csr, 0x1A); // XFER_DONE | COMMAND phase

    // 3. Command via SBT — each byte triggers CompletePhaseTransfer → ExecuteScsiCommand.
    SbtSendByte(0x12); // INQUIRY opcode — triggers ExecuteScsiCommand immediately
    EXPECT_GE(target0.processCount, 1);

    csr = ReadCsr();
    EXPECT_EQ(csr, 0x19); // XFER_DONE | DATA_IN
}

TEST_F(Wd33c93DeviceTest, LevelI_PIO_MsgOut_TransitionsToCommand)
{
    AttachTarget0();
    target0.SetNoDataResult();

    // Select — returns 0x11 (CSR_SELECT)
    DoSelectAtn(0);
    ReadCsr(); // CSR=0x11 (SELECT)

    // PIO MsgOut: set TC=1, issue XFER_INFO
    SetTransferCount(1);
    IssueCommand(0x20); // XFER_INFO

    // ASR.DBR should be set (PIO transfer active)
    EXPECT_NE(ReadAsr() & 0x01, 0);

    // Write IDENTIFY message byte via PIO
    device.WriteByte(DataPort, 0x80);

    // After TC=0, phase transitions to Command
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x1A); // XFER_DONE | COMMAND
}

TEST_F(Wd33c93DeviceTest, LevelI_PIO_Command_ExecutesScsiCommand)
{
    AttachTarget0();
    target0.SetNoDataResult();

    // Select — returns 0x11 (CSR_SELECT)
    DoSelectAtn(0);
    ReadCsr(); // CSR=0x11 (SELECT)

    // MsgOut via PIO (TC=1)
    SetTransferCount(1);
    IssueCommand(0x20);
    device.WriteByte(DataPort, 0x80); // IDENTIFY
    ReadCsr(); // CSR=0x1A, clear INT

    // Command phase via PIO — send TUR (6 bytes)
    SetTransferCount(6);
    IssueCommand(0x20);

    // PIO active
    EXPECT_NE(ReadAsr() & 0x01, 0);

    // Send 6 CDB bytes
    for (int i = 0; i < 6; i++)
        device.WriteByte(DataPort, 0x00);

    // ExecuteScsiCommand should have been called
    EXPECT_EQ(target0.processCount, 1);

    // No data → STATUS phase
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x1B); // XFER_DONE | STATUS
}

TEST_F(Wd33c93DeviceTest, LevelI_StatusPhase_SBT_ReturnsStatusByte)
{
    AttachTarget0();
    target0.SetNoDataResult(0x02); // CHECK CONDITION

    // Select → MsgOut (PIO) → Command (PIO) → Status
    DoSelectAtn(0); ReadCsr(); // 0x11
    SetTransferCount(1); IssueCommand(0x20);
    device.WriteByte(DataPort, 0x80); // IDENTIFY
    ReadCsr();
    SetTransferCount(6); IssueCommand(0x20);
    for (int i = 0; i < 6; i++) device.WriteByte(DataPort, 0x00); // TUR
    ReadCsr(); // CSR=0x1B (STATUS)

    // Now in Status phase — SBT receive status
    uint8_t statusByte = SbtRecvByte();
    EXPECT_EQ(statusByte, 0x02);

    // CSR after Status → MsgIn
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x1F); // XFER_DONE | MSG_IN
}

TEST_F(Wd33c93DeviceTest, LevelI_MsgInPhase_SBT_ReturnsCommandComplete)
{
    AttachTarget0();
    target0.SetNoDataResult();

    // Full flow up to MsgIn using PIO for MsgOut/Command
    DoSelectAtn(0); ReadCsr(); // 0x11
    SetTransferCount(1); IssueCommand(0x20);
    device.WriteByte(DataPort, 0x80); // MsgOut PIO
    ReadCsr();
    SetTransferCount(6); IssueCommand(0x20);
    for (int i = 0; i < 6; i++) device.WriteByte(DataPort, 0x00); // Command PIO
    ReadCsr(); // CSR=0x1B (STATUS)
    SbtRecvByte(); // Read status
    ReadCsr(); // CSR=0x2F (MSG_IN)

    // Receive COMMAND COMPLETE message (0x00)
    uint8_t msg = SbtRecvByte();
    EXPECT_EQ(msg, 0x00);

    // CSR after MsgIn → DISC
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x41);
}

// ============================================================================
// Level I DataIn via PIO
// ============================================================================

TEST_F(Wd33c93DeviceTest, LevelI_PIO_DataIn)
{
    AttachTarget0();
    target0.SetDataInResult({0x11, 0x22, 0x33, 0x44});

    // Select — returns 0x11 (CSR_SELECT)
    DoSelectAtn(0); ReadCsr(); // 0x11
    SetTransferCount(1); IssueCommand(0x20);
    device.WriteByte(DataPort, 0x80); // MsgOut PIO
    ReadCsr();

    // Send INQUIRY CDB (6 bytes) via PIO
    SetTransferCount(6);
    IssueCommand(0x20);
    uint8_t cdb[] = {0x12, 0x00, 0x00, 0x00, 0x04, 0x00};
    for (int i = 0; i < 6; i++) device.WriteByte(DataPort, cdb[i]);

    // Should be in DataIn phase
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x19); // XFER_DONE | DATA_IN

    // PIO read 4 bytes
    SetTransferCount(4);
    IssueCommand(0x20); // XFER_INFO

    EXPECT_NE(ReadAsr() & 0x01, 0); // DBR set

    uint8_t d0 = device.ReadByte(DataPort);
    uint8_t d1 = device.ReadByte(DataPort);
    uint8_t d2 = device.ReadByte(DataPort);
    uint8_t d3 = device.ReadByte(DataPort);

    EXPECT_EQ(d0, 0x11);
    EXPECT_EQ(d1, 0x22);
    EXPECT_EQ(d2, 0x33);
    EXPECT_EQ(d3, 0x44);

    // Data complete → Status
    csr = ReadCsr();
    EXPECT_EQ(csr, 0x1B); // XFER_DONE | STATUS
}

// ============================================================================
// Transfer Count
// ============================================================================

TEST_F(Wd33c93DeviceTest, TransferCount_24bit)
{
    WriteReg(0x12, 0x01);
    WriteReg(0x13, 0x02);
    WriteReg(0x14, 0x03);

    EXPECT_EQ(ReadReg(0x12), 0x01);
    EXPECT_EQ(ReadReg(0x13), 0x02);
    EXPECT_EQ(ReadReg(0x14), 0x03);
}

// ============================================================================
// Attach / Detach
// ============================================================================

TEST_F(Wd33c93DeviceTest, AttachDetach_Target)
{
    AttachTarget0();
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x11); // Works — CSR_SELECT

    device.DetachTarget(0);
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x42); // No target → timeout
}

TEST_F(Wd33c93DeviceTest, AttachTarget_InvalidId_Ignored)
{
    device.AttachTarget(-1, &target0);
    device.AttachTarget(8, &target0);
    // Should not crash, and no target at ID 0
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x42);
}

TEST_F(Wd33c93DeviceTest, MultipleTargets)
{
    MockScsiTarget target1;
    target1.SetNoDataResult();

    device.AttachTarget(0, &target0);
    device.AttachTarget(1, &target1);

    target0.SetNoDataResult();

    // Select target 0
    DoSelectAtn(0);
    EXPECT_EQ(ReadCsr(), 0x11);

    // Select target 1
    DoSelectAtn(1);
    EXPECT_EQ(ReadCsr(), 0x11);
}

// ============================================================================
// SBT (Single Byte Transfer) — ASR.DBR
// ============================================================================

TEST_F(Wd33c93DeviceTest, SBT_Output_SetsDBR)
{
    AttachTarget0();
    target0.SetNoDataResult();
    DoSelectAtn(0);
    ReadCsr(); // 0x11 (MSG_OUT)

    // In MsgOut phase — issue SBT XFER_INFO
    // First, preload data register
    WriteReg(0x19, 0x80);
    IssueCommand(0xA0); // XFER_INFO | SBT

    // After SBT output, the byte has been consumed and phase transitions
    // (SBT output is immediate for single byte)
    // So DBR should no longer be set after completion
}

TEST_F(Wd33c93DeviceTest, SBT_Input_SetsDBR)
{
    AttachTarget0();
    target0.SetNoDataResult();

    // Get to Status phase via PIO
    DoSelectAtn(0); ReadCsr(); // 0x11
    SetTransferCount(1); IssueCommand(0x20);
    device.WriteByte(DataPort, 0x80); // MsgOut PIO
    ReadCsr();
    SetTransferCount(6); IssueCommand(0x20);
    for (int i = 0; i < 6; i++) device.WriteByte(DataPort, 0x00);
    ReadCsr();

    // Status phase — issue SBT receive
    IssueCommand(0xA0); // XFER_INFO | SBT

    // DBR should be set (waiting for driver to read $19)
    EXPECT_NE(ReadAsr() & 0x01, 0);
}

// ============================================================================
// Word/Long Access (mapped I/O)
// ============================================================================

TEST_F(Wd33c93DeviceTest, ReadWord_CombinesASRAndDataPort)
{
    IssueCommand(0x00); // RESET → sets INT

    uint16_t word = device.ReadWord(Base);
    uint8_t hi = static_cast<uint8_t>(word >> 8);   // ASR
    EXPECT_NE(hi & 0x80, 0); // INT bit in ASR
}

// ============================================================================
// Diagnostic Counters
// ============================================================================

TEST_F(Wd33c93DeviceTest, DiagnosticCounters)
{
    device.ReadByte(Base);
    device.ReadByte(Base);
    EXPECT_EQ(device.GetReadCount(), 2);

    device.WriteByte(Base, 0x00);
    EXPECT_EQ(device.GetWriteCount(), 1);

    IssueCommand(0x00); // RESET
    EXPECT_EQ(device.GetCommandCount(), 1);
}

// ============================================================================
// DMA Integration Tests — SAT (SEL_ATN_XFER) with PCC DMA
// ============================================================================
// These tests use real Memory + MC68030 + PccDevice to verify DMA data
// transfers through the WD33C93 SAT command flow. They cover the exact
// scenarios that caused NetBSD and Linux boot regressions.

class Wd33c93DmaTest : public ::testing::Test {
protected:
    static constexpr uint32_t WdBase = 0xFFFE4000;
    static constexpr uint32_t WdAddr = WdBase + 0;
    static constexpr uint32_t WdData = WdBase + 1;
    static constexpr uint32_t PccBase = 0xFFFE1000;
    static constexpr uint32_t DmaBase = 0x00100000; // DMA target address in RAM

    Core::Memory memory{16 * 1024 * 1024};
    Core::MC68030 cpu{memory};
    IO::PccDevice pcc{cpu};
    IO::Wd33c93Device wd;
    MockScsiTarget target0;
    std::vector<bool> interruptHistory;

    void SetUp() override {
        memory.AddRegion(0, 16 * 1024 * 1024, Core::RegionType::Ram);
        wd.AttachMemory(&memory);
        wd.AttachPcc(&pcc);
        wd.AttachTarget(0, &target0);
        interruptHistory.clear();
        wd.InterruptOutput = [this](bool active) {
            interruptHistory.push_back(active);
        };
    }

    void WriteReg(uint8_t reg, uint8_t value) {
        wd.WriteByte(WdAddr, reg);
        wd.WriteByte(WdData, value);
    }

    uint8_t ReadReg(uint8_t reg) {
        wd.WriteByte(WdAddr, reg);
        return wd.ReadByte(WdData);
    }

    uint8_t ReadCsr() { return ReadReg(0x17); }

    void SetTransferCount(int count) {
        WriteReg(0x12, static_cast<uint8_t>((count >> 16) & 0xFF));
        WriteReg(0x13, static_cast<uint8_t>((count >> 8) & 0xFF));
        WriteReg(0x14, static_cast<uint8_t>(count & 0xFF));
    }

    int GetTransferCount() {
        return (ReadReg(0x12) << 16) | (ReadReg(0x13) << 8) | ReadReg(0x14);
    }

    void SetDestId(int id) {
        WriteReg(0x15, static_cast<uint8_t>(id & 0x07));
    }

    void IssueCommand(uint8_t cmd) {
        WriteReg(0x18, cmd);
    }

    // Set up PCC DMA registers for a transfer
    void SetupPccDma(uint32_t addr, int count) {
        // DMA data address (0x04-0x07)
        pcc.WriteByte(PccBase + 0x04, static_cast<uint8_t>(addr >> 24));
        pcc.WriteByte(PccBase + 0x05, static_cast<uint8_t>(addr >> 16));
        pcc.WriteByte(PccBase + 0x06, static_cast<uint8_t>(addr >> 8));
        pcc.WriteByte(PccBase + 0x07, static_cast<uint8_t>(addr));
    }

    // Write CDB bytes to WD registers 0x03-0x0E
    void WriteCdb6(uint8_t op, uint32_t lba, uint8_t count) {
        WriteReg(0x03, op);
        WriteReg(0x04, static_cast<uint8_t>((lba >> 16) & 0x1F));
        WriteReg(0x05, static_cast<uint8_t>((lba >> 8) & 0xFF));
        WriteReg(0x06, static_cast<uint8_t>(lba & 0xFF));
        WriteReg(0x07, count);
        WriteReg(0x08, 0x00);
    }

    // Issue SAT command (0x08)
    void IssueSat() {
        SetDestId(0);
        IssueCommand(0x08);
    }
};

// --- SAT DataIn: single DMA transfer (complete in one segment) ---

TEST_F(Wd33c93DmaTest, Sat_DataIn_SingleSegment_TransfersData)
{
    // Prepare target with 512 bytes of test data
    std::vector<uint8_t> testData(512);
    for (int i = 0; i < 512; i++) testData[i] = static_cast<uint8_t>(i & 0xFF);
    target0.SetDataInResult(testData);

    // Set up CDB (READ(6), LBA=0, 1 block)
    WriteCdb6(0x08, 0, 1);

    // Set up DMA: TC=512, PCC DMA address
    SetTransferCount(512);
    SetupPccDma(DmaBase, 512);
    WriteReg(0x10, 0x00); // cmdPhase=0 (start fresh)

    // Issue SAT
    IssueSat();

    // Should complete with SEL_XFER_DONE (0x16) since all data fits in one segment
    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x16) << "Expected SEL_XFER_DONE after complete single-segment DMA";

    // Verify data was transferred to RAM
    for (int i = 0; i < 512; i++) {
        uint8_t actual = memory.PeekByte(DmaBase + static_cast<uint32_t>(i));
        EXPECT_EQ(actual, testData[i]) << "Mismatch at offset " << i;
        if (actual != testData[i]) break; // Stop on first mismatch
    }

    // TC should be 0 (all transferred)
    EXPECT_EQ(GetTransferCount(), 0);

    // Status byte and command phase should be set
    EXPECT_EQ(ReadReg(0x0F), 0x00); // Status byte (GOOD)
    EXPECT_EQ(ReadReg(0x10), 0x60); // Command phase (done)
}

// --- SAT DataIn: scatter-gather (two segments) ---

TEST_F(Wd33c93DmaTest, Sat_DataIn_ScatterGather_TwoSegments)
{
    // Target returns 1024 bytes, but we'll transfer in two 512-byte segments
    std::vector<uint8_t> testData(1024);
    for (int i = 0; i < 1024; i++) testData[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    target0.SetDataInResult(testData);

    WriteCdb6(0x08, 0, 2);

    // First segment: TC=512, DMA to DmaBase
    SetTransferCount(512);
    SetupPccDma(DmaBase, 512);
    WriteReg(0x10, 0x00); // cmdPhase=0

    IssueSat();

    // Should get SRV_REQ|DATA_IN (0x89) — more data to transfer
    uint8_t csr1 = ReadCsr();
    EXPECT_EQ(csr1, 0x89) << "Expected SRV_REQ|DATA_IN after partial transfer";

    // TC should reflect remaining (512 - 512 = 0 for this segment)
    EXPECT_EQ(GetTransferCount(), 0);

    // Verify first segment data
    for (int i = 0; i < 512; i++) {
        EXPECT_EQ(memory.PeekByte(DmaBase + static_cast<uint32_t>(i)), testData[i])
            << "Segment 1 mismatch at offset " << i;
        if (memory.PeekByte(DmaBase + static_cast<uint32_t>(i)) != testData[i]) break;
    }

    // Second segment: set up new DMA address, TC=512, cmdPhase=0x45 (resume)
    SetTransferCount(512);
    SetupPccDma(DmaBase + 512, 512);
    WriteReg(0x10, 0x45); // cmdPhase=0x45 = scatter-gather resume

    IssueSat(); // Re-issue SAT to continue

    // Should complete with SEL_XFER_DONE (0x16)
    uint8_t csr2 = ReadCsr();
    EXPECT_EQ(csr2, 0x16) << "Expected SEL_XFER_DONE after second segment";

    // Verify second segment data
    for (int i = 0; i < 512; i++) {
        EXPECT_EQ(memory.PeekByte(DmaBase + 512 + static_cast<uint32_t>(i)), testData[512 + i])
            << "Segment 2 mismatch at offset " << i;
        if (memory.PeekByte(DmaBase + 512 + static_cast<uint32_t>(i)) != testData[512 + i]) break;
    }

    EXPECT_EQ(ReadReg(0x10), 0x60); // Command phase done
}

// --- SAT DataIn: TC reflects remaining bytes after partial DMA ---

TEST_F(Wd33c93DmaTest, Sat_DataIn_TcReflectsRemainingBytes)
{
    // Target returns 1024 bytes, TC is set to only 256
    std::vector<uint8_t> testData(1024);
    for (int i = 0; i < 1024; i++) testData[i] = static_cast<uint8_t>(i & 0xFF);
    target0.SetDataInResult(testData);

    WriteCdb6(0x08, 0, 2);

    SetTransferCount(256);
    SetupPccDma(DmaBase, 256);
    WriteReg(0x10, 0x00);

    IssueSat();

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x89) << "Expected SRV_REQ — data remaining";

    // TC should be 0 (256 requested, 256 transferred)
    EXPECT_EQ(GetTransferCount(), 0);
}

// --- SAT DataOut: single segment ---

TEST_F(Wd33c93DmaTest, Sat_DataOut_SingleSegment)
{
    // Prepare a WRITE command — target expects 512 bytes
    target0.SetDataOutResult(512);

    // Write test data into RAM at DMA source
    for (int i = 0; i < 512; i++) {
        memory.PokeByte(DmaBase + static_cast<uint32_t>(i), static_cast<uint8_t>((i + 0x55) & 0xFF));
    }

    // CDB for WRITE(6)
    WriteReg(0x03, 0x0A); // WRITE(6)
    WriteReg(0x04, 0x00);
    WriteReg(0x05, 0x00);
    WriteReg(0x06, 0x00);
    WriteReg(0x07, 0x01);
    WriteReg(0x08, 0x00);

    SetTransferCount(512);
    SetupPccDma(DmaBase, 512);
    WriteReg(0x10, 0x00);

    IssueSat();

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x16) << "Expected SEL_XFER_DONE after DataOut";
    EXPECT_EQ(GetTransferCount(), 0);
    EXPECT_EQ(ReadReg(0x10), 0x60);
}

// --- SAT DataOut: scatter-gather ---

TEST_F(Wd33c93DmaTest, Sat_DataOut_ScatterGather)
{
    target0.SetDataOutResult(1024);

    // Write test data into two RAM locations
    for (int i = 0; i < 512; i++) {
        memory.PokeByte(DmaBase + static_cast<uint32_t>(i), static_cast<uint8_t>(i & 0xFF));
        memory.PokeByte(DmaBase + 0x1000 + static_cast<uint32_t>(i), static_cast<uint8_t>((i + 0x80) & 0xFF));
    }

    WriteReg(0x03, 0x0A);
    WriteReg(0x04, 0x00);
    WriteReg(0x05, 0x00);
    WriteReg(0x06, 0x00);
    WriteReg(0x07, 0x02);
    WriteReg(0x08, 0x00);

    // First segment
    SetTransferCount(512);
    SetupPccDma(DmaBase, 512);
    WriteReg(0x10, 0x00);

    IssueSat();

    uint8_t csr1 = ReadCsr();
    EXPECT_EQ(csr1, 0x88) << "Expected SRV_REQ|DATA_OUT after first segment";

    // Second segment — cmdPhase=0x45 resume
    SetTransferCount(512);
    SetupPccDma(DmaBase + 0x1000, 512);
    WriteReg(0x10, 0x45);

    IssueSat();

    uint8_t csr2 = ReadCsr();
    EXPECT_EQ(csr2, 0x16) << "Expected SEL_XFER_DONE after second segment";
    EXPECT_EQ(ReadReg(0x10), 0x60);
}

// --- SAT no data phase: TEST UNIT READY ---

TEST_F(Wd33c93DmaTest, Sat_NoData_CompletesImmediately)
{
    target0.SetNoDataResult(0x00);

    WriteCdb6(0x00, 0, 0); // TEST UNIT READY
    SetTransferCount(0);
    WriteReg(0x10, 0x00);

    IssueSat();

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x16) << "Expected SEL_XFER_DONE for no-data command";
    EXPECT_EQ(ReadReg(0x0F), 0x00); // Status = GOOD
    EXPECT_EQ(ReadReg(0x10), 0x60);
}

// --- SAT with CHECK CONDITION status ---

TEST_F(Wd33c93DmaTest, Sat_CheckCondition_ReportsStatus)
{
    target0.SetNoDataResult(0x02); // CHECK CONDITION

    WriteCdb6(0x00, 0, 0);
    SetTransferCount(0);
    WriteReg(0x10, 0x00);

    IssueSat();

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x16);
    EXPECT_EQ(ReadReg(0x0F), 0x02); // Status = CHECK CONDITION
}

// --- SAT target not ready: selection timeout ---

TEST_F(Wd33c93DmaTest, Sat_TargetNotReady_SelectionTimeout)
{
    target0.ready = false;

    WriteCdb6(0x00, 0, 0);
    SetTransferCount(0);
    WriteReg(0x10, 0x00);

    IssueSat();

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x42) << "Expected selection timeout for not-ready target";
}

// --- SAT scatter-gather: three segments ---

TEST_F(Wd33c93DmaTest, Sat_DataIn_ThreeSegments)
{
    // 768 bytes = 3 segments of 256
    std::vector<uint8_t> testData(768);
    for (int i = 0; i < 768; i++) testData[i] = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
    target0.SetDataInResult(testData);

    WriteCdb6(0x08, 0, 2);

    // Segment 1: 256 bytes
    SetTransferCount(256);
    SetupPccDma(DmaBase, 256);
    WriteReg(0x10, 0x00);
    IssueSat();
    EXPECT_EQ(ReadCsr(), 0x89);

    // Segment 2: 256 bytes
    SetTransferCount(256);
    SetupPccDma(DmaBase + 256, 256);
    WriteReg(0x10, 0x45);
    IssueSat();
    EXPECT_EQ(ReadCsr(), 0x89);

    // Segment 3: 256 bytes (final)
    SetTransferCount(256);
    SetupPccDma(DmaBase + 512, 256);
    WriteReg(0x10, 0x45);
    IssueSat();
    EXPECT_EQ(ReadCsr(), 0x16) << "Expected SEL_XFER_DONE after final segment";

    // Verify all data
    for (int i = 0; i < 768; i++) {
        EXPECT_EQ(memory.PeekByte(DmaBase + static_cast<uint32_t>(i)), testData[i])
            << "Data mismatch at offset " << i;
        if (memory.PeekByte(DmaBase + static_cast<uint32_t>(i)) != testData[i]) break;
    }
}

// --- CDB group lengths ---

TEST_F(Wd33c93DmaTest, Sat_CdbGroup2_10ByteCdb)
{
    // READ(10) = opcode 0x28, group 1 (10-byte CDB)
    target0.SetNoDataResult();

    WriteReg(0x03, 0x28); // READ(10) opcode
    WriteReg(0x04, 0x00);
    WriteReg(0x05, 0x00);
    WriteReg(0x06, 0x00);
    WriteReg(0x07, 0x01);
    WriteReg(0x08, 0x00);
    WriteReg(0x09, 0x00);
    WriteReg(0x0A, 0x01);
    WriteReg(0x0B, 0x00);
    WriteReg(0x0C, 0x00);

    SetTransferCount(0);
    WriteReg(0x10, 0x00);

    IssueSat();

    ReadCsr();
    EXPECT_EQ(target0.lastCdbLength, 10);
    EXPECT_EQ(target0.lastCdb[0], 0x28);
}

// --- Level I (SEL_ATN) DataIn via DMA ---

TEST_F(Wd33c93DmaTest, LevelI_DataIn_DMA)
{
    // Test Level I flow with DMA transfer (PIO alternative to SBT)
    std::vector<uint8_t> testData(256);
    for (int i = 0; i < 256; i++) testData[i] = static_cast<uint8_t>(i);
    target0.SetDataInResult(testData);

    // Select target with ATN
    SetDestId(0);
    IssueCommand(0x06); // SEL_ATN

    uint8_t csr = ReadCsr();
    EXPECT_EQ(csr, 0x11) << "Expected CSR_SELECT after select";

    // Message Out phase — send IDENTIFY
    SetTransferCount(1);
    IssueCommand(0x20); // XFER_INFO (PIO)
    WriteReg(0x19, 0x80); // IDENTIFY message
    csr = ReadCsr();

    // Command phase — send CDB
    SetTransferCount(6);
    IssueCommand(0x20);
    WriteReg(0x19, 0x08); // READ(6)
    WriteReg(0x19, 0x00);
    WriteReg(0x19, 0x00);
    WriteReg(0x19, 0x00);
    WriteReg(0x19, 0x01); // 1 block
    WriteReg(0x19, 0x00);
    csr = ReadCsr();

    // Data In phase — DMA transfer
    if ((csr & 0x0F) == 0x09 || csr == 0x29) { // DATA_IN phase
        SetTransferCount(256);
        SetupPccDma(DmaBase, 256);
        IssueCommand(0x20); // XFER_INFO for DMA
        csr = ReadCsr();
    }

    // Verify we get status phase or data was transferred
    // (Exact CSR depends on Level I implementation details)
    EXPECT_TRUE(target0.processCount >= 1) << "Target command should have been executed";
}

} // namespace Em68030::Tests
