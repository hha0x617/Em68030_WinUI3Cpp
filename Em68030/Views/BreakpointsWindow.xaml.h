#pragma once

#include "BreakpointsWindow.g.h"

#include <functional>
#include <cstdint>
#include <unordered_map>

namespace winrt::Em68030::implementation
{
    struct BreakpointsWindow : BreakpointsWindowT<BreakpointsWindow>
    {
        BreakpointsWindow();

        /// Refresh the breakpoint list from AllBreakpoints data.
        void RefreshList(
            const std::unordered_map<uint32_t, struct BreakpointData>& breakpoints);

        // Callbacks (set by MainWindow)
        std::function<void(uint32_t, bool)> OnToggleEnabled;
        std::function<void(uint32_t)> OnDelete;
        std::function<void()> OnClearAll;
        std::function<void(uint32_t)> OnDoubleClick;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct BreakpointsWindow : BreakpointsWindowT<BreakpointsWindow, implementation::BreakpointsWindow>
    {
    };
}
