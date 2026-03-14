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
#include <array>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>
#include <functional>

#include <windows.h>

#include "INetworkHandler.h"

namespace Em68030::IO {

/// Information about a TAP-Windows adapter found on the system.
struct TapAdapterInfo {
    std::string Guid;        // NetCfgInstanceId (e.g. "{A1B2C3D4-...}")
    std::string Name;        // Friendly name from Network\Connection
    std::string Description; // DriverDesc from the adapter class key
};

/// Bridge network handler using a TAP-Windows adapter.
/// Sends and receives raw Ethernet frames via the TAP device, which the
/// user bridges to a physical NIC in Windows Network Connections.
class TapNetworkHandler : public INetworkHandler {
public:
    explicit TapNetworkHandler(const std::string& adapterGuid);
    ~TapNetworkHandler() override;

    // INetworkHandler
    void ProcessPacket(const uint8_t* frame, int length) override;
    bool HasPendingPacket() const override;
    std::vector<uint8_t> DequeuePacket() override;
    void SetGuestMac(const std::array<uint8_t, 6>& mac) override;
    void Reset() override;

    bool IsConnected() const { return m_tapHandle != INVALID_HANDLE_VALUE; }

    /// Enumerate all TAP-Windows adapters on the system.
    /// Returns an empty vector if no TAP adapters are installed.
    static std::vector<TapAdapterInfo> EnumerateAdapters();

    std::function<void(const std::string&)> DiagnosticOutput;

private:
    void ReadLoop();
    void SetMediaStatus(bool up);

    HANDLE m_tapHandle = INVALID_HANDLE_VALUE;
    std::atomic<bool> m_disposed{false};
    std::thread m_readThread;

    mutable std::mutex m_rxMutex;
    std::queue<std::vector<uint8_t>> m_rxQueue;

    HANDLE m_readEvent = nullptr;
    HANDLE m_writeEvent = nullptr;

    std::array<uint8_t, 6> m_guestMac{};
};

} // namespace Em68030::IO
