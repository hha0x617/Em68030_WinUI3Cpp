#include "pch.h"
#include <gtest/gtest.h>
#include "IO/SlirpNetworkHandler.h"
#include "IO/NetworkUtils.h"

using namespace Em68030::IO;
namespace NU = Em68030::IO::NetworkUtils;

// ============================================================================
// Test fixture
// ============================================================================

class SlirpNetworkHandlerTest : public ::testing::Test {
protected:
    SlirpNetworkHandler handler;

    static constexpr std::array<uint8_t, 6> GuestMac = { 0x08, 0x00, 0x3E, 0x21, 0x00, 0x00 };
    static constexpr std::array<uint8_t, 6> GatewayMac = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    static constexpr std::array<uint8_t, 4> GatewayIp = { 10, 0, 2, 2 };
    static constexpr std::array<uint8_t, 4> GuestIp = { 10, 0, 2, 15 };

    void SetUp() override {
        handler.SetGuestMac(GuestMac);
    }

    static std::vector<uint8_t> BuildArpRequest(const uint8_t* senderMac,
        const uint8_t* senderIp, const uint8_t* targetIp)
    {
        std::vector<uint8_t> pkt(60, 0);
        std::memset(pkt.data(), 0xFF, 6);
        std::memcpy(pkt.data() + 6, senderMac, 6);
        pkt[12] = 0x08; pkt[13] = 0x06;
        pkt[14] = 0x00; pkt[15] = 0x01;
        pkt[16] = 0x08; pkt[17] = 0x00;
        pkt[18] = 6; pkt[19] = 4;
        pkt[20] = 0x00; pkt[21] = 0x01;
        std::memcpy(pkt.data() + 22, senderMac, 6);
        std::memcpy(pkt.data() + 28, senderIp, 4);
        std::memcpy(pkt.data() + 38, targetIp, 4);
        return pkt;
    }
};

// ============================================================================
// ARP — Proxy ARP
// ============================================================================

TEST_F(SlirpNetworkHandlerTest, Arp_Request_GetsProxyReply)
{
    auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0x0806, NU::ReadBE16(reply.data(), 12));
    EXPECT_EQ(0x0002, NU::ReadBE16(reply.data(), 20)); // Reply
    EXPECT_EQ(0, std::memcmp(GatewayMac.data(), reply.data() + 22, 6)); // Sender MAC
    EXPECT_EQ(0, std::memcmp(GatewayIp.data(), reply.data() + 28, 4)); // Sender IP
}

TEST_F(SlirpNetworkHandlerTest, Arp_ProxyArp_RespondsToAnyIp)
{
    uint8_t remoteIp[] = { 8, 8, 8, 8 };
    auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), remoteIp);
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));

    ASSERT_TRUE(handler.HasPendingPacket());
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0, std::memcmp(remoteIp, reply.data() + 28, 4));
    EXPECT_EQ(0, std::memcmp(GatewayMac.data(), reply.data() + 22, 6));
}

TEST_F(SlirpNetworkHandlerTest, Arp_LearnGuestIp_FromSpa)
{
    auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));
    auto reply = handler.DequeuePacket();

    EXPECT_EQ(0, std::memcmp(GuestIp.data(), reply.data() + 38, 4));
}

TEST_F(SlirpNetworkHandlerTest, Arp_NonRequest_Ignored)
{
    auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
    arp[20] = 0x00; arp[21] = 0x02; // Reply
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(SlirpNetworkHandlerTest, Arp_DadProbe_Ignored)
{
    // DAD probe: SPA = 0.0.0.0, TPA = guest IP
    uint8_t zeroIp[] = { 0, 0, 0, 0 };
    auto arp = BuildArpRequest(GuestMac.data(), zeroIp, GuestIp.data());
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(SlirpNetworkHandlerTest, Arp_ForOwnIp_Ignored)
{
    // ARP request where TPA = guest's own IP should not be answered
    auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), GuestIp.data());
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(SlirpNetworkHandlerTest, Arp_TooShort_Ignored)
{
    uint8_t small[30] = {};
    small[12] = 0x08; small[13] = 0x06;
    handler.ProcessPacket(small, 30);
    EXPECT_FALSE(handler.HasPendingPacket());
}

// ============================================================================
// General
// ============================================================================

TEST_F(SlirpNetworkHandlerTest, ProcessPacket_ShortFrame_Ignored)
{
    uint8_t tiny[] = { 0x00, 0x01 };
    handler.ProcessPacket(tiny, 2);
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(SlirpNetworkHandlerTest, ProcessPacket_UnknownEtherType_Ignored)
{
    uint8_t pkt[60] = {};
    pkt[12] = 0x88; pkt[13] = 0x00;
    handler.ProcessPacket(pkt, 60);
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(SlirpNetworkHandlerTest, Reset_ClearsQueue)
{
    auto arp = BuildArpRequest(GuestMac.data(), GuestIp.data(), GatewayIp.data());
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));
    ASSERT_TRUE(handler.HasPendingPacket());

    handler.Reset();
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST_F(SlirpNetworkHandlerTest, SetGuestMac_UpdatesMacInReplies)
{
    std::array<uint8_t, 6> newMac = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    handler.SetGuestMac(newMac);

    auto arp = BuildArpRequest(newMac.data(), GuestIp.data(), GatewayIp.data());
    handler.ProcessPacket(arp.data(), static_cast<int>(arp.size()));

    auto reply = handler.DequeuePacket();
    EXPECT_EQ(0, std::memcmp(newMac.data(), reply.data(), 6));       // Ethernet dst
    EXPECT_EQ(0, std::memcmp(newMac.data(), reply.data() + 32, 6)); // ARP target MAC
}

TEST_F(SlirpNetworkHandlerTest, INetworkHandler_ImplementsInterface)
{
    INetworkHandler* iface = &handler;
    EXPECT_NE(nullptr, iface);
}
