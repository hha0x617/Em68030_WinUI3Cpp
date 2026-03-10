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

#include "SlirpNetworkHandler.h"
#include "NetworkUtils.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

#include <sstream>

namespace Em68030::IO {

static std::string FormatIp(const uint8_t* ip)
{
    return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
           std::to_string(ip[2]) + "." + std::to_string(ip[3]);
}

SlirpNetworkHandler::SlirpNetworkHandler()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0)
        m_wsaInitialized = true;
}

SlirpNetworkHandler::SlirpNetworkHandler(const std::array<uint8_t, 4>& gatewayIp,
                                         const std::array<uint8_t, 6>& gatewayMac)
    : SlirpNetworkHandler()
{
    GatewayIp = gatewayIp;
    GatewayMac = gatewayMac;
}

std::array<uint8_t, 4> SlirpNetworkHandler::ParseIpAddress(const std::string& s)
{
    std::array<uint8_t, 4> result = { 10, 0, 2, 2 }; // default
    unsigned int a, b, c, d;
    if (sscanf_s(s.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
        a <= 255 && b <= 255 && c <= 255 && d <= 255)
    {
        result = { (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d };
    }
    return result;
}

std::array<uint8_t, 6> SlirpNetworkHandler::ParseMacAddress(const std::string& s)
{
    std::array<uint8_t, 6> result = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 }; // default
    unsigned int m[6];
    if (sscanf_s(s.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6 &&
        m[0] <= 255 && m[1] <= 255 && m[2] <= 255 && m[3] <= 255 && m[4] <= 255 && m[5] <= 255)
    {
        for (int i = 0; i < 6; ++i) result[i] = (uint8_t)m[i];
    }
    return result;
}

SlirpNetworkHandler::~SlirpNetworkHandler()
{
    m_disposed = true;
    CleanupUdpSessions(true);
    CleanupTcpSessions(true);
    if (m_wsaInitialized)
        WSACleanup();
}

void SlirpNetworkHandler::SetGuestMac(const std::array<uint8_t, 6>& mac)
{
    m_guestMac = mac;
}

void SlirpNetworkHandler::ProcessPacket(const uint8_t* frame, int length)
{
    if (length < 14) return;

    uint16_t etherType = NetworkUtils::ReadBE16(frame, 12);
    if (DiagnosticOutput)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[NAT] TX len=%d etype=0x%04X\n", length, etherType);
        DiagnosticOutput(buf);
    }
    switch (etherType) {
        case NetworkUtils::ETHERTYPE_ARP:
            HandleArp(frame, length);
            break;
        case NetworkUtils::ETHERTYPE_IPV4:
            HandleIpv4(frame, length);
            break;
    }
}

bool SlirpNetworkHandler::HasPendingPacket() const
{
    std::lock_guard<std::mutex> lock(m_rxMutex);
    return !m_rxQueue.empty();
}

std::vector<uint8_t> SlirpNetworkHandler::DequeuePacket()
{
    std::lock_guard<std::mutex> lock(m_rxMutex);
    auto pkt = std::move(m_rxQueue.front());
    m_rxQueue.pop();
    if (DiagnosticOutput)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[NAT] RX dequeue len=%d remaining=%d\n",
                 static_cast<int>(pkt.size()), static_cast<int>(m_rxQueue.size()));
        DiagnosticOutput(buf);
    }
    return pkt;
}

void SlirpNetworkHandler::Reset()
{
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        while (!m_rxQueue.empty()) m_rxQueue.pop();
    }
    CleanupUdpSessions(true);
    CleanupTcpSessions(true);
}

void SlirpNetworkHandler::EnqueuePacket(std::vector<uint8_t>&& pkt)
{
    std::lock_guard<std::mutex> lock(m_rxMutex);
    m_rxQueue.push(std::move(pkt));
}

// =======================================================================
// ARP — Proxy ARP
// =======================================================================

void SlirpNetworkHandler::HandleArp(const uint8_t* frame, int length)
{
    if (length < 42) return;

    uint16_t op = NetworkUtils::ReadBE16(frame, 20);
    if (op != 1) return; // Only ARP Request

    // Ignore DAD probes (SPA = 0.0.0.0)
    if (frame[28] == 0 && frame[29] == 0 && frame[30] == 0 && frame[31] == 0)
        return;

    // Learn guest IP
    std::memcpy(m_guestIp.data(), frame + 28, 4);
    m_guestIpKnown = true;

    // Don't respond if TPA = guest IP (would cause DAD conflict)
    uint8_t targetIp[4];
    std::memcpy(targetIp, frame + 38, 4);
    if (std::memcmp(targetIp, m_guestIp.data(), 4) == 0)
        return;

    // Proxy ARP: respond with gateway MAC for any target IP
    auto reply = NetworkUtils::BuildArpReply(m_guestMac.data(), GatewayMac.data(),
                                              targetIp, m_guestIp.data());
    EnqueuePacket(std::move(reply));
}

// =======================================================================
// IPv4 dispatcher
// =======================================================================

void SlirpNetworkHandler::HandleIpv4(const uint8_t* frame, int length)
{
    if (length < 34) return;

    int ipHeaderLen = (frame[14] & 0x0F) * 4;
    if (length < 14 + ipHeaderLen) return;

    if (!m_guestIpKnown)
    {
        std::memcpy(m_guestIp.data(), frame + 26, 4);
        m_guestIpKnown = true;
    }

    uint8_t protocol = frame[23];
    switch (protocol) {
        case NetworkUtils::IP_PROTO_ICMP:
            HandleIcmp(frame, length, ipHeaderLen);
            break;
        case NetworkUtils::IP_PROTO_UDP:
            HandleUdp(frame, length, ipHeaderLen);
            break;
        case NetworkUtils::IP_PROTO_TCP:
            HandleTcp(frame, length, ipHeaderLen);
            break;
    }
}

// =======================================================================
// ICMP — Forward ping via IcmpSendEcho (Windows API)
// =======================================================================

void SlirpNetworkHandler::HandleIcmp(const uint8_t* frame, int length, int ipHeaderLen)
{
    int icmpOffset = 14 + ipHeaderLen;
    if (length < icmpOffset + 8) return;

    uint8_t icmpType = frame[icmpOffset];
    if (icmpType != 8) return; // Only Echo Request

    // Extract destination IP
    uint8_t destIp[4];
    std::memcpy(destIp, frame + 30, 4);

    uint16_t icmpId = NetworkUtils::ReadBE16(frame, icmpOffset + 4);
    uint16_t icmpSeq = NetworkUtils::ReadBE16(frame, icmpOffset + 6);
    if (DiagnosticOutput)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "[NAT] ICMP Echo Request to %s id=%u seq=%u\n",
                 FormatIp(destIp).c_str(), icmpId, icmpSeq);
        DiagnosticOutput(buf);
    }
    int payloadLen = length - icmpOffset - 8;

    std::vector<uint8_t> payload;
    if (payloadLen > 0)
    {
        payload.assign(frame + icmpOffset + 8, frame + icmpOffset + 8 + payloadLen);
    }

    // Gateway ping: respond locally without forwarding to host
    if (std::memcmp(destIp, GatewayIp.data(), 4) == 0)
    {
        BuildIcmpReply(destIp, icmpId, icmpSeq,
                       payload.data(), static_cast<int>(payload.size()));
        return;
    }

    // Copy data for async use
    std::array<uint8_t, 4> destIpArr;
    std::memcpy(destIpArr.data(), destIp, 4);

    // Store original frame data needed for unreachable response
    std::vector<uint8_t> origFrame(frame, frame + length);
    int origIpHeaderLen = ipHeaderLen;

    std::thread([this, destIpArr, icmpId, icmpSeq, payload = std::move(payload),
                 origFrame = std::move(origFrame), origIpHeaderLen]()
    {
        if (m_disposed) return;

        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE)
        {
            BuildIcmpUnreachable(destIpArr.data(), origFrame.data(), origIpHeaderLen,
                                 static_cast<int>(origFrame.size()));
            return;
        }

        uint32_t destAddr;
        std::memcpy(&destAddr, destIpArr.data(), 4); // Already in network byte order

        int replyBufSize = sizeof(ICMP_ECHO_REPLY) + static_cast<int>(payload.size()) + 8;
        std::vector<uint8_t> replyBuf(replyBufSize);

        DWORD ret = IcmpSendEcho(hIcmp, destAddr,
            const_cast<uint8_t*>(payload.data()), static_cast<WORD>(payload.size()),
            nullptr, replyBuf.data(), replyBufSize, 5000);

        if (ret > 0)
        {
            auto* echoReply = reinterpret_cast<ICMP_ECHO_REPLY*>(replyBuf.data());
            int replyDataLen = static_cast<int>(echoReply->DataSize);
            auto* replyData = reinterpret_cast<const uint8_t*>(echoReply->Data);
            if (DiagnosticOutput)
            {
                char buf[128];
                snprintf(buf, sizeof(buf), "[NAT] ICMP Echo Reply from %s id=%u seq=%u rtt=%ums\n",
                         FormatIp(destIpArr.data()).c_str(), icmpId, icmpSeq, echoReply->RoundTripTime);
                DiagnosticOutput(buf);
            }
            BuildIcmpReply(destIpArr.data(), icmpId, icmpSeq, replyData, replyDataLen);
        }
        else
        {
            if (DiagnosticOutput)
            {
                char buf[128];
                snprintf(buf, sizeof(buf), "[NAT] ICMP ping failed for %s err=%lu\n",
                         FormatIp(destIpArr.data()).c_str(), GetLastError());
                DiagnosticOutput(buf);
            }
            BuildIcmpUnreachable(destIpArr.data(), origFrame.data(), origIpHeaderLen,
                                 static_cast<int>(origFrame.size()));
        }

        IcmpCloseHandle(hIcmp);
    }).detach();
}

void SlirpNetworkHandler::BuildIcmpReply(const uint8_t* fromIp, uint16_t id, uint16_t seq,
                                          const uint8_t* payload, int payloadLen)
{
    int icmpLen = 8 + payloadLen;
    auto pkt = NetworkUtils::BuildIpPacket(m_guestMac.data(), GatewayMac.data(),
        fromIp, m_guestIp.data(), NetworkUtils::IP_PROTO_ICMP, icmpLen);

    int icmpOff = 34;
    pkt[icmpOff] = 0; // Echo Reply
    pkt[icmpOff + 1] = 0;
    NetworkUtils::WriteBE16(pkt.data(), icmpOff + 4, id);
    NetworkUtils::WriteBE16(pkt.data(), icmpOff + 6, seq);
    if (payload && payloadLen > 0)
        std::memcpy(pkt.data() + icmpOff + 8, payload, payloadLen);

    uint16_t csum = NetworkUtils::ComputeChecksum(pkt.data() + icmpOff, icmpLen);
    NetworkUtils::WriteBE16(pkt.data(), icmpOff + 2, csum);

    EnqueuePacket(std::move(pkt));
}

void SlirpNetworkHandler::BuildIcmpUnreachable(const uint8_t* fromIp,
    const uint8_t* origFrame, int ipHeaderLen, int origLen)
{
    int origDataLen = std::min(8, origLen - 14 - ipHeaderLen);
    if (origDataLen < 0) origDataLen = 0;
    int icmpPayloadLen = ipHeaderLen + origDataLen;
    int icmpLen = 8 + icmpPayloadLen;
    auto pkt = NetworkUtils::BuildIpPacket(m_guestMac.data(), GatewayMac.data(),
        fromIp, m_guestIp.data(), NetworkUtils::IP_PROTO_ICMP, icmpLen);

    int icmpOff = 34;
    pkt[icmpOff] = 3;     // Destination Unreachable
    pkt[icmpOff + 1] = 1; // Host Unreachable
    std::memcpy(pkt.data() + icmpOff + 8, origFrame + 14, icmpPayloadLen);

    uint16_t csum = NetworkUtils::ComputeChecksum(pkt.data() + icmpOff, icmpLen);
    NetworkUtils::WriteBE16(pkt.data(), icmpOff + 2, csum);

    EnqueuePacket(std::move(pkt));
}

// =======================================================================
// UDP — Forward via Winsock UDP
// =======================================================================

void SlirpNetworkHandler::HandleUdp(const uint8_t* frame, int length, int ipHeaderLen)
{
    int udpOffset = 14 + ipHeaderLen;
    if (length < udpOffset + 8) return;

    uint16_t srcPort = NetworkUtils::ReadBE16(frame, udpOffset);
    uint16_t dstPort = NetworkUtils::ReadBE16(frame, udpOffset + 2);
    uint16_t udpLen  = NetworkUtils::ReadBE16(frame, udpOffset + 4);

    int payloadLen = udpLen - 8;
    if (payloadLen < 0 || length < udpOffset + udpLen) return;

    uint8_t destIp[4];
    std::memcpy(destIp, frame + 30, 4);

    std::vector<uint8_t> payload;
    if (payloadLen > 0)
        payload.assign(frame + udpOffset + 8, frame + udpOffset + 8 + payloadLen);

    // Get or create UDP session
    SOCKET sock = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(m_udpMutex);
        auto it = m_udpSessions.find(srcPort);
        if (it == m_udpSessions.end())
        {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock == INVALID_SOCKET) return;

            // Set receive timeout
            DWORD timeout = 5000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));

            UdpSession session;
            session.Socket = static_cast<uintptr_t>(sock);
            session.LastActivity = std::chrono::steady_clock::now();
            session.GuestPort = srcPort;
            std::memcpy(session.DestIp.data(), destIp, 4);
            m_udpSessions[srcPort] = session;
        }
        else
        {
            sock = static_cast<SOCKET>(it->second.Socket);
            it->second.LastActivity = std::chrono::steady_clock::now();
            std::memcpy(it->second.DestIp.data(), destIp, 4);
        }
    }

    // Copy data for async use
    std::array<uint8_t, 4> destIpArr;
    std::memcpy(destIpArr.data(), destIp, 4);

    std::thread([this, sock, payload = std::move(payload), destIpArr, srcPort, dstPort]()
    {
        if (m_disposed) return;

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(dstPort);
        std::memcpy(&dest.sin_addr, destIpArr.data(), 4);

        sendto(sock, reinterpret_cast<const char*>(payload.data()),
               static_cast<int>(payload.size()), 0,
               reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

        // Wait for reply
        char recvBuf[65536];
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);

        if (recvLen > 0)
        {
            BuildUdpReply(destIpArr.data(), dstPort, srcPort,
                          reinterpret_cast<const uint8_t*>(recvBuf), recvLen);
        }

        CleanupUdpSessions(false);
    }).detach();
}

void SlirpNetworkHandler::BuildUdpReply(const uint8_t* fromIp, uint16_t srcPort,
    uint16_t dstPort, const uint8_t* payload, int payloadLen)
{
    int udpLen = 8 + payloadLen;
    auto pkt = NetworkUtils::BuildIpPacket(m_guestMac.data(), GatewayMac.data(),
        fromIp, m_guestIp.data(), NetworkUtils::IP_PROTO_UDP, udpLen);

    int udpOff = 34;
    NetworkUtils::WriteBE16(pkt.data(), udpOff, srcPort);
    NetworkUtils::WriteBE16(pkt.data(), udpOff + 2, dstPort);
    NetworkUtils::WriteBE16(pkt.data(), udpOff + 4, static_cast<uint16_t>(udpLen));
    if (payload && payloadLen > 0)
        std::memcpy(pkt.data() + udpOff + 8, payload, payloadLen);

    uint16_t csum = NetworkUtils::ComputeTransportChecksum(fromIp, m_guestIp.data(),
        NetworkUtils::IP_PROTO_UDP, pkt.data() + udpOff, udpLen);
    if (csum == 0) csum = 0xFFFF;
    NetworkUtils::WriteBE16(pkt.data(), udpOff + 6, csum);

    EnqueuePacket(std::move(pkt));
}

void SlirpNetworkHandler::CleanupUdpSessions(bool force)
{
    std::lock_guard<std::mutex> lock(m_udpMutex);
    auto now = std::chrono::steady_clock::now();
    std::vector<uint16_t> expired;
    for (auto& kv : m_udpSessions)
    {
        if (force || std::chrono::duration_cast<std::chrono::seconds>(
            now - kv.second.LastActivity).count() > 30)
        {
            expired.push_back(kv.first);
        }
    }
    for (auto port : expired)
    {
        auto& s = m_udpSessions[port];
        closesocket(static_cast<SOCKET>(s.Socket));
        m_udpSessions.erase(port);
    }
}

// =======================================================================
// TCP — Forward via Winsock TCP
// =======================================================================

void SlirpNetworkHandler::HandleTcp(const uint8_t* frame, int length, int ipHeaderLen)
{
    int tcpOffset = 14 + ipHeaderLen;
    if (length < tcpOffset + 20) return;

    uint16_t srcPort = NetworkUtils::ReadBE16(frame, tcpOffset);
    uint16_t dstPort = NetworkUtils::ReadBE16(frame, tcpOffset + 2);
    uint32_t seq = NetworkUtils::ReadBE32(frame, tcpOffset + 4);
    uint32_t ackNum = NetworkUtils::ReadBE32(frame, tcpOffset + 8);
    int tcpDataOffset = (frame[tcpOffset + 12] >> 4) * 4;
    uint8_t flags = frame[tcpOffset + 13];

    uint8_t destIp[4];
    std::memcpy(destIp, frame + 30, 4);
    uint32_t destIpU = NetworkUtils::ReadBE32(frame, 30);
    TcpKey key{ srcPort, dstPort, destIpU };

    // RST from guest
    if ((flags & TCP_RST) != 0)
    {
        std::lock_guard<std::mutex> lock(m_tcpMutex);
        auto it = m_tcpSessions.find(key);
        if (it != m_tcpSessions.end())
        {
            CloseTcpSession(*it->second);
            m_tcpSessions.erase(it);
        }
        return;
    }

    // SYN: new connection
    if ((flags & TCP_SYN) != 0 && (flags & TCP_ACK) == 0)
    {
        {
            std::lock_guard<std::mutex> lock(m_tcpMutex);
            auto it = m_tcpSessions.find(key);
            if (it != m_tcpSessions.end())
            {
                CloseTcpSession(*it->second);
                m_tcpSessions.erase(it);
            }
        }

        auto session = std::make_shared<TcpSession>();
        session->State = TcpState::Connecting;
        session->OurSeq = static_cast<uint32_t>(GetTickCount64() & 0x7FFFFFFF);
        session->TheirSeq = seq + 1;
        session->GuestSrcPort = srcPort;
        session->GuestDstPort = dstPort;
        std::memcpy(session->DestIp.data(), destIp, 4);
        session->LastActivity = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(m_tcpMutex);
            m_tcpSessions[key] = session;
        }

        // Connect asynchronously
        std::array<uint8_t, 4> destIpArr;
        std::memcpy(destIpArr.data(), destIp, 4);

        std::thread([this, session, key, destIpArr, srcPort, dstPort, seq]()
        {
            if (m_disposed) return;

            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET)
            {
                auto rst = BuildTcpPacket(destIpArr.data(), dstPort, srcPort,
                    0, seq + 1, TCP_RST | TCP_ACK, nullptr, 0);
                EnqueuePacket(std::move(rst));
                std::lock_guard<std::mutex> lock(m_tcpMutex);
                m_tcpSessions.erase(key);
                return;
            }

            // Set connect timeout via non-blocking + select
            u_long nonBlocking = 1;
            ioctlsocket(sock, FIONBIO, &nonBlocking);

            sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(dstPort);
            std::memcpy(&dest.sin_addr, destIpArr.data(), 4);

            connect(sock, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

            fd_set writeFds;
            FD_ZERO(&writeFds);
            FD_SET(sock, &writeFds);
            timeval tv{ 10, 0 }; // 10 second timeout

            int selResult = select(0, nullptr, &writeFds, nullptr, &tv);
            if (selResult <= 0)
            {
                closesocket(sock);
                auto rst = BuildTcpPacket(destIpArr.data(), dstPort, srcPort,
                    0, seq + 1, TCP_RST | TCP_ACK, nullptr, 0);
                EnqueuePacket(std::move(rst));
                std::lock_guard<std::mutex> lock(m_tcpMutex);
                m_tcpSessions.erase(key);
                return;
            }

            // Back to blocking mode
            nonBlocking = 0;
            ioctlsocket(sock, FIONBIO, &nonBlocking);

            // Set receive timeout
            DWORD rcvTimeout = 500;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));

            session->Socket = static_cast<uintptr_t>(sock);
            session->State = TcpState::SynAckSent;

            // Send SYN+ACK to guest
            auto synAck = BuildTcpPacket(session->DestIp.data(), dstPort, srcPort,
                session->OurSeq, session->TheirSeq, TCP_SYN | TCP_ACK, nullptr, 0);
            EnqueuePacket(std::move(synAck));
            session->OurSeq++; // SYN consumes 1 seq

            // Start receive loop
            StartTcpReceiveLoop(session, key);
        }).detach();

        return;
    }

    // Subsequent packets
    std::shared_ptr<TcpSession> sess;
    {
        std::lock_guard<std::mutex> lock(m_tcpMutex);
        auto it = m_tcpSessions.find(key);
        if (it == m_tcpSessions.end()) return;
        sess = it->second;
    }

    sess->LastActivity = std::chrono::steady_clock::now();

    // ACK of our SYN+ACK
    if (sess->State == TcpState::SynAckSent && (flags & TCP_ACK) != 0)
    {
        sess->State = TcpState::Established;
    }

    // FIN from guest
    if ((flags & TCP_FIN) != 0)
    {
        sess->TheirSeq = seq + 1;
        auto finAck = BuildTcpPacket(sess->DestIp.data(), dstPort, srcPort,
            sess->OurSeq, sess->TheirSeq, TCP_FIN | TCP_ACK, nullptr, 0);
        EnqueuePacket(std::move(finAck));
        sess->OurSeq++;

        {
            std::lock_guard<std::mutex> lock(m_tcpMutex);
            m_tcpSessions.erase(key);
        }
        CloseTcpSession(*sess);
        return;
    }

    // Data from guest
    if ((flags & TCP_ACK) != 0 && sess->State == TcpState::Established)
    {
        int dataLen = length - tcpOffset - tcpDataOffset;
        if (dataLen > 0 && sess->Socket != static_cast<uintptr_t>(INVALID_SOCKET))
        {
            sess->TheirSeq = seq + static_cast<uint32_t>(dataLen);

            SOCKET sock = static_cast<SOCKET>(sess->Socket);
            int sent = send(sock, reinterpret_cast<const char*>(frame + tcpOffset + tcpDataOffset),
                            dataLen, 0);
            if (sent == SOCKET_ERROR)
            {
                auto rst = BuildTcpPacket(sess->DestIp.data(), dstPort, srcPort,
                    sess->OurSeq, sess->TheirSeq, TCP_RST | TCP_ACK, nullptr, 0);
                EnqueuePacket(std::move(rst));
                std::lock_guard<std::mutex> lock(m_tcpMutex);
                m_tcpSessions.erase(key);
                CloseTcpSession(*sess);
                return;
            }

            // ACK the data
            auto ackPkt = BuildTcpPacket(sess->DestIp.data(), dstPort, srcPort,
                sess->OurSeq, sess->TheirSeq, TCP_ACK, nullptr, 0);
            EnqueuePacket(std::move(ackPkt));
        }
    }
}

void SlirpNetworkHandler::StartTcpReceiveLoop(std::shared_ptr<TcpSession> session, TcpKey key)
{
    if (session->ReceiveLoopRunning.exchange(true)) return;

    std::thread([this, session, key]()
    {
        char buffer[1460]; // MSS
        while (!m_disposed && !session->Cancelled &&
               (session->State == TcpState::SynAckSent || session->State == TcpState::Established))
        {
            SOCKET sock = static_cast<SOCKET>(session->Socket);
            if (sock == INVALID_SOCKET) break;

            int bytesRead = recv(sock, buffer, sizeof(buffer), 0);
            if (bytesRead == 0)
            {
                // Host closed connection
                auto fin = BuildTcpPacket(session->DestIp.data(),
                    session->GuestDstPort, session->GuestSrcPort,
                    session->OurSeq, session->TheirSeq, TCP_FIN | TCP_ACK, nullptr, 0);
                EnqueuePacket(std::move(fin));
                session->OurSeq++;
                session->State = TcpState::FinWait;
                break;
            }
            if (bytesRead < 0)
            {
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT)
                    continue; // Receive timeout — just retry
                break; // Real error
            }

            session->LastActivity = std::chrono::steady_clock::now();

            auto dataPkt = BuildTcpPacket(session->DestIp.data(),
                session->GuestDstPort, session->GuestSrcPort,
                session->OurSeq, session->TheirSeq,
                TCP_PSH | TCP_ACK,
                reinterpret_cast<const uint8_t*>(buffer), bytesRead);
            session->OurSeq += static_cast<uint32_t>(bytesRead);
            EnqueuePacket(std::move(dataPkt));
        }

        session->ReceiveLoopRunning = false;
    }).detach();
}

std::vector<uint8_t> SlirpNetworkHandler::BuildTcpPacket(const uint8_t* destIp,
    uint16_t srcPort, uint16_t dstPort, uint32_t seq, uint32_t ack,
    uint8_t flags, const uint8_t* payload, int payloadLen)
{
    int tcpHeaderLen = 20;
    int transportLen = tcpHeaderLen + payloadLen;
    auto pkt = NetworkUtils::BuildIpPacket(m_guestMac.data(), GatewayMac.data(),
        destIp, m_guestIp.data(), NetworkUtils::IP_PROTO_TCP, transportLen);

    // Fix IP total length
    int ipTotalLen = 20 + transportLen;
    NetworkUtils::WriteBE16(pkt.data(), 16, static_cast<uint16_t>(ipTotalLen));
    pkt[24] = 0; pkt[25] = 0;
    uint16_t ipCsum = NetworkUtils::ComputeChecksum(pkt.data() + 14, 20);
    NetworkUtils::WriteBE16(pkt.data(), 24, ipCsum);

    int tcpOff = 34;
    NetworkUtils::WriteBE16(pkt.data(), tcpOff, srcPort);
    NetworkUtils::WriteBE16(pkt.data(), tcpOff + 2, dstPort);
    NetworkUtils::WriteBE32(pkt.data(), tcpOff + 4, seq);
    NetworkUtils::WriteBE32(pkt.data(), tcpOff + 8, ack);
    pkt[tcpOff + 12] = 0x50; // data offset = 5
    pkt[tcpOff + 13] = flags;
    NetworkUtils::WriteBE16(pkt.data(), tcpOff + 14, 65535); // window size

    if (payload && payloadLen > 0)
        std::memcpy(pkt.data() + tcpOff + tcpHeaderLen, payload, payloadLen);

    uint16_t tcpCsum = NetworkUtils::ComputeTransportChecksum(destIp, m_guestIp.data(),
        NetworkUtils::IP_PROTO_TCP, pkt.data() + tcpOff, transportLen);
    NetworkUtils::WriteBE16(pkt.data(), tcpOff + 16, tcpCsum);

    return pkt;
}

void SlirpNetworkHandler::CloseTcpSession(TcpSession& session)
{
    session.State = TcpState::Closed;
    session.Cancelled = true;
    SOCKET sock = static_cast<SOCKET>(session.Socket);
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        session.Socket = static_cast<uintptr_t>(INVALID_SOCKET);
    }
}

void SlirpNetworkHandler::CleanupTcpSessions(bool force)
{
    std::lock_guard<std::mutex> lock(m_tcpMutex);
    auto now = std::chrono::steady_clock::now();
    std::vector<TcpKey> expired;
    for (auto& kv : m_tcpSessions)
    {
        if (force || std::chrono::duration_cast<std::chrono::seconds>(
            now - kv.second->LastActivity).count() > 120)
        {
            expired.push_back(kv.first);
        }
    }
    for (auto& k : expired)
    {
        CloseTcpSession(*m_tcpSessions[k]);
        m_tcpSessions.erase(k);
    }
}

} // namespace Em68030::IO
