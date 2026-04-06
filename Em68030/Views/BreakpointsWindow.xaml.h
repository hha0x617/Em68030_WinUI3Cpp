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

#include "BreakpointsWindow.g.h"

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace winrt::Em68030::implementation
{
    // Forward declarations (defined in MainViewModel.h)
    struct BreakpointData;
    struct WatchpointData;
    enum class WatchpointType;
    enum class WatchpointSize;

    struct BreakpointsWindow : BreakpointsWindowT<BreakpointsWindow>
    {
        BreakpointsWindow();

        /// Refresh the breakpoint list from AllBreakpoints data.
        void RefreshList(
            const std::unordered_map<uint32_t, struct BreakpointData>& breakpoints,
            const std::unordered_map<uint32_t, struct WatchpointData>& watchpoints);

        // Callbacks (set by MainWindow)
        std::function<void(uint32_t, bool)> OnToggleEnabled;
        std::function<void(uint32_t)> OnDelete;
        std::function<void(uint32_t, const std::string&)> OnSetCondition;
        std::function<void()> OnClearAll;
        std::function<void(uint32_t)> OnDoubleClick;

        // Watchpoint callbacks
        std::function<void(uint32_t, bool)> OnToggleWatchpointEnabled;
        std::function<void(uint32_t)> OnDeleteWatchpoint;
        std::function<void()> OnClearAllWatchpoints;
        std::function<void(uint32_t, WatchpointSize, WatchpointType, const std::string&)> OnAddWatchpoint;
        std::function<void(uint32_t, const std::string&)> OnSetWatchpointCondition;

    private:
        winrt::fire_and_forget ShowAddWatchpointDialog();
        winrt::fire_and_forget ShowEditConditionDialog(uint32_t addr, std::string currentCondition);
        winrt::fire_and_forget ShowEditWatchpointConditionDialog(uint32_t addr, std::string currentCondition);
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct BreakpointsWindow : BreakpointsWindowT<BreakpointsWindow, implementation::BreakpointsWindow>
    {
    };
}
