#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <queue>
#include <unordered_map>

#include "INetworkHandler.h"

namespace Em68030::IO {

class VirtualNetworkHandler : public INetworkHandler {
public:
    void SetGuestMac(const std::array<uint8_t, 6>& mac) override;
    void ProcessPacket(const uint8_t* frame, int length) override;
    bool HasPendingPacket() const override;
    std::vector<uint8_t> DequeuePacket() override;
    void Reset() override;

private:
    static constexpr std::array<uint8_t, 6> GatewayMac = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    static constexpr std::array<uint8_t, 4> GatewayIp = { 10, 0, 2, 2 };

    static constexpr uint16_t ETHERTYPE_ARP  = 0x0806;
    static constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
    static constexpr uint8_t IP_PROTO_ICMP = 1;
    static constexpr uint8_t IP_PROTO_TCP  = 6;
    static constexpr uint8_t IP_PROTO_UDP  = 17;
    static constexpr int MIN_ETHERNET_FRAME = 60;

    // TCP flags
    static constexpr uint8_t TCP_FIN = 0x01;
    static constexpr uint8_t TCP_SYN = 0x02;
    static constexpr uint8_t TCP_RST = 0x04;
    static constexpr uint8_t TCP_PSH = 0x08;
    static constexpr uint8_t TCP_ACK = 0x10;

    enum class TcpState { SynReceived, Established };

    struct TcpConnectionState {
        TcpState State;
        uint32_t OurSeq;
        uint32_t TheirSeq;
    };

    void HandleArp(const uint8_t* frame, int length);
    void HandleIpv4(const uint8_t* frame, int length);
    void HandleIcmp(const uint8_t* frame, int length, int ipHeaderLen);
    void HandleTcp(const uint8_t* frame, int length, int ipHeaderLen);
    void HandleUdp(const uint8_t* frame, int length, int ipHeaderLen);

    std::vector<uint8_t> BuildTcpPacket(uint16_t srcPort, uint16_t dstPort,
        uint32_t seq, uint32_t ack, uint8_t flags,
        const uint8_t* payload, int payloadLen);

    static uint16_t ComputeChecksum(const uint8_t* data, int length);
    static void WriteBE16(uint8_t* buf, int off, uint16_t val);
    static void WriteBE32(uint8_t* buf, int off, uint32_t val);
    static uint16_t ReadBE16(const uint8_t* buf, int off);
    static uint32_t ReadBE32(const uint8_t* buf, int off);

    std::array<uint8_t, 6> m_guestMac{};
    std::array<uint8_t, 4> m_guestIp{};
    bool m_guestIpKnown = false;
    std::queue<std::vector<uint8_t>> m_rxQueue;
    std::unordered_map<uint16_t, TcpConnectionState> m_tcpConnections;
};

} // namespace Em68030::IO
