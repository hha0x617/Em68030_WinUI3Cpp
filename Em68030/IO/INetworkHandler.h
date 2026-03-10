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

namespace Em68030::IO {

/// Interface for network handler backends used by the LANCE ethernet controller.
/// Implementations process outgoing packets and produce incoming packets for the guest.
class INetworkHandler {
public:
    virtual ~INetworkHandler() = default;

    virtual void ProcessPacket(const uint8_t* frame, int length) = 0;
    virtual bool HasPendingPacket() const = 0;
    virtual std::vector<uint8_t> DequeuePacket() = 0;
    virtual void SetGuestMac(const std::array<uint8_t, 6>& mac) = 0;
    virtual void Reset() = 0;
};

} // namespace Em68030::IO
