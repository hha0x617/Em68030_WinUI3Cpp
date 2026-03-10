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
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>

namespace Em68030::IO::NetworkUtils {

static constexpr uint16_t ETHERTYPE_ARP  = 0x0806;
static constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
static constexpr uint8_t IP_PROTO_ICMP = 1;
static constexpr uint8_t IP_PROTO_TCP  = 6;
static constexpr uint8_t IP_PROTO_UDP  = 17;
static constexpr int MIN_ETHERNET_FRAME = 60;

inline void WriteBE16(uint8_t* buf, int off, uint16_t val)
{
    buf[off]     = static_cast<uint8_t>(val >> 8);
    buf[off + 1] = static_cast<uint8_t>(val & 0xFF);
}

inline void WriteBE32(uint8_t* buf, int off, uint32_t val)
{
    buf[off]     = static_cast<uint8_t>(val >> 24);
    buf[off + 1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[off + 2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[off + 3] = static_cast<uint8_t>(val & 0xFF);
}

inline uint16_t ReadBE16(const uint8_t* buf, int off)
{
    return static_cast<uint16_t>((buf[off] << 8) | buf[off + 1]);
}

inline uint32_t ReadBE32(const uint8_t* buf, int off)
{
    return static_cast<uint32_t>((buf[off] << 24) | (buf[off + 1] << 16) |
                                  (buf[off + 2] << 8) | buf[off + 3]);
}

inline uint16_t ComputeChecksum(const uint8_t* data, int length)
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

/// Compute TCP or UDP checksum with pseudo-header.
inline uint16_t ComputeTransportChecksum(const uint8_t* srcIp, const uint8_t* dstIp,
    uint8_t protocol, const uint8_t* segment, int segLen)
{
    std::vector<uint8_t> pseudo(12 + segLen, 0);
    std::memcpy(pseudo.data(), srcIp, 4);
    std::memcpy(pseudo.data() + 4, dstIp, 4);
    pseudo[8] = 0;
    pseudo[9] = protocol;
    WriteBE16(pseudo.data(), 10, static_cast<uint16_t>(segLen));
    std::memcpy(pseudo.data() + 12, segment, segLen);
    return ComputeChecksum(pseudo.data(), static_cast<int>(pseudo.size()));
}

/// Build an Ethernet + IPv4 header. Returns the packet buffer.
inline std::vector<uint8_t> BuildIpPacket(const uint8_t* dstMac, const uint8_t* srcMac,
    const uint8_t* srcIp, const uint8_t* dstIp, uint8_t protocol, int payloadLen)
{
    int ipTotalLen = 20 + payloadLen;
    int frameLen = std::max(14 + ipTotalLen, MIN_ETHERNET_FRAME);

    std::vector<uint8_t> pkt(frameLen, 0);

    // Ethernet header
    std::memcpy(pkt.data(), dstMac, 6);
    std::memcpy(pkt.data() + 6, srcMac, 6);
    WriteBE16(pkt.data(), 12, ETHERTYPE_IPV4);

    // IP header
    pkt[14] = 0x45; // version=4, IHL=5
    WriteBE16(pkt.data(), 16, static_cast<uint16_t>(ipTotalLen));
    WriteBE16(pkt.data(), 20, 0x4000); // Don't Fragment
    pkt[22] = 64; // TTL
    pkt[23] = protocol;
    std::memcpy(pkt.data() + 26, srcIp, 4);
    std::memcpy(pkt.data() + 30, dstIp, 4);

    // IP checksum
    uint16_t ipCsum = ComputeChecksum(pkt.data() + 14, 20);
    WriteBE16(pkt.data(), 24, ipCsum);

    return pkt;
}

/// Build an ARP reply packet.
inline std::vector<uint8_t> BuildArpReply(const uint8_t* dstMac, const uint8_t* srcMac,
    const uint8_t* srcIp, const uint8_t* dstIp)
{
    std::vector<uint8_t> reply(MIN_ETHERNET_FRAME, 0);

    // Ethernet header
    std::memcpy(reply.data(), dstMac, 6);
    std::memcpy(reply.data() + 6, srcMac, 6);
    WriteBE16(reply.data(), 12, ETHERTYPE_ARP);

    // ARP header
    WriteBE16(reply.data(), 14, 0x0001); // hardware type: Ethernet
    WriteBE16(reply.data(), 16, 0x0800); // protocol type: IPv4
    reply[18] = 6;                        // hardware size
    reply[19] = 4;                        // protocol size
    WriteBE16(reply.data(), 20, 0x0002); // opcode: Reply

    // Sender
    std::memcpy(reply.data() + 22, srcMac, 6);
    std::memcpy(reply.data() + 28, srcIp, 4);

    // Target
    std::memcpy(reply.data() + 32, dstMac, 6);
    std::memcpy(reply.data() + 38, dstIp, 4);

    return reply;
}

} // namespace Em68030::IO::NetworkUtils
