#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <functional>

#include "INetworkHandler.h"

namespace Em68030::IO {

/// User-mode NAT network handler. Routes guest packets to the host network
/// without requiring administrator privileges or external drivers.
/// Supports ARP (proxy), ICMP (ping), UDP (including DNS), and TCP.
class SlirpNetworkHandler : public INetworkHandler {
public:
    SlirpNetworkHandler();
    SlirpNetworkHandler(const std::array<uint8_t, 4>& gatewayIp,
                        const std::array<uint8_t, 6>& gatewayMac);
    ~SlirpNetworkHandler() override;

    void SetGuestMac(const std::array<uint8_t, 6>& mac) override;
    void ProcessPacket(const uint8_t* frame, int length) override;
    bool HasPendingPacket() const override;
    std::vector<uint8_t> DequeuePacket() override;
    void Reset() override;

    static std::array<uint8_t, 4> ParseIpAddress(const std::string& s);
    static std::array<uint8_t, 6> ParseMacAddress(const std::string& s);

private:
    std::array<uint8_t, 6> GatewayMac = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    std::array<uint8_t, 4> GatewayIp = { 10, 0, 2, 2 };

    // TCP flags
    static constexpr uint8_t TCP_FIN = 0x01;
    static constexpr uint8_t TCP_SYN = 0x02;
    static constexpr uint8_t TCP_RST = 0x04;
    static constexpr uint8_t TCP_PSH = 0x08;
    static constexpr uint8_t TCP_ACK = 0x10;

    enum class TcpState { Connecting, SynAckSent, Established, FinWait, Closed };

    struct UdpSession {
        uintptr_t Socket = ~(uintptr_t)0; // SOCKET as uintptr_t to avoid winsock header
        std::chrono::steady_clock::time_point LastActivity;
        uint16_t GuestPort;
        std::array<uint8_t, 4> DestIp;
    };

    struct TcpSession {
        uintptr_t Socket = ~(uintptr_t)0;
        TcpState State = TcpState::Connecting;
        uint32_t OurSeq = 0;
        uint32_t TheirSeq = 0;
        uint16_t GuestSrcPort = 0;
        uint16_t GuestDstPort = 0;
        std::array<uint8_t, 4> DestIp;
        std::chrono::steady_clock::time_point LastActivity;
        std::atomic<bool> ReceiveLoopRunning{ false };
        std::atomic<bool> Cancelled{ false };
    };

    // TCP session key
    struct TcpKey {
        uint16_t srcPort;
        uint16_t dstPort;
        uint32_t dstIp;
        bool operator==(const TcpKey& o) const {
            return srcPort == o.srcPort && dstPort == o.dstPort && dstIp == o.dstIp;
        }
    };
    struct TcpKeyHash {
        size_t operator()(const TcpKey& k) const {
            return std::hash<uint64_t>()(
                (uint64_t)k.srcPort << 48 | (uint64_t)k.dstPort << 32 | k.dstIp);
        }
    };

    void HandleArp(const uint8_t* frame, int length);
    void HandleIpv4(const uint8_t* frame, int length);
    void HandleIcmp(const uint8_t* frame, int length, int ipHeaderLen);
    void HandleUdp(const uint8_t* frame, int length, int ipHeaderLen);
    void HandleTcp(const uint8_t* frame, int length, int ipHeaderLen);

    void BuildIcmpReply(const uint8_t* fromIp, uint16_t id, uint16_t seq,
                        const uint8_t* payload, int payloadLen);
    void BuildIcmpUnreachable(const uint8_t* fromIp, const uint8_t* origFrame,
                              int ipHeaderLen, int origLen);
    void BuildUdpReply(const uint8_t* fromIp, uint16_t srcPort, uint16_t dstPort,
                       const uint8_t* payload, int payloadLen);
    std::vector<uint8_t> BuildTcpPacket(const uint8_t* destIp, uint16_t srcPort, uint16_t dstPort,
        uint32_t seq, uint32_t ack, uint8_t flags,
        const uint8_t* payload, int payloadLen);

    void StartTcpReceiveLoop(std::shared_ptr<TcpSession> session, TcpKey key);
    void CloseTcpSession(TcpSession& session);
    void CleanupUdpSessions(bool force);
    void CleanupTcpSessions(bool force);

    void EnqueuePacket(std::vector<uint8_t>&& pkt);

public:
    std::function<void(const std::string&)> DiagnosticOutput;

private:
    std::array<uint8_t, 6> m_guestMac{};
    std::array<uint8_t, 4> m_guestIp{};
    bool m_guestIpKnown = false;
    std::atomic<bool> m_disposed{ false };

    mutable std::mutex m_rxMutex;
    std::queue<std::vector<uint8_t>> m_rxQueue;

    std::mutex m_udpMutex;
    std::unordered_map<uint16_t, UdpSession> m_udpSessions;

    std::mutex m_tcpMutex;
    std::unordered_map<TcpKey, std::shared_ptr<TcpSession>, TcpKeyHash> m_tcpSessions;

    bool m_wsaInitialized = false;
};

} // namespace Em68030::IO
