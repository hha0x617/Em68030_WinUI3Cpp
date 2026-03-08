#include "pch.h"
#include <gtest/gtest.h>

#include "IO/Mk48t02Device.h"

namespace Em68030::Tests {

// ============================================================================
// Mk48t02Device Tests
// ============================================================================

class Mk48t02DeviceTest : public ::testing::Test {
protected:
    static constexpr uint32_t Base = 0xFFFE0000;
    IO::Mk48t02Device rtc;
};

// ============================================================================
// NVRAM read/write
// ============================================================================

TEST_F(Mk48t02DeviceTest, NVRAM_WriteAndReadByte) {
    rtc.WriteByte(Base + 0x0000, 0x42);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0000), 0x42);
}

TEST_F(Mk48t02DeviceTest, NVRAM_WriteAndReadMultipleLocations) {
    rtc.WriteByte(Base + 0x0100, 0xAA);
    rtc.WriteByte(Base + 0x0200, 0x55);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0100), 0xAA);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0200), 0x55);
}

TEST_F(Mk48t02DeviceTest, NVRAM_DefaultIsZero) {
    EXPECT_EQ(rtc.ReadByte(Base + 0x0000), 0);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0100), 0);
    EXPECT_EQ(rtc.ReadByte(Base + 0x07F7), 0); // Last NVRAM byte before clock
}

TEST_F(Mk48t02DeviceTest, NVRAM_OutOfRange_ReturnsFF) {
    EXPECT_EQ(rtc.ReadByte(Base + 2048), 0xFF);
}

TEST_F(Mk48t02DeviceTest, NVRAM_OutOfRange_WriteIgnored) {
    rtc.WriteByte(Base + 2048, 0x42);
    EXPECT_EQ(rtc.ReadByte(Base + 2048), 0xFF);
}

// ============================================================================
// Word / Long access
// ============================================================================

TEST_F(Mk48t02DeviceTest, NVRAM_ReadWord) {
    rtc.WriteByte(Base + 0x10, 0xAB);
    rtc.WriteByte(Base + 0x11, 0xCD);
    EXPECT_EQ(rtc.ReadWord(Base + 0x10), 0xABCD);
}

TEST_F(Mk48t02DeviceTest, NVRAM_WriteWord) {
    rtc.WriteWord(Base + 0x20, 0x1234);
    EXPECT_EQ(rtc.ReadByte(Base + 0x20), 0x12);
    EXPECT_EQ(rtc.ReadByte(Base + 0x21), 0x34);
}

TEST_F(Mk48t02DeviceTest, NVRAM_ReadLong) {
    rtc.WriteByte(Base + 0x30, 0xDE);
    rtc.WriteByte(Base + 0x31, 0xAD);
    rtc.WriteByte(Base + 0x32, 0xBE);
    rtc.WriteByte(Base + 0x33, 0xEF);
    EXPECT_EQ(rtc.ReadLong(Base + 0x30), 0xDEADBEEFu);
}

TEST_F(Mk48t02DeviceTest, NVRAM_WriteLong) {
    rtc.WriteLong(Base + 0x40, 0xCAFEBABE);
    EXPECT_EQ(rtc.ReadByte(Base + 0x40), 0xCA);
    EXPECT_EQ(rtc.ReadByte(Base + 0x41), 0xFE);
    EXPECT_EQ(rtc.ReadByte(Base + 0x42), 0xBA);
    EXPECT_EQ(rtc.ReadByte(Base + 0x43), 0xBE);
}

// ============================================================================
// Clock registers — basic sanity (values are BCD, from real time)
// ============================================================================

TEST_F(Mk48t02DeviceTest, Clock_SecondsInBcdRange) {
    uint8_t sec = rtc.ReadByte(Base + 0x7F9);
    // BCD 0-59: high nibble 0-5, low nibble 0-9
    EXPECT_LE(sec >> 4, 5);
    EXPECT_LE(sec & 0x0F, 9);
}

TEST_F(Mk48t02DeviceTest, Clock_MinutesInBcdRange) {
    uint8_t min = rtc.ReadByte(Base + 0x7FA);
    EXPECT_LE(min >> 4, 5);
    EXPECT_LE(min & 0x0F, 9);
}

TEST_F(Mk48t02DeviceTest, Clock_HoursInBcdRange) {
    uint8_t hour = rtc.ReadByte(Base + 0x7FB);
    EXPECT_LE(hour >> 4, 2);
    EXPECT_LE(hour & 0x0F, 9);
    // BCD value must be <= 23
    int h = (hour >> 4) * 10 + (hour & 0x0F);
    EXPECT_LE(h, 23);
}

TEST_F(Mk48t02DeviceTest, Clock_DayOfWeekInRange) {
    uint8_t dow = rtc.ReadByte(Base + 0x7FC);
    EXPECT_GE(dow, 1);
    EXPECT_LE(dow, 7);
}

TEST_F(Mk48t02DeviceTest, Clock_DateInBcdRange) {
    uint8_t date = rtc.ReadByte(Base + 0x7FD);
    int d = (date >> 4) * 10 + (date & 0x0F);
    EXPECT_GE(d, 1);
    EXPECT_LE(d, 31);
}

TEST_F(Mk48t02DeviceTest, Clock_MonthInBcdRange) {
    uint8_t mon = rtc.ReadByte(Base + 0x7FE);
    int m = (mon >> 4) * 10 + (mon & 0x0F);
    EXPECT_GE(m, 1);
    EXPECT_LE(m, 12);
}

TEST_F(Mk48t02DeviceTest, Clock_YearInBcdRange) {
    uint8_t year = rtc.ReadByte(Base + 0x7FF);
    EXPECT_LE(year >> 4, 9);
    EXPECT_LE(year & 0x0F, 9);
}

// ============================================================================
// Control register
// ============================================================================

TEST_F(Mk48t02DeviceTest, Clock_ControlRegister_Writable) {
    rtc.WriteByte(Base + 0x7F8, 0x80); // Set Write bit
    // Control register reads back from NVRAM (not live clock)
    EXPECT_EQ(rtc.ReadByte(Base + 0x7F8), 0x80);
}

TEST_F(Mk48t02DeviceTest, Clock_ControlRegister_DefaultZero) {
    EXPECT_EQ(rtc.ReadByte(Base + 0x7F8), 0x00);
}

// ============================================================================
// Year offset (NetBSD vs Linux)
// ============================================================================

TEST_F(Mk48t02DeviceTest, YearOffset_Default_LinuxMode) {
    // Default offset = 0, year = current_year % 100
    uint8_t year = rtc.ReadByte(Base + 0x7FF);
    int y = (year >> 4) * 10 + (year & 0x0F);
    EXPECT_GE(y, 0);
    EXPECT_LE(y, 99);
}

TEST_F(Mk48t02DeviceTest, YearOffset_NetBSD_DifferentFromDefault) {
    uint8_t yearLinux = rtc.ReadByte(Base + 0x7FF);
    rtc.SetYearOffset(68); // NetBSD YEAR0=1968
    uint8_t yearNetBSD = rtc.ReadByte(Base + 0x7FF);
    // With offset 68, year = (current_year - 68) % 100
    // For 2026: Linux=26, NetBSD=(2026-1968)%100=58
    int yLinux = (yearLinux >> 4) * 10 + (yearLinux & 0x0F);
    int yNetBSD = (yearNetBSD >> 4) * 10 + (yearNetBSD & 0x0F);
    // They should differ (unless by coincidence year%100 == (year-68)%100)
    // For any year 2000-2067, they differ
    EXPECT_NE(yLinux, yNetBSD);
}

// ============================================================================
// SetMvme147Config
// ============================================================================

TEST_F(Mk48t02DeviceTest, SetMvme147Config_RamEnd) {
    uint8_t eth[] = {0x08, 0x00, 0x3E};
    rtc.SetMvme147Config(0x02000000, eth, 3);
    EXPECT_EQ(rtc.ReadLong(Base + 0x0774), 0x02000000u);
}

TEST_F(Mk48t02DeviceTest, SetMvme147Config_OffboardRamZero) {
    uint8_t eth[] = {0x08, 0x00, 0x3E};
    rtc.SetMvme147Config(0x02000000, eth, 3);
    EXPECT_EQ(rtc.ReadLong(Base + 0x0764), 0u); // Start
    EXPECT_EQ(rtc.ReadLong(Base + 0x0768), 0u); // End
}

TEST_F(Mk48t02DeviceTest, SetMvme147Config_EthernetAddr) {
    uint8_t eth[] = {0xAA, 0xBB, 0xCC};
    rtc.SetMvme147Config(0x01000000, eth, 3);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0778), 0xAA);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0779), 0xBB);
    EXPECT_EQ(rtc.ReadByte(Base + 0x077A), 0xCC);
}

TEST_F(Mk48t02DeviceTest, SetMvme147Config_ShortEthernet_NoWrite) {
    // If ethernetAddrLen < 3, should not write ethernet bytes
    rtc.WriteByte(Base + 0x0778, 0xFF); // pre-fill
    uint8_t eth[] = {0x01, 0x02};
    rtc.SetMvme147Config(0x01000000, eth, 2);
    EXPECT_EQ(rtc.ReadByte(Base + 0x0778), 0xFF); // unchanged
}

} // namespace Em68030::Tests
