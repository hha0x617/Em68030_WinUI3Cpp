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
#include <gtest/gtest.h>

#include "IO/TapNetworkHandler.h"

namespace Em68030::Tests {

// ============================================================================
// Construction with empty/invalid GUID (no TAP device)
// ============================================================================

TEST(TapNetworkHandlerTest, EmptyGuid_IsNotConnected) {
    IO::TapNetworkHandler handler("");
    EXPECT_FALSE(handler.IsConnected());
}

TEST(TapNetworkHandlerTest, InvalidGuid_IsNotConnected) {
    IO::TapNetworkHandler handler("{00000000-0000-0000-0000-000000000000}");
    EXPECT_FALSE(handler.IsConnected());
}

// ============================================================================
// Safe operations when not connected
// ============================================================================

TEST(TapNetworkHandlerTest, ProcessPacket_WhenNotConnected_DoesNotCrash) {
    IO::TapNetworkHandler handler("");
    uint8_t frame[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                        0x08, 0x00, 0x3E, 0x21, 0x00, 0x00,
                        0x08, 0x06 };
    handler.ProcessPacket(frame, sizeof(frame));
    // No crash expected
}

TEST(TapNetworkHandlerTest, HasPendingPacket_WhenNotConnected_ReturnsFalse) {
    IO::TapNetworkHandler handler("");
    EXPECT_FALSE(handler.HasPendingPacket());
}

TEST(TapNetworkHandlerTest, DequeuePacket_WhenNotConnected_ReturnsEmpty) {
    IO::TapNetworkHandler handler("");
    auto pkt = handler.DequeuePacket();
    EXPECT_TRUE(pkt.empty());
}

TEST(TapNetworkHandlerTest, Reset_WhenNotConnected_DoesNotCrash) {
    IO::TapNetworkHandler handler("");
    handler.Reset();
}

TEST(TapNetworkHandlerTest, SetGuestMac_WhenNotConnected_DoesNotCrash) {
    IO::TapNetworkHandler handler("");
    std::array<uint8_t, 6> mac = { 0x08, 0x00, 0x3E, 0x21, 0x00, 0x00 };
    handler.SetGuestMac(mac);
}

// ============================================================================
// Adapter enumeration
// ============================================================================

TEST(TapNetworkHandlerTest, EnumerateAdapters_DoesNotCrash) {
    // Should return a list (possibly empty if TAP is not installed)
    auto adapters = IO::TapNetworkHandler::EnumerateAdapters();
    // Each adapter should have a non-empty GUID
    for (const auto& adapter : adapters) {
        EXPECT_FALSE(adapter.Guid.empty());
    }
}

TEST(TapNetworkHandlerTest, EnumerateAdapters_GuidFormat) {
    auto adapters = IO::TapNetworkHandler::EnumerateAdapters();
    for (const auto& adapter : adapters) {
        // GUID should start with '{' and end with '}'
        if (!adapter.Guid.empty()) {
            EXPECT_EQ('{', adapter.Guid.front());
            EXPECT_EQ('}', adapter.Guid.back());
        }
    }
}

} // namespace Em68030::Tests
