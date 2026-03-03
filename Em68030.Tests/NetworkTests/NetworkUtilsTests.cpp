#include "pch.h"
#include <gtest/gtest.h>
#include "IO/NetworkUtils.h"

using namespace Em68030::IO;

// ============================================================================
// Big-endian helpers
// ============================================================================

TEST(NetworkUtilsTests, WriteBE16_ReadsBackCorrectly)
{
    uint8_t buf[4] = {};
    NetworkUtils::WriteBE16(buf, 1, 0xABCD);
    EXPECT_EQ(0xAB, buf[1]);
    EXPECT_EQ(0xCD, buf[2]);
}

TEST(NetworkUtilsTests, ReadBE16_ReturnsCorrectValue)
{
    uint8_t buf[] = { 0x00, 0x12, 0x34, 0x00 };
    uint16_t val = NetworkUtils::ReadBE16(buf, 1);
    EXPECT_EQ(0x1234, val);
}

TEST(NetworkUtilsTests, WriteBE32_ReadsBackCorrectly)
{
    uint8_t buf[6] = {};
    NetworkUtils::WriteBE32(buf, 1, 0xDEADBEEF);
    EXPECT_EQ(0xDE, buf[1]);
    EXPECT_EQ(0xAD, buf[2]);
    EXPECT_EQ(0xBE, buf[3]);
    EXPECT_EQ(0xEF, buf[4]);
}

TEST(NetworkUtilsTests, ReadBE32_ReturnsCorrectValue)
{
    uint8_t buf[] = { 0x00, 0xCA, 0xFE, 0xBA, 0xBE };
    uint32_t val = NetworkUtils::ReadBE32(buf, 1);
    EXPECT_EQ(0xCAFEBABEu, val);
}

// ============================================================================
// Checksum
// ============================================================================

TEST(NetworkUtilsTests, ComputeChecksum_ZeroData_ReturnsFFFF)
{
    uint8_t data[20] = {};
    uint16_t csum = NetworkUtils::ComputeChecksum(data, 20);
    EXPECT_EQ(0xFFFF, csum);
}

TEST(NetworkUtilsTests, ComputeChecksum_ValidIpHeader_ReturnsZeroOnVerify)
{
    uint8_t hdr[20] = {};
    hdr[0] = 0x45;
    NetworkUtils::WriteBE16(hdr, 2, 40);
    hdr[8] = 64;
    hdr[9] = 6; // TCP
    hdr[12] = 10; hdr[13] = 0; hdr[14] = 2; hdr[15] = 15;
    hdr[16] = 8; hdr[17] = 8; hdr[18] = 8; hdr[19] = 8;

    uint16_t csum = NetworkUtils::ComputeChecksum(hdr, 20);
    NetworkUtils::WriteBE16(hdr, 10, csum);

    uint16_t verify = NetworkUtils::ComputeChecksum(hdr, 20);
    EXPECT_EQ(0, verify);
}

TEST(NetworkUtilsTests, ComputeChecksum_OddLength_HandlesCorrectly)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    uint16_t csum = NetworkUtils::ComputeChecksum(data, 3);
    // sum = 0x0102 + 0x0300 = 0x0402, ~0x0402 = 0xFBFD
    EXPECT_EQ(0xFBFD, csum);
}

// ============================================================================
// BuildArpReply
// ============================================================================

TEST(NetworkUtilsTests, BuildArpReply_HasCorrectFormat)
{
    uint8_t dstMac[] = { 0x08, 0x00, 0x3E, 0x21, 0x00, 0x00 };
    uint8_t srcMac[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    uint8_t srcIp[] = { 10, 0, 2, 2 };
    uint8_t dstIp[] = { 10, 0, 2, 15 };

    auto reply = NetworkUtils::BuildArpReply(dstMac, srcMac, srcIp, dstIp);

    EXPECT_GE(static_cast<int>(reply.size()), 60);
    // Ethernet header
    EXPECT_EQ(0, std::memcmp(dstMac, reply.data(), 6));
    EXPECT_EQ(0, std::memcmp(srcMac, reply.data() + 6, 6));
    EXPECT_EQ(0x0806, NetworkUtils::ReadBE16(reply.data(), 12));
    // ARP header
    EXPECT_EQ(0x0001, NetworkUtils::ReadBE16(reply.data(), 14));
    EXPECT_EQ(0x0800, NetworkUtils::ReadBE16(reply.data(), 16));
    EXPECT_EQ(6, reply[18]);
    EXPECT_EQ(4, reply[19]);
    EXPECT_EQ(0x0002, NetworkUtils::ReadBE16(reply.data(), 20));
    // Sender
    EXPECT_EQ(0, std::memcmp(srcMac, reply.data() + 22, 6));
    EXPECT_EQ(0, std::memcmp(srcIp, reply.data() + 28, 4));
    // Target
    EXPECT_EQ(0, std::memcmp(dstMac, reply.data() + 32, 6));
    EXPECT_EQ(0, std::memcmp(dstIp, reply.data() + 38, 4));
}

// ============================================================================
// BuildIpPacket
// ============================================================================

TEST(NetworkUtilsTests, BuildIpPacket_HasCorrectHeaders)
{
    uint8_t dstMac[] = { 0x08, 0x00, 0x3E, 0x21, 0x00, 0x00 };
    uint8_t srcMac[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    uint8_t srcIp[] = { 10, 0, 2, 2 };
    uint8_t dstIp[] = { 10, 0, 2, 15 };

    auto pkt = NetworkUtils::BuildIpPacket(dstMac, srcMac, srcIp, dstIp, NetworkUtils::IP_PROTO_TCP, 20);

    // Ethernet
    EXPECT_EQ(0, std::memcmp(dstMac, pkt.data(), 6));
    EXPECT_EQ(0, std::memcmp(srcMac, pkt.data() + 6, 6));
    EXPECT_EQ(0x0800, NetworkUtils::ReadBE16(pkt.data(), 12));
    // IP header
    EXPECT_EQ(0x45, pkt[14]);
    EXPECT_EQ(40, NetworkUtils::ReadBE16(pkt.data(), 16)); // total length
    EXPECT_EQ(64, pkt[22]);
    EXPECT_EQ(NetworkUtils::IP_PROTO_TCP, pkt[23]);
    // IP checksum verify
    uint16_t verify = NetworkUtils::ComputeChecksum(pkt.data() + 14, 20);
    EXPECT_EQ(0, verify);
    // IPs
    EXPECT_EQ(0, std::memcmp(srcIp, pkt.data() + 26, 4));
    EXPECT_EQ(0, std::memcmp(dstIp, pkt.data() + 30, 4));
}

TEST(NetworkUtilsTests, BuildIpPacket_PadsToMinimumFrame)
{
    uint8_t mac[6] = {};
    uint8_t ip[4] = {};
    auto pkt = NetworkUtils::BuildIpPacket(mac, mac, ip, ip, 0, 0);
    EXPECT_GE(static_cast<int>(pkt.size()), 60);
}

// ============================================================================
// ComputeTransportChecksum
// ============================================================================

TEST(NetworkUtilsTests, ComputeTransportChecksum_VerifiesCorrectly)
{
    uint8_t srcIp[] = { 10, 0, 2, 2 };
    uint8_t dstIp[] = { 10, 0, 2, 15 };
    uint8_t udp[8] = {};
    NetworkUtils::WriteBE16(udp, 0, 1234);
    NetworkUtils::WriteBE16(udp, 2, 53);
    NetworkUtils::WriteBE16(udp, 4, 8);

    uint16_t csum = NetworkUtils::ComputeTransportChecksum(srcIp, dstIp, NetworkUtils::IP_PROTO_UDP, udp, 8);
    NetworkUtils::WriteBE16(udp, 6, csum);

    uint16_t verify = NetworkUtils::ComputeTransportChecksum(srcIp, dstIp, NetworkUtils::IP_PROTO_UDP, udp, 8);
    EXPECT_EQ(0, verify);
}
