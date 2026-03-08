#include "pch.h"
#include <gtest/gtest.h>

#include "IO/Z8530Device.h"

namespace Em68030::Tests {

// ============================================================================
// Z8530Channel Tests
// ============================================================================

class Z8530ChannelTest : public ::testing::Test {
protected:
    IO::Z8530Channel channel;
    int interruptChangedCount = 0;
    std::vector<uint8_t> transmittedChars;

    void SetUp() override {
        interruptChangedCount = 0;
        transmittedChars.clear();
        channel.InterruptStateChanged = [this]() { interruptChangedCount++; };
        channel.CharTransmitted = [this](uint8_t ch) { transmittedChars.push_back(ch); };
    }

    // Helper: write to a WR register via the register pointer mechanism.
    void WriteReg(uint8_t reg, uint8_t value) {
        // First write sets the register pointer (reg must be non-zero, or use Point High)
        if (reg < 8) {
            channel.WriteControl(reg); // Sets register pointer to reg
        } else {
            // Point High: cmd=1 (bits 5-3 = 001), regSelect = reg & 0x07
            // value = (1 << 3) | (reg & 0x07) = 0x08 | (reg & 0x07)
            channel.WriteControl(static_cast<uint8_t>(0x08 | (reg & 0x07)));
        }
        channel.WriteControl(value); // Actual register write
    }

    // Helper: read from a RR register via the register pointer mechanism.
    uint8_t ReadReg(uint8_t reg) {
        if (reg < 8) {
            channel.WriteControl(reg); // Sets register pointer
        } else {
            channel.WriteControl(static_cast<uint8_t>(0x08 | (reg & 0x07)));
        }
        return channel.ReadControl();
    }
};

// --- RR0 Default State ---

TEST_F(Z8530ChannelTest, RR0_DefaultState_TxEmptyAndDCDAndCTS)
{
    // RR0 default: TxEmpty=1, DCD=1, CTS=1, RxAvail=0
    uint8_t rr0 = channel.ReadControl();
    EXPECT_EQ(rr0 & 0x01, 0x00); // RxAvail = 0
    EXPECT_NE(rr0 & 0x04, 0);    // TxEmpty = 1
    EXPECT_NE(rr0 & 0x08, 0);    // DCD = 1
    EXPECT_NE(rr0 & 0x20, 0);    // CTS = 1
}

// --- Register Pointer ---

TEST_F(Z8530ChannelTest, RegisterPointer_ResetsAfterRead)
{
    // Write register pointer to 1, then read — pointer should reset to 0
    channel.WriteControl(1); // Set pointer to RR1
    uint8_t rr1 = channel.ReadControl(); // Reads RR1, resets pointer
    EXPECT_EQ(rr1, 0x01); // RR1: All Sent

    // Next read should be RR0 (pointer reset)
    uint8_t rr0 = channel.ReadControl();
    EXPECT_NE(rr0 & 0x04, 0); // TxEmpty in RR0
}

TEST_F(Z8530ChannelTest, RegisterPointer_ResetsAfterWrite)
{
    WriteReg(1, 0x00); // Write WR1 = 0
    // After the write, pointer should be back at 0
    uint8_t rr0 = channel.ReadControl(); // Should read RR0
    EXPECT_NE(rr0 & 0x04, 0); // TxEmpty
}

// --- Point High (WR0 cmd=1) ---

TEST_F(Z8530ChannelTest, PointHigh_NotImplemented)
{
    // Point High (cmd=1) is not implemented because Linux uses 16550 UART
    // instead of Z8530 SCC for console output. The command is ignored and
    // the register pointer stays at 0 (WR0).
    channel.WriteControl(0x08); // cmd=1, regSelect=0 → ignored
    channel.WriteControl(0x41); // This writes to WR0, not WR8

    EXPECT_EQ(transmittedChars.size(), 0u); // No data transmitted
}

// --- TX Operations ---

TEST_F(Z8530ChannelTest, WriteData_TransmitsChar)
{
    channel.WriteData(0x42);
    ASSERT_EQ(transmittedChars.size(), 1u);
    EXPECT_EQ(transmittedChars[0], 0x42);
}

TEST_F(Z8530ChannelTest, WriteData_SetsTxInProgress)
{
    channel.WriteData(0x42);
    EXPECT_TRUE(channel.IsTxInProgress());
    EXPECT_FALSE(channel.IsTxIntPending());
}

TEST_F(Z8530ChannelTest, WriteData_RR0_TxEmptyClears)
{
    channel.WriteData(0x42);
    uint8_t rr0 = channel.ReadControl();
    EXPECT_EQ(rr0 & 0x04, 0); // TxEmpty should be 0 during transmission
}

TEST_F(Z8530ChannelTest, Tick_CompletesTx_SetsTxIntPending)
{
    channel.WriteData(0x42);
    EXPECT_TRUE(channel.IsTxInProgress());

    channel.Tick(false);
    EXPECT_FALSE(channel.IsTxInProgress());
    EXPECT_TRUE(channel.IsTxIntPending());
}

TEST_F(Z8530ChannelTest, Tick_AfterTxComplete_RR0_TxEmptyAsserts)
{
    channel.WriteData(0x42);
    channel.Tick(false); // Complete transmission

    uint8_t rr0 = channel.ReadControl();
    EXPECT_NE(rr0 & 0x04, 0); // TxEmpty should be 1 again
}

// --- TX Interrupt Enable ---

TEST_F(Z8530ChannelTest, WR1_EnableTxInt_ImmediatelyAssertsTxIntPending)
{
    // Z8530 spec: first TX interrupt fires when TIE is set with buffer empty
    WriteReg(1, 0x02); // TIE = 1
    EXPECT_TRUE(channel.IsTxIntPending());
}

TEST_F(Z8530ChannelTest, WR0_Cmd5_ResetssTxIntPending)
{
    WriteReg(1, 0x02); // Enable TIE → TxIntPending = true
    EXPECT_TRUE(channel.IsTxIntPending());

    // WR0 command 5 (bits 5-3 = 101) = Reset Tx Int Pending
    channel.WriteControl(0x28); // cmd=5, regSelect=0
    EXPECT_FALSE(channel.IsTxIntPending());
}

// --- TX Prod Mechanism ---

TEST_F(Z8530ChannelTest, TxProd_ReassertsAfter16Ticks)
{
    WriteReg(1, 0x02); // TIE = 1 → TxIntPending = true

    // Reset TxIntPending
    channel.WriteControl(0x28); // cmd=5
    EXPECT_FALSE(channel.IsTxIntPending());

    // Tick 15 times — should not reassert
    for (int i = 0; i < 15; i++)
        channel.Tick(false);
    EXPECT_FALSE(channel.IsTxIntPending());

    // 16th tick — should reassert
    channel.Tick(false);
    EXPECT_TRUE(channel.IsTxIntPending());
}

// --- RX via ReceiveChar (hardware RX FIFO) ---

TEST_F(Z8530ChannelTest, ReceiveChar_SetsRxAvailInRR0)
{
    channel.ReceiveChar(0x61);
    uint8_t rr0 = channel.ReadControl();
    EXPECT_NE(rr0 & 0x01, 0); // RxAvail = 1
}

TEST_F(Z8530ChannelTest, ReceiveChar_ReadData_ReturnsChar)
{
    channel.ReceiveChar(0x61);
    EXPECT_EQ(channel.ReadData(), 0x61);
}

TEST_F(Z8530ChannelTest, ReceiveChar_SetsRxIntPending)
{
    channel.ReceiveChar(0x61);
    EXPECT_TRUE(channel.IsRxIntPending());
}

TEST_F(Z8530ChannelTest, ReadData_ClearsRxIntPending)
{
    channel.ReceiveChar(0x61);
    EXPECT_TRUE(channel.IsRxIntPending());

    channel.ReadData(); // Consume
    EXPECT_FALSE(channel.IsRxIntPending());
}

TEST_F(Z8530ChannelTest, ReceiveChar_MultipleChars_FIFO)
{
    channel.ReceiveChar(0x61);
    channel.ReceiveChar(0x62);
    channel.ReceiveChar(0x63);

    EXPECT_EQ(channel.ReadData(), 0x61);
    EXPECT_EQ(channel.ReadData(), 0x62);
    EXPECT_EQ(channel.ReadData(), 0x63);
}

// --- RX via QueueInput (polled-mode user input) ---

TEST_F(Z8530ChannelTest, QueueInput_RR0_RxAvail_But_NoRxIntPending)
{
    channel.QueueInput(0x61);

    // RR0 should show RxAvail (for polling)
    uint8_t rr0 = channel.ReadControl();
    EXPECT_NE(rr0 & 0x01, 0); // RxAvail = 1

    // But RxIntPending should remain false (no interrupt)
    EXPECT_FALSE(channel.IsRxIntPending());
}

TEST_F(Z8530ChannelTest, QueueInput_ReadData_ReturnsCharDirectly)
{
    channel.QueueInput(0x42);
    EXPECT_EQ(channel.ReadData(), 0x42);
}

TEST_F(Z8530ChannelTest, QueueInput_NotPromotedWhenCpuRunning)
{
    WriteReg(1, 0x10); // RX interrupt enabled (bits 4-3 = 10)
    channel.QueueInput(0x42);

    // CPU running — should not promote to FIFO
    channel.Tick(false);
    EXPECT_FALSE(channel.IsRxIntPending());

    // Character should still be readable via polled path
    EXPECT_EQ(channel.ReadData(), 0x42);
}

TEST_F(Z8530ChannelTest, QueueInput_PromotedWhenCpuStopped)
{
    WriteReg(1, 0x10); // RX interrupt enabled
    channel.QueueInput(0x42);

    // CPU stopped — promote to FIFO
    channel.Tick(true);
    EXPECT_TRUE(channel.IsRxIntPending());

    EXPECT_EQ(channel.ReadData(), 0x42);
}

// --- HW FIFO takes priority over pending input ---

TEST_F(Z8530ChannelTest, ReadData_HwFifoTakesPriority)
{
    channel.QueueInput(0x41); // User input: 'A'
    channel.ReceiveChar(0x42); // HW receive: 'B'

    // HW FIFO should be read first
    EXPECT_EQ(channel.ReadData(), 0x42);
    EXPECT_EQ(channel.ReadData(), 0x41);
}

// --- ReadData returns 0 when empty ---

TEST_F(Z8530ChannelTest, ReadData_EmptyReturnsZero)
{
    EXPECT_EQ(channel.ReadData(), 0);
}

// --- Force TIE when CPU stopped ---

TEST_F(Z8530ChannelTest, Tick_CpuStopped_ForcesTIE)
{
    // Set up: TxIntPending=true, TIE=false, CPU stopped
    WriteReg(1, 0x02); // Enable TIE → TxIntPending = true
    // Now disable TIE
    WriteReg(1, 0x00); // TIE = false

    // TxIntPending should still be true (disabling TIE doesn't clear it)
    // Actually enabling then disabling — TxIntPending was set when TIE was enabled
    EXPECT_TRUE(channel.IsTxIntPending());
    EXPECT_FALSE(channel.IsTxInterruptEnabled());

    channel.Tick(true); // CPU stopped
    EXPECT_TRUE(channel.IsTxInterruptEnabled()); // TIE forced on
}

// ============================================================================
// Z8530Device Tests
// ============================================================================

class Z8530DeviceTest : public ::testing::Test {
protected:
    IO::Z8530Device device;
    std::vector<bool> interruptHistory;

    void SetUp() override {
        interruptHistory.clear();
        device.InterruptOutput = [this](bool active) {
            interruptHistory.push_back(active);
        };
    }

    static constexpr uint32_t Base = 0xFFFE3000;

    // Helper: write to a channel register via memory-mapped I/O
    void WriteChannelReg(uint32_t ctrlAddr, uint8_t reg, uint8_t value) {
        if (reg < 8) {
            device.WriteByte(ctrlAddr, reg);
        } else {
            device.WriteByte(ctrlAddr, static_cast<uint8_t>(0x08 | (reg & 0x07)));
        }
        device.WriteByte(ctrlAddr, value);
    }
};

// --- Address Map ---

TEST_F(Z8530DeviceTest, AddressMap_ChannelB_Control)
{
    // Offset 0 = Channel B Control
    // Write a char via Channel B data port, then check control
    device.WriteByte(Base + 1, 0x42); // Channel B Data write
    auto& chB = device.GetChannelB();
    EXPECT_TRUE(chB.IsTxInProgress());
}

TEST_F(Z8530DeviceTest, AddressMap_ChannelA_Control)
{
    // Offset 2 = Channel A Control, Offset 3 = Channel A Data
    device.WriteByte(Base + 3, 0x42); // Channel A Data write
    auto& chA = device.GetChannelA();
    EXPECT_TRUE(chA.IsTxInProgress());
}

TEST_F(Z8530DeviceTest, AddressMap_ChannelA_Data_ReadAfterReceive)
{
    device.GetChannelA().ReceiveChar(0x61);
    uint8_t data = device.ReadByte(Base + 3); // Channel A Data
    EXPECT_EQ(data, 0x61);
}

TEST_F(Z8530DeviceTest, AddressMap_ChannelB_Data_ReadAfterReceive)
{
    device.GetChannelB().ReceiveChar(0x62);
    uint8_t data = device.ReadByte(Base + 1); // Channel B Data
    EXPECT_EQ(data, 0x62);
}

// --- RR3 (Interrupt Pending, Channel A only) ---

TEST_F(Z8530DeviceTest, RR3_ChA_TxIntPending)
{
    // Enable TIE on Channel A → TxIntPending
    WriteChannelReg(Base + 2, 1, 0x02); // WR1 = TIE on Channel A

    // Read RR3 via Channel A control (set pointer to 3)
    device.WriteByte(Base + 2, 3); // register pointer = 3
    uint8_t rr3 = device.ReadByte(Base + 2); // read RR3
    EXPECT_NE(rr3 & 0x10, 0); // Ch A TX IP
}

TEST_F(Z8530DeviceTest, RR3_ChA_RxIntPending)
{
    // Enable RX interrupt and receive a char on Channel A
    WriteChannelReg(Base + 2, 1, 0x10); // WR1: RX interrupt enabled
    device.GetChannelA().ReceiveChar(0x61);

    device.WriteByte(Base + 2, 3);
    uint8_t rr3 = device.ReadByte(Base + 2);
    EXPECT_NE(rr3 & 0x20, 0); // Ch A RX IP
}

TEST_F(Z8530DeviceTest, RR3_ChB_TxIntPending)
{
    WriteChannelReg(Base + 0, 1, 0x02); // WR1 = TIE on Channel B

    device.WriteByte(Base + 2, 3); // Read RR3 via Channel A
    uint8_t rr3 = device.ReadByte(Base + 2);
    EXPECT_NE(rr3 & 0x02, 0); // Ch B TX IP
}

TEST_F(Z8530DeviceTest, RR3_ChB_RxIntPending)
{
    WriteChannelReg(Base + 0, 1, 0x10); // RX int enable on Ch B
    device.GetChannelB().ReceiveChar(0x62);

    device.WriteByte(Base + 2, 3);
    uint8_t rr3 = device.ReadByte(Base + 2);
    EXPECT_NE(rr3 & 0x04, 0); // Ch B RX IP
}

// --- Composite Interrupt ---

TEST_F(Z8530DeviceTest, CompositeInterrupt_Asserts_OnTxIntPending)
{
    WriteChannelReg(Base + 2, 1, 0x02); // Enable TIE on Ch A

    // Should have received interrupt assertion
    ASSERT_FALSE(interruptHistory.empty());
    EXPECT_TRUE(interruptHistory.back());
}

TEST_F(Z8530DeviceTest, CompositeInterrupt_Deasserts_WhenCleared)
{
    WriteChannelReg(Base + 2, 1, 0x02); // Enable TIE → interrupt asserts
    ASSERT_FALSE(interruptHistory.empty());
    EXPECT_TRUE(interruptHistory.back());

    // Reset TX Int Pending (cmd=5)
    device.WriteByte(Base + 2, 0x28); // WR0 cmd=5
    // Disable TIE
    WriteChannelReg(Base + 2, 1, 0x00);

    EXPECT_FALSE(interruptHistory.back());
}

TEST_F(Z8530DeviceTest, CompositeInterrupt_Asserts_OnRxIntPending)
{
    WriteChannelReg(Base + 2, 1, 0x10); // RX int enable on Ch A
    interruptHistory.clear();

    device.GetChannelA().ReceiveChar(0x61);

    ASSERT_FALSE(interruptHistory.empty());
    EXPECT_TRUE(interruptHistory.back());
}

// --- Word/Long Access ---

TEST_F(Z8530DeviceTest, ReadWord_CombinesTwoBytes)
{
    // ReadWord at Base should combine Channel B Control (high) + Channel B Data (low)
    device.GetChannelB().ReceiveChar(0x42);

    uint16_t word = device.ReadWord(Base);
    uint8_t hi = static_cast<uint8_t>(word >> 8);   // Ch B Control (RR0)
    uint8_t lo = static_cast<uint8_t>(word & 0xFF);  // Ch B Data
    EXPECT_EQ(lo, 0x42);
    EXPECT_NE(hi & 0x04, 0); // TxEmpty in RR0
}

// --- Tick delegates to both channels ---

TEST_F(Z8530DeviceTest, Tick_TicksBothChannels)
{
    device.GetChannelA().WriteData(0x41); // Ch A TX in progress
    device.GetChannelB().WriteData(0x42); // Ch B TX in progress

    EXPECT_TRUE(device.GetChannelA().IsTxInProgress());
    EXPECT_TRUE(device.GetChannelB().IsTxInProgress());

    device.Tick(false);

    EXPECT_FALSE(device.GetChannelA().IsTxInProgress());
    EXPECT_FALSE(device.GetChannelB().IsTxInProgress());
    EXPECT_TRUE(device.GetChannelA().IsTxIntPending());
    EXPECT_TRUE(device.GetChannelB().IsTxIntPending());
}

} // namespace Em68030::Tests
