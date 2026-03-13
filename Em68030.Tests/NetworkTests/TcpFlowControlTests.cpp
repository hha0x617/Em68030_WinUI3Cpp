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
#include "gtest/gtest.h"

#include "../Em68030/IO/SlirpNetworkHandler.h"
#include "../Em68030/IO/NetworkUtils.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <chrono>
#include <vector>

using namespace Em68030::IO;

/// Tests for TCP flow control: the proxy must respect the guest's advertised
/// TCP window and not send more data than the guest can accept. Without this,
/// large downloads (e.g. curl) stall after ~140 KB because the guest drops
/// packets and the proxy has no retransmission mechanism.
class TcpFlowControlTest : public ::testing::Test {
protected:
    static constexpr std::array<uint8_t, 6> GuestMac = {0x08, 0x00, 0x3E, 0x21, 0x00, 0x00};
    static constexpr std::array<uint8_t, 4> GuestIp = {10, 0, 2, 15};
    static constexpr std::array<uint8_t, 4> LoopbackIp = {127, 0, 0, 1};
    static constexpr uint16_t GuestPort = 40000;

    SlirpNetworkHandler handler;
    SOCKET listenSock = INVALID_SOCKET;
    uint16_t serverPort = 0;

    void SetUp() override {
        handler.SetGuestMac(GuestMac);
        LearnGuestIp();
        StartServer();
    }

    void TearDown() override {
        handler.Reset();
        if (listenSock != INVALID_SOCKET) {
            closesocket(listenSock);
            listenSock = INVALID_SOCKET;
        }
    }

    void LearnGuestIp() {
        // Send an ARP request so the handler learns the guest IP
        std::vector<uint8_t> arp(60, 0);
        std::memset(arp.data(), 0xFF, 6);
        std::memcpy(arp.data() + 6, GuestMac.data(), 6);
        arp[12] = 0x08; arp[13] = 0x06;
        arp[14] = 0x00; arp[15] = 0x01;
        arp[16] = 0x08; arp[17] = 0x00;
        arp[18] = 6; arp[19] = 4;
        arp[20] = 0x00; arp[21] = 0x01;
        std::memcpy(arp.data() + 22, GuestMac.data(), 6);
        std::memcpy(arp.data() + 28, GuestIp.data(), 4);
        std::memcpy(arp.data() + 38, LoopbackIp.data(), 4);
        handler.ProcessPacket(arp.data(), 60);
        while (handler.HasPendingPacket()) handler.DequeuePacket();
    }

    void StartServer() {
        listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ASSERT_NE(listenSock, INVALID_SOCKET);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ASSERT_EQ(bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        ASSERT_EQ(listen(listenSock, 1), 0);
        int addrLen = sizeof(addr);
        getsockname(listenSock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        serverPort = ntohs(addr.sin_port);
    }

    // Build a TCP packet as if sent by the guest
    std::vector<uint8_t> BuildGuestTcp(uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window) {
        std::vector<uint8_t> f(60, 0);
        // Ethernet
        const uint8_t gwMac[] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
        std::memcpy(f.data(), gwMac, 6);
        std::memcpy(f.data() + 6, GuestMac.data(), 6);
        f[12] = 0x08; f[13] = 0x00;
        // IP (20 bytes)
        f[14] = 0x45;
        NetworkUtils::WriteBE16(f.data(), 16, 40); // total len = 20 IP + 20 TCP
        f[20] = 0x40; // DF
        f[22] = 64; // TTL
        f[23] = 6;  // TCP
        std::memcpy(f.data() + 26, GuestIp.data(), 4);
        std::memcpy(f.data() + 30, LoopbackIp.data(), 4);
        uint16_t ipCsum = NetworkUtils::ComputeChecksum(f.data() + 14, 20);
        NetworkUtils::WriteBE16(f.data(), 24, ipCsum);
        // TCP (20 bytes at offset 34)
        NetworkUtils::WriteBE16(f.data(), 34, GuestPort);
        NetworkUtils::WriteBE16(f.data(), 36, serverPort);
        NetworkUtils::WriteBE32(f.data(), 38, seq);
        NetworkUtils::WriteBE32(f.data(), 42, ack);
        f[46] = 0x50; // data offset = 5
        f[47] = flags;
        NetworkUtils::WriteBE16(f.data(), 48, window);
        uint16_t tcpCsum = NetworkUtils::ComputeTransportChecksum(
            GuestIp.data(), LoopbackIp.data(), 6, f.data() + 34, 20);
        NetworkUtils::WriteBE16(f.data(), 50, tcpCsum);
        return f;
    }

    bool WaitForPacket(int timeoutMs = 5000) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < end) {
            if (handler.HasPendingPacket()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    // TCP 3-way handshake; returns proxy's seq after SYN and the accepted client socket
    uint32_t Handshake(uint16_t window, SOCKET& clientSock) {
        // SYN
        auto syn = BuildGuestTcp(1000, 0, 0x02, window);
        handler.ProcessPacket(syn.data(), static_cast<int>(syn.size()));

        // Wait for SYN+ACK (proxy has connected by this point)
        EXPECT_TRUE(WaitForPacket(5000));
        auto synAck = handler.DequeuePacket();
        uint32_t proxySeq = NetworkUtils::ReadBE32(synAck.data(), 38);

        // Accept the server-side connection
        clientSock = accept(listenSock, nullptr, nullptr);
        EXPECT_NE(clientSock, INVALID_SOCKET);

        // ACK to complete handshake (advertise the test window)
        auto ack = BuildGuestTcp(1001, proxySeq + 1, 0x10, window);
        handler.ProcessPacket(ack.data(), static_cast<int>(ack.size()));

        // Allow state transition and receive loop to start
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return proxySeq + 1; // proxy seq after SYN consumed
    }

    // Drain all data packets within timeout, return total payload bytes
    int DrainDataBytes(int timeoutMs) {
        int totalBytes = 0;
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < end) {
            if (handler.HasPendingPacket()) {
                auto pkt = handler.DequeuePacket();
                if (pkt.size() >= 54) {
                    uint16_t ipTotal = NetworkUtils::ReadBE16(pkt.data(), 16);
                    int dataLen = ipTotal - 40; // 20 IP + 20 TCP header
                    if (dataLen > 0)
                        totalBytes += dataLen;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        return totalBytes;
    }
};

// When the guest advertises a small TCP window (2920 = 2 MSS), the proxy must
// not send more than that many bytes without receiving an ACK first.
// Without flow control, 50 KB of server data would all be forwarded immediately.
TEST_F(TcpFlowControlTest, SmallWindowLimitsDataSent) {
    SOCKET clientSock = INVALID_SOCKET;
    Handshake(2920, clientSock);

    // Server sends a large payload
    std::vector<char> data(50000, 'X');
    send(clientSock, data.data(), static_cast<int>(data.size()), 0);

    // Wait without sending any ACKs — proxy should stop after filling the window
    int received = DrainDataBytes(3000);

    EXPECT_LE(received, 2920) << "Proxy sent more data than window allows";
    EXPECT_GT(received, 0) << "Proxy should send at least some data";

    closesocket(clientSock);
}

// After the window fills, an ACK from the guest should open the window and
// allow the proxy to resume sending data.
TEST_F(TcpFlowControlTest, AckOpensWindowForMoreData) {
    SOCKET clientSock = INVALID_SOCKET;
    uint32_t proxySeq = Handshake(1460, clientSock);

    std::vector<char> data(50000, 'Y');
    send(clientSock, data.data(), static_cast<int>(data.size()), 0);

    // First batch: limited by window=1460
    int first = DrainDataBytes(2000);
    EXPECT_LE(first, 1460);
    EXPECT_GT(first, 0);

    // ACK the received data → opens window for more
    auto ack = BuildGuestTcp(1001, proxySeq + static_cast<uint32_t>(first), 0x10, 1460);
    handler.ProcessPacket(ack.data(), static_cast<int>(ack.size()));

    // Should receive more data after ACK
    int second = DrainDataBytes(2000);
    EXPECT_GT(second, 0) << "Proxy should resume sending after ACK opens window";

    closesocket(clientSock);
}

// A zero window means the guest cannot accept any data. The proxy must not
// send any data packets until the guest opens the window.
TEST_F(TcpFlowControlTest, ZeroWindowPreventsDataSending) {
    SOCKET clientSock = INVALID_SOCKET;
    Handshake(0, clientSock);

    std::vector<char> data(10000, 'Z');
    send(clientSock, data.data(), static_cast<int>(data.size()), 0);

    int received = DrainDataBytes(2000);
    EXPECT_EQ(received, 0) << "Proxy must not send data when guest window is zero";

    closesocket(clientSock);
}
