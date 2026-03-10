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
#include "Views/BreakpointsWindow.xaml.h"
#if __has_include("BreakpointsWindow.g.cpp")
#include "BreakpointsWindow.g.cpp"
#endif

#include "Helpers/ResourceHelper.h"
#include "ViewModels/MainViewModel.h"

#include <format>
#include <vector>
#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// ============================================================================
// XAML template implementations
// ============================================================================
namespace winrt::Em68030::implementation
{
    template <typename D, typename... I>
    void BreakpointsWindowT<D, I...>::InitializeComponent()
    {
        if (!_contentLoaded)
        {
            _contentLoaded = true;
            ::winrt::Windows::Foundation::Uri resourceLocator{ L"ms-appx:///Views/BreakpointsWindow.xaml" };
            ::winrt::Microsoft::UI::Xaml::Application::LoadComponent(*this, resourceLocator);
        }
    }

    template <typename D, typename... I>
    void BreakpointsWindowT<D, I...>::Connect(int32_t /*connectionId*/, IInspectable const& /*target*/)
    {
        _contentLoaded = true;
    }

    template <typename D, typename... I>
    ::winrt::Microsoft::UI::Xaml::Markup::IComponentConnector
    BreakpointsWindowT<D, I...>::GetBindingConnector(int32_t /*connectionId*/, IInspectable const& /*target*/)
    {
        return nullptr;
    }

    template <typename D, typename... I>
    void BreakpointsWindowT<D, I...>::UnloadObject(::winrt::Microsoft::UI::Xaml::DependencyObject const&)
    {
        throw ::winrt::hresult_not_implemented();
    }

    template <typename D, typename... I>
    void BreakpointsWindowT<D, I...>::DisconnectUnloadedObject(int32_t)
    {
    }
}

namespace winrt::Em68030::implementation
{
    BreakpointsWindow::BreakpointsWindow()
    {
        InitializeComponent();
        AppWindow().SetIcon(L"Assets/Em68030.ico");

        // Resolve named elements
        if (auto root = Content().try_as<FrameworkElement>())
        {
            BreakpointList(root.FindName(L"BreakpointList").try_as<ListView>());
            ClearAllButton(root.FindName(L"ClearAllButton").try_as<Button>());
        }

        Title(ResourceHelper::GetString(L"Window_Breakpoints"));
        AppWindow().Resize({ 400, 450 });

        // Wire Clear All button
        if (ClearAllButton())
        {
            ClearAllButton().Click([this](auto&&, auto&&)
            {
                if (OnClearAll) OnClearAll();
            });
        }

        // Wire double-click on breakpoint list
        if (BreakpointList())
        {
            BreakpointList().DoubleTapped([this](auto&&, auto&&)
            {
                if (!BreakpointList()) return;
                auto selected = BreakpointList().SelectedItem();
                if (!selected) return;
                if (auto grid = selected.try_as<Grid>())
                {
                    auto tag = grid.Tag();
                    if (tag)
                    {
                        auto addr = winrt::unbox_value<uint32_t>(tag);
                        if (OnDoubleClick) OnDoubleClick(addr);
                    }
                }
            });
        }
    }

    void BreakpointsWindow::RefreshList(
        const std::unordered_map<uint32_t, BreakpointData>& breakpoints)
    {
        if (!BreakpointList()) return;

        auto items = BreakpointList().Items();
        items.Clear();

        // Sort breakpoints by address for stable display
        std::vector<std::pair<uint32_t, BreakpointData>> sorted(
            breakpoints.begin(), breakpoints.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        auto consolasFont = Microsoft::UI::Xaml::Media::FontFamily(L"Consolas");
        auto normalFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xD4, 0xD4, 0xD4 });
        auto deleteFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xFF, 0x60, 0x60 });

        for (const auto& [addr, bp] : sorted)
        {
            // Row: [CheckBox] [Address TextBlock] [Delete Button]
            Grid row;
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(40));
            row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::Auto());
            row.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 2, 4, 2));
            row.Tag(winrt::box_value(addr));

            // Enable checkbox
            CheckBox cb;
            cb.IsChecked(bp.enabled);
            cb.MinWidth(0);
            cb.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0, 0, 0, 0));
            cb.VerticalAlignment(VerticalAlignment::Center);
            uint32_t capturedAddr = addr;
            cb.Checked([this, capturedAddr](auto&&, auto&&)
            {
                if (OnToggleEnabled) OnToggleEnabled(capturedAddr, true);
            });
            cb.Unchecked([this, capturedAddr](auto&&, auto&&)
            {
                if (OnToggleEnabled) OnToggleEnabled(capturedAddr, false);
            });
            Grid::SetColumn(cb, 0);

            // Address text
            TextBlock addrText;
            addrText.FontFamily(consolasFont);
            addrText.FontSize(13);
            addrText.Foreground(normalFg);
            addrText.VerticalAlignment(VerticalAlignment::Center);
            auto addrStr = std::format("${:08X}", addr);
            addrText.Text(winrt::to_hstring(addrStr));
            Grid::SetColumn(addrText, 1);

            // Delete button
            Button delBtn;
            delBtn.Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_Delete")));
            delBtn.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
                Windows::UI::Color{ 0xFF, 0x3E, 0x3E, 0x42 }));
            delBtn.Foreground(deleteFg);
            delBtn.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush(
                Windows::UI::Color{ 0xFF, 0x55, 0x55, 0x55 }));
            delBtn.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(6, 2, 6, 2));
            delBtn.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(8, 0, 4, 0));
            delBtn.FontSize(11);
            delBtn.VerticalAlignment(VerticalAlignment::Center);
            delBtn.HorizontalAlignment(HorizontalAlignment::Right);
            delBtn.Click([this, capturedAddr](auto&&, auto&&)
            {
                if (OnDelete) OnDelete(capturedAddr);
            });
            Grid::SetColumn(delBtn, 2);

            row.Children().Append(cb);
            row.Children().Append(addrText);
            row.Children().Append(delBtn);

            items.Append(row);
        }
    }
}
