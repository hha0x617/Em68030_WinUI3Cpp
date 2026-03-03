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
