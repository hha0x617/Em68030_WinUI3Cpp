#include "pch.h"
#include <gtest/gtest.h>
#include "IO/VirtualNetworkHandler.h"
#include "IO/NetworkUtils.h"

using namespace Em68030::IO;
namespace NU = Em68030::IO::NetworkUtils;

// ============================================================================
// Test fixture
// ============================================================================

class VirtualNetworkHandlerTest : public ::testing::Test {
protected:
    VirtualNetworkHandler handler;

    static constexpr std::array<uint8_t, 6> GuestMac = { 0x08, 0x00, 0x3E, 0x21, 0x00, 0x00 };
    static constexpr std::array<uint8_t, 6> GatewayMac = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    static constexpr std::array<uint8_t, 4> GatewayIp = { 10, 0, 2, 2 };
    static constexpr std::array<uint8_t, 4> GuestIp = { 10, 0, 2, 15 };

    void SetUp() override {
        handler.SetGuestMac(GuestMac);
    }

    void SendArpToLearnGuestIp() {
        auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
        handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));
        handler.DequeuePacket(); // discard reply
    }

    static std::vector<uint8_t> BuildArpRequest(const uint8_t* senderMac,
        const uint8_t* senderIp, const uint8_t* targetIp)
    {
        std::vector<uint8_t> pkt(60, 0);
        std::memset(pkt.data(), 0xFF, 6); // broadcast dst
        std::memcpy(pkt.data() + 6, senderMac, 6);
        NU::WriteBE16(pkt.data(), 12, 0x0806);
        NU::WriteBE16(pkt.data(), 14, 0x0001);
        NU::WriteBE16(pkt.data(), 16, 0x0800);
        pkt[18] = 6; pkt[19] = 4;
        NU::WriteBE16(pkt.data(), 20, 0x0001);
        std::memcpy(pkt.data() + 22, senderMac, 6);
        std::memcpy(pkt.data() + 28, senderIp, 4);
        std::memcpy(pkt.data() + 38, targetIp, 4);
        return pkt;
    }

    static std::vector<uint8_t> BuildIcmpEchoRequest(const uint8_t* srcMac, const uint8_t* dstMac,
        const uint8_t* srcIp, const uint8_t* dstIp,
        uint16_t id, uint16_t seq, const uint8_t* payload, int payloadLen)
    {
        int icmpLen = 8 + payloadLen;
        int ipLen = 20 + icmpLen;
        int frameLen = std::max(14 + ipLen, 60);
        std::vector<uint8_t> pkt(frameLen, 0);

        std::memcpy(pkt.data(), dstMac, 6);
        std::memcpy(pkt.data() + 6, srcMac, 6);
        NU::WriteBE16(pkt.data(), 12, 0x0800);

        pkt[14] = 0x45;
        NU::WriteBE16(pkt.data(), 16, static_cast<uint16_t>(ipLen));
        NU::WriteBE16(pkt.data(), 20, 0x4000);
        pkt[22] = 64;
        pkt[23] = 1; // ICMP
        std::memcpy(pkt.data() + 26, srcIp, 4);
        std::memcpy(pkt.data() + 30, dstIp, 4);
        uint16_t ipCsum = NU::ComputeChecksum(pkt.data() + 14, 20);
        NU::WriteBE16(pkt.data(), 24, ipCsum);

        pkt[34] = 8; // Echo Request
        NU::WriteBE16(pkt.data(), 38, id);
        NU::WriteBE16(pkt.data(), 40, seq);
        if (payloadLen > 0)
            std::memcpy(pkt.data() + 42, payload, payloadLen);
        uint16_t icmpCsum = NU::ComputeChecksum(pkt.data() + 34, icmpLen);
        NU::WriteBE16(pkt.data(), 36, icmpCsum);

        return pkt;
    }

    static std::vector<uint8_t> BuildTcpPacket(const uint8_t* srcMac, const uint8_t* dstMac,
        const uint8_t* srcIp, const uint8_t* dstIp,
        uint16_t srcPort, uint16_t dstPort,
        uint32_t seq, uint32_t ack, uint8_t flags,
        const uint8_t* payload = nullptr, int payloadLen = 0)
    {
        int tcpLen = 20 + payloadLen;
        int ipLen = 20 + tcpLen;
        int frameLen = 14 + ipLen; // No padding for correct data parsing
        std::vector<uint8_t> pkt(frameLen, 0);

        std::memcpy(pkt.data(), dstMac, 6);
        std::memcpy(pkt.data() + 6, srcMac, 6);
        NU::WriteBE16(pkt.data(), 12, 0x0800);

        pkt[14] = 0x45;
        NU::WriteBE16(pkt.data(), 16, static_cast<uint16_t>(ipLen));
        NU::WriteBE16(pkt.data(), 20, 0x4000);
        pkt[22] = 64;
        pkt[23] = 6; // TCP
        std::memcpy(pkt.data() + 26, srcIp, 4);
        std::memcpy(pkt.data() + 30, dstIp, 4);
        uint16_t ipCsum = NU::ComputeChecksum(pkt.data() + 14, 20);
        NU::WriteBE16(pkt.data(), 24, ipCsum);

        NU::WriteBE16(pkt.data(), 34, srcPort);
        NU::WriteBE16(pkt.data(), 36, dstPort);
        NU::WriteBE32(pkt.data(), 38, seq);
        NU::WriteBE32(pkt.data(), 42, ack);
        pkt[46] = 0x50; // data offset = 5
        pkt[47] = flags;
        NU::WriteBE16(pkt.data(), 48, 65535); // window

        if (payload != nullptr && payloadLen > 0)
            std::memcpy(pkt.data() + 54, payload, payloadLen);

        // TCP checksum
        uint16_t tcpCsum = NU::ComputeTransportChecksum(srcIp, dstIp, 6, pkt.data() + 34, tcpLen);
        NU::WriteBE16(pkt.data(), 50, tcpCsum);

        return pkt;
    }

    static std::vector<uint8_t> BuildUdpPacket(const uint8_t* srcMac, const uint8_t* dstMac,
        const uint8_t* srcIp, const uint8_t* dstIp,
        uint16_t srcPort, uint16_t dstPort,
        const uint8_t* payload, int payloadLen)
    {
        int udpLen = 8 + payloadLen;
        int ipLen = 20 + udpLen;
        int frameLen = std::max(14 + ipLen, 60);
        std::vector<uint8_t> pkt(frameLen, 0);

        std::memcpy(pkt.data(), dstMac, 6);
        std::memcpy(pkt.data() + 6, srcMac, 6);
        NU::WriteBE16(pkt.data(), 12, 0x0800);

        pkt[14] = 0x45;
        NU::WriteBE16(pkt.data(), 16, static_cast<uint16_t>(ipLen));
        NU::WriteBE16(pkt.data(), 20, 0x4000);
        pkt[22] = 64;
        pkt[23] = 17; // UDP
        std::memcpy(pkt.data() + 26, srcIp, 4);
        std::memcpy(pkt.data() + 30, dstIp, 4);
        uint16_t ipCsum = NU::ComputeChecksum(pkt.data() + 14, 20);
        NU::WriteBE16(pkt.data(), 24, ipCsum);

        NU::WriteBE16(pkt.data(), 34, srcPort);
        NU::WriteBE16(pkt.data(), 36, dstPort);
        NU::WriteBE16(pkt.data(), 38, static_cast<uint16_t>(udpLen));
        if (payloadLen > 0)
            std::memcpy(pkt.data() + 42, payload, payloadLen);
        uint16_t udpCsum = NU::ComputeTransportChecksum(srcIp, dstIp, 17, pkt.data() + 34, udpLen);
        if (udpCsum == 0) udpCsum = 0xFFFF;
        NU::WriteBE16(pkt.data(), 40, udpCsum);

        return pkt;
    }
};

// ============================================================================
// ARP
// ============================================================================

TEST_F(VirtualNetworkHandlerTest, Arp_Request_GetsReply)
{
    auto arpReq = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
    handler.ProcessPacket(arpReq.data(), static_cast<int>(arpReq.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0x0806, NU::ReadBE16(reply.data(), 12));
    EXPECT_EQ(0x0002, NU::ReadBE16(reply.data(), 20)); // ARP Reply opcode
    EXPECT_EQ(0, std::memcmp(GatewayMac.data(), reply.data() + 22, 6)); // Sender MAC
    EXPECT_EQ(0, std::memcmp(GuestMac.data(), reply.data() + 32, 6));   // Target MAC
}

TEST_F(VirtualNetworkHandlerTest, Arp_ProxyArp_RespondsToAnyTargetIp)
{
    uint8_t targetIp[] = { 192, 168, 1, 1 };
    auto arpReq = BuildArpRequest(GuestMac.data(), GuestIp.data(), targetIp);
    handler.ProcessPacket(arpReq.data(), static_cast<int>(arpReq.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0, std::memcmp(targetIp, reply.data() + 28, 4)); // Sender IP = target from request
}

TEST_F(VirtualNetworkHandlerTest, Arp_ShortPacket_Ignored)
{
    uint8_t tooShort[30] = {};
    NU::WriteBE16(tooShort, 12, 0x0806);
    handler.ProcessPacket(tooShort, 30);
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(VirtualNetworkHandlerTest, Arp_ReplyPacket_Ignored)
{
    auto arpReply = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
    NU::WriteBE16(arpReply.data(), 20, 0x0002); // Change to Reply
    handler.ProcessPacket(arpReply.data(), static_cast<int>(arpReply.size()));
    EXPECT_FALSE(handler.HasPendingPacket());
}

// ============================================================================
// ICMP
// ============================================================================

TEST_F(VirtualNetworkHandlerTest, Icmp_EchoRequest_GetsEchoReply)
{
    uint8_t payload[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    auto ping = BuildIcmpEchoRequest(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 0x1234, 0x0001, payload, 4);

    SendArpToLearnGuestIp();
    handler.ProcessPacket(ping.data(), static_cast<int>(ping.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0, reply[34]); // Echo Reply type
    EXPECT_EQ(0x1234, NU::ReadBE16(reply.data(), 38)); // ID
    EXPECT_EQ(0x0001, NU::ReadBE16(reply.data(), 40)); // Sequence
    EXPECT_EQ(0xAA, reply[42]);
    EXPECT_EQ(0xBB, reply[43]);
    EXPECT_EQ(0xCC, reply[44]);
    EXPECT_EQ(0xDD, reply[45]);
    EXPECT_EQ(0, std::memcmp(GatewayIp.data(), reply.data() + 26, 4)); // Src IP
    EXPECT_EQ(0, std::memcmp(GuestIp.data(), reply.data() + 30, 4));   // Dst IP
}

TEST_F(VirtualNetworkHandlerTest, Icmp_NonEchoRequest_Ignored)
{
    auto pkt = BuildIcmpEchoRequest(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 1, 1, nullptr, 0);
    pkt[34] = 3; // Dest Unreachable
    // Recalc ICMP checksum
    pkt[36] = 0; pkt[37] = 0;
    int icmpLen = static_cast<int>(pkt.size()) - 34;
    uint16_t csum = NU::ComputeChecksum(pkt.data() + 34, icmpLen);
    NU::WriteBE16(pkt.data(), 36, csum);

    SendArpToLearnGuestIp();
    handler.ProcessPacket(pkt.data(), static_cast<int>(pkt.size()));
    EXPECT_FALSE(handler.HasPendingPacket());
}

// ============================================================================
// TCP — Port 7 Echo
// ============================================================================

TEST_F(VirtualNetworkHandlerTest, Tcp_SynToPort7_GetsSynAck)
{
    SendArpToLearnGuestIp();

    auto syn = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1000, 0, 0x02);

    handler.ProcessPacket(syn.data(), static_cast<int>(syn.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0x12, reply[47]); // SYN+ACK
    EXPECT_EQ(1001u, NU::ReadBE32(reply.data(), 42)); // ACK = seq+1
}

TEST_F(VirtualNetworkHandlerTest, Tcp_DataToPort7_GetsEchoBack)
{
    SendArpToLearnGuestIp();

    // SYN
    auto syn = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1000, 0, 0x02);
    handler.ProcessPacket(syn.data(), static_cast<int>(syn.size()));
    auto synAck = handler.DequeuePacket();
    uint32_t serverSeq = NU::ReadBE32(synAck.data(), 38);

    // ACK (handshake complete)
    auto ack = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1001, serverSeq + 1, 0x10);
    handler.ProcessPacket(ack.data(), static_cast<int>(ack.size()));

    // PSH+ACK with data "Hi"
    uint8_t hiData[] = { 0x48, 0x69 };
    auto data = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1001, serverSeq + 1, 0x18,
        hiData, 2);
    handler.ProcessPacket(data.data(), static_cast<int>(data.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto echoReply = handler.DequeuePacket();

    int tcpDataOff = 34 + 20;
    EXPECT_EQ(0x48, echoReply[tcpDataOff]);     // 'H'
    EXPECT_EQ(0x69, echoReply[tcpDataOff + 1]); // 'i'
}

TEST_F(VirtualNetworkHandlerTest, Tcp_FinToPort7_GetsFinAck)
{
    SendArpToLearnGuestIp();

    // SYN → SYN-ACK → ACK → FIN
    auto syn = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1000, 0, 0x02);
    handler.ProcessPacket(syn.data(), static_cast<int>(syn.size()));
    handler.DequeuePacket(); // SYN-ACK

    auto ack = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1001, 1001, 0x10);
    handler.ProcessPacket(ack.data(), static_cast<int>(ack.size()));

    auto fin = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 7, 1001, 1001, 0x01);
    handler.ProcessPacket(fin.data(), static_cast<int>(fin.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();
    EXPECT_EQ(0x11, reply[47]); // FIN+ACK
}

TEST_F(VirtualNetworkHandlerTest, Tcp_SynToNonEchoPort_GetsRst)
{
    SendArpToLearnGuestIp();

    auto syn = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 80, 1000, 0, 0x02);
    handler.ProcessPacket(syn.data(), static_cast<int>(syn.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();
    EXPECT_EQ(0x14, reply[47]); // RST+ACK
}

TEST_F(VirtualNetworkHandlerTest, Tcp_RstToNonEchoPort_NoReply)
{
    SendArpToLearnGuestIp();

    auto rst = BuildTcpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 12345, 80, 1000, 0, 0x04);
    handler.ProcessPacket(rst.data(), static_cast<int>(rst.size()));

    EXPECT_FALSE(handler.HasPendingPacket());
}

// ============================================================================
// UDP — Port 7 Echo
// ============================================================================

TEST_F(VirtualNetworkHandlerTest, Udp_Port7_EchoesPayload)
{
    SendArpToLearnGuestIp();

    uint8_t payload[] = { 0x48, 0x65, 0x6C, 0x6C, 0x6F }; // "Hello"
    auto udp = BuildUdpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 5000, 7, payload, 5);
    handler.ProcessPacket(udp.data(), static_cast<int>(udp.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(7, NU::ReadBE16(reply.data(), 34));     // src port
    EXPECT_EQ(5000, NU::ReadBE16(reply.data(), 36));   // dst port
    for (int i = 0; i < 5; i++)
        EXPECT_EQ(payload[i], reply[42 + i]);
}

TEST_F(VirtualNetworkHandlerTest, Udp_NonEchoPort_Ignored)
{
    SendArpToLearnGuestIp();

    uint8_t payload[] = { 0x01 };
    auto udp = BuildUdpPacket(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 5000, 53, payload, 1);
    handler.ProcessPacket(udp.data(), static_cast<int>(udp.size()));

    EXPECT_FALSE(handler.HasPendingPacket());
}

// ============================================================================
// Reset
// ============================================================================

TEST_F(VirtualNetworkHandlerTest, Reset_ClearsPendingPackets)
{
    SendArpToLearnGuestIp();

    auto ping = BuildIcmpEchoRequest(GuestMac.data(), GatewayMac.data(),
        GuestIp.data(), GatewayIp.data(), 1, 1, nullptr, 0);
    handler.ProcessPacket(ping.data(), static_cast<int>(ping.size()));
    ASSERT_TRUE(handler.HasPendingPacket());

    handler.Reset();
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(VirtualNetworkHandlerTest, ShortFrame_Ignored)
{
    uint8_t tiny[] = { 0x00, 0x01, 0x02 };
    handler.ProcessPacket(tiny, 3);
    EXPECT_FALSE(handler.HasPendingPacket());
}
