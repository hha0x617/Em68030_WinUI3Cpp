#include "pch.h"

#include "VirtualNetworkHandler.h"
#include <cstring>
#include <algorithm>

namespace Em68030::IO {

void VirtualNetworkHandler::SetGuestMac(const std::array<uint8_t, 6>& mac)
{
    m_guestMac = mac;
}

void VirtualNetworkHandler::ProcessPacket(const uint8_t* frame, int length)
{
    if (length < 14) return;

    uint16_t etherType = ReadBE16(frame, 12);
    switch (etherType) {
        case ETHERTYPE_ARP:
            HandleArp(frame, length);
            break;
        case ETHERTYPE_IPV4:
            HandleIpv4(frame, length);
            break;
    }
}

bool VirtualNetworkHandler::HasPendingPacket() const
{
    return !m_rxQueue.empty();
}

std::vector<uint8_t> VirtualNetworkHandler::DequeuePacket()
{
    auto pkt = std::move(m_rxQueue.front());
    m_rxQueue.pop();
    return pkt;
}

void VirtualNetworkHandler::Reset()
{
    while (!m_rxQueue.empty()) m_rxQueue.pop();
    m_tcpConnections.clear();
}

// --- ARP ---

void VirtualNetworkHandler::HandleArp(const uint8_t* frame, int length)
{
    if (length < 42) return;

    uint16_t op = ReadBE16(frame, 20);
    if (op != 1) return; // Only ARP Request

    // Learn guest IP from SPA (offset 28)
    std::memcpy(m_guestIp.data(), frame + 28, 4);
    m_guestIpKnown = true;

    // Build ARP Reply
    std::vector<uint8_t> reply(MIN_ETHERNET_FRAME, 0);

    // Ethernet header
    std::memcpy(reply.data(), m_guestMac.data(), 6);
    std::memcpy(reply.data() + 6, GatewayMac.data(), 6);
    WriteBE16(reply.data(), 12, ETHERTYPE_ARP);

    // ARP header
    WriteBE16(reply.data(), 14, 0x0001); // hardware: Ethernet
    WriteBE16(reply.data(), 16, 0x0800); // protocol: IPv4
    reply[18] = 6;                        // hardware size
    reply[19] = 4;                        // protocol size
    WriteBE16(reply.data(), 20, 0x0002); // opcode: Reply

    // Sender: gateway
    std::memcpy(reply.data() + 22, GatewayMac.data(), 6);
    // Use TPA from request as our SPA (proxy ARP)
    std::memcpy(reply.data() + 28, frame + 38, 4);

    // Target: guest
    std::memcpy(reply.data() + 32, m_guestMac.data(), 6);
    std::memcpy(reply.data() + 38, m_guestIp.data(), 4);

    m_rxQueue.push(std::move(reply));
}

// --- IPv4 ---

void VirtualNetworkHandler::HandleIpv4(const uint8_t* frame, int length)
{
    if (length < 34) return;

    int ipHeaderLen = (frame[14] & 0x0F) * 4;
    if (length < 14 + ipHeaderLen) return;

    uint8_t protocol = frame[23];
    switch (protocol) {
        case IP_PROTO_ICMP:
            HandleIcmp(frame, length, ipHeaderLen);
            break;
        case IP_PROTO_TCP:
            HandleTcp(frame, length, ipHeaderLen);
            break;
        case IP_PROTO_UDP:
            HandleUdp(frame, length, ipHeaderLen);
            break;
    }
}

// --- ICMP ---

void VirtualNetworkHandler::HandleIcmp(const uint8_t* frame, int length, int ipHeaderLen)
{
    int icmpOffset = 14 + ipHeaderLen;
    if (length < icmpOffset + 8) return;

    uint8_t icmpType = frame[icmpOffset];
    if (icmpType != 8) return; // Only Echo Request

    // Copy frame as reply
    std::vector<uint8_t> reply(frame, frame + length);

    // Swap MAC addresses
    std::memcpy(reply.data(), frame + 6, 6);          // src -> dst
    std::memcpy(reply.data() + 6, GatewayMac.data(), 6); // gateway -> src

    // Swap IP addresses
    std::memcpy(reply.data() + 30, frame + 26, 4); // src IP -> dst IP
    std::memcpy(reply.data() + 26, frame + 30, 4); // dst IP -> src IP

    // TTL = 64
    reply[22] = 64;

    // Recalculate IP header checksum
    reply[24] = 0;
    reply[25] = 0;
    uint16_t ipCsum = ComputeChecksum(reply.data() + 14, ipHeaderLen);
    WriteBE16(reply.data(), 24, ipCsum);

    // ICMP: set type = 0 (Echo Reply)
    reply[icmpOffset] = 0;

    // Recalculate ICMP checksum
    reply[icmpOffset + 2] = 0;
    reply[icmpOffset + 3] = 0;
    int icmpLen = length - icmpOffset;
    uint16_t icmpCsum = ComputeChecksum(reply.data() + icmpOffset, icmpLen);
    WriteBE16(reply.data(), icmpOffset + 2, icmpCsum);

    m_rxQueue.push(std::move(reply));
}

// --- TCP ---

void VirtualNetworkHandler::HandleTcp(const uint8_t* frame, int length, int ipHeaderLen)
{
    int tcpOffset = 14 + ipHeaderLen;
    if (length < tcpOffset + 20) return;

    uint16_t srcPort = ReadBE16(frame, tcpOffset);
    uint16_t dstPort = ReadBE16(frame, tcpOffset + 2);
    uint32_t seq = ReadBE32(frame, tcpOffset + 4);
    int tcpDataOffset = (frame[tcpOffset + 12] >> 4) * 4;
    uint8_t flags = frame[tcpOffset + 13];

    // Non-echo port: send RST
    if (dstPort != 7)
    {
        if ((flags & TCP_RST) != 0) return;

        int dataLen = length - tcpOffset - tcpDataOffset;
        uint32_t ackSeq = seq + static_cast<uint32_t>(dataLen);
        if ((flags & TCP_SYN) != 0) ackSeq = seq + 1;
        if ((flags & TCP_FIN) != 0) ackSeq++;

        auto rst = BuildTcpPacket(dstPort, srcPort, 0, ackSeq, TCP_RST | TCP_ACK, nullptr, 0);
        m_rxQueue.push(std::move(rst));
        return;
    }

    // Port 7 echo server
    if ((flags & TCP_RST) != 0)
    {
        m_tcpConnections.erase(srcPort);
        return;
    }

    if ((flags & TCP_SYN) != 0)
    {
        TcpConnectionState state{};
        state.State = TcpState::SynReceived;
        state.OurSeq = 1000;
        state.TheirSeq = seq + 1;
        m_tcpConnections[srcPort] = state;

        auto synAck = BuildTcpPacket(7, srcPort, state.OurSeq, state.TheirSeq, TCP_SYN | TCP_ACK, nullptr, 0);
        m_rxQueue.push(std::move(synAck));
        return;
    }

    auto it = m_tcpConnections.find(srcPort);
    if (it == m_tcpConnections.end()) return;
    auto& conn = it->second;

    if ((flags & TCP_FIN) != 0)
    {
        uint32_t finAckSeq = conn.OurSeq + 1;
        auto finAck = BuildTcpPacket(7, srcPort, finAckSeq, seq + 1, TCP_FIN | TCP_ACK, nullptr, 0);
        m_rxQueue.push(std::move(finAck));
        m_tcpConnections.erase(it);
        return;
    }

    if ((flags & TCP_ACK) != 0)
    {
        if (conn.State == TcpState::SynReceived)
        {
            conn.State = TcpState::Established;
            conn.OurSeq = 1001;
        }

        int dataLen = length - tcpOffset - tcpDataOffset;
        if (dataLen > 0)
        {
            conn.TheirSeq = seq + static_cast<uint32_t>(dataLen);

            auto echoAck = BuildTcpPacket(7, srcPort, conn.OurSeq, conn.TheirSeq, TCP_PSH | TCP_ACK,
                frame + tcpOffset + tcpDataOffset, dataLen);
            conn.OurSeq += static_cast<uint32_t>(dataLen);
            m_rxQueue.push(std::move(echoAck));
        }
    }
}

std::vector<uint8_t> VirtualNetworkHandler::BuildTcpPacket(uint16_t srcPort, uint16_t dstPort,
    uint32_t seq, uint32_t ack, uint8_t flags,
    const uint8_t* payload, int payloadLen)
{
    int tcpHeaderLen = 20;
    int ipTotalLen = 20 + tcpHeaderLen + payloadLen;
    int frameLen = std::max(14 + ipTotalLen, MIN_ETHERNET_FRAME);

    std::vector<uint8_t> pkt(frameLen, 0);

    // Ethernet header
    std::memcpy(pkt.data(), m_guestMac.data(), 6);
    std::memcpy(pkt.data() + 6, GatewayMac.data(), 6);
    WriteBE16(pkt.data(), 12, ETHERTYPE_IPV4);

    // IP header
    pkt[14] = 0x45;
    WriteBE16(pkt.data(), 16, static_cast<uint16_t>(ipTotalLen));
    WriteBE16(pkt.data(), 20, 0x4000); // DF
    pkt[22] = 64; // TTL
    pkt[23] = IP_PROTO_TCP;
    std::memcpy(pkt.data() + 26, GatewayIp.data(), 4);
    std::memcpy(pkt.data() + 30, m_guestIp.data(), 4);
    uint16_t ipCsum = ComputeChecksum(pkt.data() + 14, 20);
    WriteBE16(pkt.data(), 24, ipCsum);

    // TCP header
    int tcpOff = 34;
    WriteBE16(pkt.data(), tcpOff, srcPort);
    WriteBE16(pkt.data(), tcpOff + 2, dstPort);
    WriteBE32(pkt.data(), tcpOff + 4, seq);
    WriteBE32(pkt.data(), tcpOff + 8, ack);
    pkt[tcpOff + 12] = 0x50; // data offset = 5
    pkt[tcpOff + 13] = flags;
    WriteBE16(pkt.data(), tcpOff + 14, 65535); // window

    // Payload
    if (payload != nullptr && payloadLen > 0)
        std::memcpy(pkt.data() + tcpOff + tcpHeaderLen, payload, payloadLen);

    // TCP checksum with pseudo-header
    int tcpSegLen = tcpHeaderLen + payloadLen;
    std::vector<uint8_t> pseudo(12 + tcpSegLen, 0);
    std::memcpy(pseudo.data(), GatewayIp.data(), 4);
    std::memcpy(pseudo.data() + 4, m_guestIp.data(), 4);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    WriteBE16(pseudo.data(), 10, static_cast<uint16_t>(tcpSegLen));
    std::memcpy(pseudo.data() + 12, pkt.data() + tcpOff, tcpSegLen);
    uint16_t tcpCsum = ComputeChecksum(pseudo.data(), static_cast<int>(pseudo.size()));
    WriteBE16(pkt.data(), tcpOff + 16, tcpCsum);

    return pkt;
}

// --- UDP ---

void VirtualNetworkHandler::HandleUdp(const uint8_t* frame, int length, int ipHeaderLen)
{
    int udpOffset = 14 + ipHeaderLen;
    if (length < udpOffset + 8) return;

    uint16_t srcPort = ReadBE16(frame, udpOffset);
    uint16_t dstPort = ReadBE16(frame, udpOffset + 2);
    uint16_t udpLen = ReadBE16(frame, udpOffset + 4);

    if (dstPort != 7) return;

    int payloadLen = udpLen - 8;
    if (payloadLen < 0 || length < udpOffset + udpLen) return;

    int ipTotalLen = 20 + 8 + payloadLen;
    int frameLen = std::max(14 + ipTotalLen, MIN_ETHERNET_FRAME);

    std::vector<uint8_t> reply(frameLen, 0);

    // Ethernet header
    std::memcpy(reply.data(), m_guestMac.data(), 6);
    std::memcpy(reply.data() + 6, GatewayMac.data(), 6);
    WriteBE16(reply.data(), 12, ETHERTYPE_IPV4);

    // IP header
    reply[14] = 0x45;
    WriteBE16(reply.data(), 16, static_cast<uint16_t>(ipTotalLen));
    WriteBE16(reply.data(), 20, 0x4000); // DF
    reply[22] = 64; // TTL
    reply[23] = IP_PROTO_UDP;
    std::memcpy(reply.data() + 26, GatewayIp.data(), 4);
    std::memcpy(reply.data() + 30, m_guestIp.data(), 4);
    uint16_t ipCsum = ComputeChecksum(reply.data() + 14, 20);
    WriteBE16(reply.data(), 24, ipCsum);

    // UDP header
    int rUdpOff = 34;
    WriteBE16(reply.data(), rUdpOff, dstPort);     // swap
    WriteBE16(reply.data(), rUdpOff + 2, srcPort);  // swap
    WriteBE16(reply.data(), rUdpOff + 4, udpLen);

    // Payload
    if (payloadLen > 0)
        std::memcpy(reply.data() + rUdpOff + 8, frame + udpOffset + 8, payloadLen);

    // UDP checksum with pseudo-header
    std::vector<uint8_t> pseudo(12 + udpLen, 0);
    std::memcpy(pseudo.data(), GatewayIp.data(), 4);
    std::memcpy(pseudo.data() + 4, m_guestIp.data(), 4);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_UDP;
    WriteBE16(pseudo.data(), 10, udpLen);
    std::memcpy(pseudo.data() + 12, reply.data() + rUdpOff, udpLen);
    uint16_t udpCsum = ComputeChecksum(pseudo.data(), static_cast<int>(pseudo.size()));
    if (udpCsum == 0) udpCsum = 0xFFFF;
    WriteBE16(reply.data(), rUdpOff + 6, udpCsum);

    m_rxQueue.push(std::move(reply));
}

// --- Checksum ---

uint16_t VirtualNetworkHandler::ComputeChecksum(const uint8_t* data, int length)
{
    uint32_t sum = 0;
    int i = 0;
    while (i < length - 1)
    {
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
        i += 2;
    }
    if (i < length)
        sum += static_cast<uint32_t>(data[i] << 8);

    while ((sum >> 16) != 0)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return static_cast<uint16_t>(~sum);
}

// --- Big-endian helpers ---

void VirtualNetworkHandler::WriteBE16(uint8_t* buf, int off, uint16_t val)
{
    buf[off] = static_cast<uint8_t>(val >> 8);
    buf[off + 1] = static_cast<uint8_t>(val & 0xFF);
}

void VirtualNetworkHandler::WriteBE32(uint8_t* buf, int off, uint32_t val)
{
    buf[off]     = static_cast<uint8_t>(val >> 24);
    buf[off + 1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[off + 2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[off + 3] = static_cast<uint8_t>(val & 0xFF);
}

uint16_t VirtualNetworkHandler::ReadBE16(const uint8_t* buf, int off)
{
    return static_cast<uint16_t>((buf[off] << 8) | buf[off + 1]);
}

uint32_t VirtualNetworkHandler::ReadBE32(const uint8_t* buf, int off)
{
    return static_cast<uint32_t>((buf[off] << 24) | (buf[off + 1] << 16) |
                                  (buf[off + 2] << 8) | buf[off + 3]);
}

} // namespace Em68030::IO
