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
#include "Views/CallStackWindow.xaml.h"
#if __has_include("CallStackWindow.g.cpp")
#include "CallStackWindow.g.cpp"
#endif

#include "Helpers/ResourceHelper.h"
#include "ViewModels/MainViewModel.h"

#include <format>

using ::Em68030::ResourceHelper;
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// ============================================================================
// XAML template implementations
// ============================================================================
namespace winrt::Em68030::implementation
{
    template <typename D, typename... I>
    void CallStackWindowT<D, I...>::InitializeComponent()
    {
        if (!_contentLoaded)
        {
            _contentLoaded = true;
            ::winrt::Windows::Foundation::Uri resourceLocator{ L"ms-appx:///Views/CallStackWindow.xaml" };
            ::winrt::Microsoft::UI::Xaml::Application::LoadComponent(*this, resourceLocator);
        }
    }

    template <typename D, typename... I>
    void CallStackWindowT<D, I...>::Connect(int32_t, IInspectable const&) { _contentLoaded = true; }

    template <typename D, typename... I>
    ::winrt::Microsoft::UI::Xaml::Markup::IComponentConnector
    CallStackWindowT<D, I...>::GetBindingConnector(int32_t, IInspectable const&) { return nullptr; }

    template <typename D, typename... I>
    void CallStackWindowT<D, I...>::UnloadObject(::winrt::Microsoft::UI::Xaml::DependencyObject const&)
    { throw ::winrt::hresult_not_implemented(); }

    template <typename D, typename... I>
    void CallStackWindowT<D, I...>::DisconnectUnloadedObject(int32_t) {}
}

namespace winrt::Em68030::implementation
{
    CallStackWindow::CallStackWindow()
    {
        InitializeComponent();
        AppWindow().SetIcon(L"Assets/Em68030.ico");

        if (auto root = Content().try_as<FrameworkElement>())
        {
            CallStackList(root.FindName(L"CallStackList").try_as<ListView>());
        }

        Title(ResourceHelper::GetString(L"Window_CallStack"));
        AppWindow().Resize({ 420, 400 });
        // Title will be refined by MainWindow calling SetMode() right after construction.

        // Double-click to navigate to address
        if (CallStackList())
        {
            CallStackList().DoubleTapped([this](auto&&, auto&&)
            {
                if (!CallStackList()) return;
                auto selected = CallStackList().SelectedItem();
                if (!selected) return;
                if (auto grid = selected.try_as<Grid>())
                {
                    auto tag = grid.Tag();
                    if (tag)
                    {
                        auto addr = winrt::unbox_value<uint32_t>(tag);
                        if (OnNavigateToAddress) OnNavigateToAddress(addr);
                    }
                }
            });
        }
    }

    void CallStackWindow::SetMode(const std::string& callStackMode)
    {
        // Append the active mode to the window title so the user can see at
        // a glance which algorithm the Call Stack window is using.
        auto base = ResourceHelper::GetString(L"Window_CallStack");
        auto modeLabel = (callStackMode == "A6Chain")
            ? ResourceHelper::GetString(L"CallStack_TitleModeA6")
            : ResourceHelper::GetString(L"CallStack_TitleModeShadow");
        Title(winrt::hstring(std::wstring(base) + L"  [" + std::wstring(modeLabel) + L"]"));
    }

    void CallStackWindow::RefreshList(const std::vector<CallStackEntry>& entries, bool isRunning)
    {
        if (!CallStackList()) return;

        auto items = CallStackList().Items();
        items.Clear();

        auto res = [](const wchar_t* key) -> Microsoft::UI::Xaml::Media::Brush {
            return ::Em68030::ResourceHelper::GetThemeBrush(key);
        };

        if (isRunning)
        {
            TextBlock runText;
            runText.Text(ResourceHelper::GetString(L"CallStack_Running"));
            runText.Foreground(res(L"ThemeDisabledFg"));
            runText.FontSize(13);
            runText.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(8, 8, 0, 0));
            items.Append(runText);
            return;
        }

        if (entries.empty())
        {
            TextBlock emptyText;
            emptyText.Text(ResourceHelper::GetString(L"CallStack_Empty"));
            emptyText.Foreground(res(L"ThemeDisabledFg"));
            emptyText.FontSize(13);
            emptyText.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(8, 8, 0, 0));
            items.Append(emptyText);
            return;
        }

        auto consolasFont = Microsoft::UI::Xaml::Media::FontFamily(L"Consolas");
        auto addrFg = res(L"ThemeForeground");
        auto currentFg = res(L"ThemeCurrentLineFg");
        auto fpFg = res(L"ThemeFpInfoFg");
        auto heuristicFg = res(L"ThemeHeuristicFg");

        for (size_t idx = 0; idx < entries.size(); idx++)
        {
            const auto& entry = entries[idx];

            Grid row;
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(32));
            row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromPixels(110));
            row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            row.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 2, 4, 2));
            row.Tag(winrt::box_value(entry.address));
            row.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
                Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));

            // Frame index
            TextBlock idxText;
            idxText.FontFamily(consolasFont);
            idxText.FontSize(13);
            idxText.Foreground(idx == 0 ? currentFg : addrFg);
            idxText.Text(winrt::to_hstring(std::format("#{}", idx)));
            Grid::SetColumn(idxText, 0);

            // Address
            TextBlock addrText;
            addrText.FontFamily(consolasFont);
            addrText.FontSize(13);
            bool isHeuristic = (entry.label == "?");
            addrText.Foreground(idx == 0 ? currentFg : (isHeuristic ? heuristicFg : addrFg));
            addrText.Text(winrt::to_hstring(std::format("${:08X}", entry.address)));
            Grid::SetColumn(addrText, 1);

            // Info: frame pointer or label
            TextBlock infoText;
            infoText.FontFamily(consolasFont);
            infoText.FontSize(11);
            infoText.VerticalAlignment(VerticalAlignment::Center);
            if (idx == 0)
            {
                infoText.Text(winrt::to_hstring(std::format("PC  A6=${:08X}", entry.framePointer)));
                infoText.Foreground(currentFg);
            }
            else if (isHeuristic)
            {
                infoText.Text(L"(heuristic)");
                infoText.Foreground(heuristicFg);
            }
            else
            {
                infoText.Text(winrt::to_hstring(std::format("FP=${:08X}", entry.framePointer)));
                infoText.Foreground(fpFg);
            }
            Grid::SetColumn(infoText, 2);

            row.Children().Append(idxText);
            row.Children().Append(addrText);
            row.Children().Append(infoText);

            items.Append(row);
        }
    }
}
