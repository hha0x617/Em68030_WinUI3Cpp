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

#include <winrt/Microsoft.UI.Text.h>
#include <format>
#include <vector>
#include <algorithm>

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
            AddWatchpointButton(root.FindName(L"AddWatchpointButton").try_as<Button>());
        }

        Title(ResourceHelper::GetString(L"Window_Breakpoints"));
        AppWindow().Resize({ 500, 500 });

        // Localize buttons
        if (ClearAllButton())
            ClearAllButton().Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_ClearAll")));
        if (AddWatchpointButton())
            AddWatchpointButton().Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_AddWatchpoint")));

        // Wire Clear All button
        if (ClearAllButton())
        {
            ClearAllButton().Click([this](auto&&, auto&&)
            {
                if (OnClearAll) OnClearAll();
                if (OnClearAllWatchpoints) OnClearAllWatchpoints();
            });
        }

        // Wire Add Watchpoint button
        if (AddWatchpointButton())
        {
            AddWatchpointButton().Click([this](auto&&, auto&&)
            {
                ShowAddWatchpointDialog();
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
        const std::unordered_map<uint32_t, BreakpointData>& breakpoints,
        const std::unordered_map<uint32_t, WatchpointData>& watchpoints)
    {
        if (!BreakpointList()) return;

        auto items = BreakpointList().Items();
        items.Clear();

        auto consolasFont = Microsoft::UI::Xaml::Media::FontFamily(L"Consolas");
        auto normalFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xD4, 0xD4, 0xD4 });
        auto condFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xB0, 0xB0, 0x80 });
        auto deleteFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xFF, 0x60, 0x60 });
        auto headerFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0x80, 0xB0, 0xFF });
        auto watchFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xFF, 0xB0, 0x60 });

        // ---- Breakpoints section ----
        if (!breakpoints.empty())
        {
            TextBlock bpHeader;
            bpHeader.Text(ResourceHelper::GetString(L"Breakpoints_SectionBreakpoints"));
            bpHeader.Foreground(headerFg);
            bpHeader.FontSize(12);
            bpHeader.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
            bpHeader.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(8, 4, 0, 2));
            items.Append(bpHeader);
        }

        // Sort breakpoints by address
        std::vector<std::pair<uint32_t, BreakpointData>> sortedBP(
            breakpoints.begin(), breakpoints.end());
        std::sort(sortedBP.begin(), sortedBP.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        auto editFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0x80, 0xC0, 0xFF });
        auto btnBg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0x3E, 0x3E, 0x42 });
        auto btnBorder = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0x55, 0x55, 0x55 });

        for (const auto& [addr, bp] : sortedBP)
        {
            // Row: [CheckBox] [Address + Condition] [Edit] [Delete]
            Grid row;
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(40));
            row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().GetAt(3).Width(GridLengthHelper::Auto());
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

            // Address + condition stack
            StackPanel textStack;
            textStack.Orientation(Orientation::Vertical);
            textStack.VerticalAlignment(VerticalAlignment::Center);

            TextBlock addrText;
            addrText.FontFamily(consolasFont);
            addrText.FontSize(13);
            addrText.Foreground(normalFg);
            auto addrStr = std::format("${:08X}", addr);
            addrText.Text(winrt::to_hstring(addrStr));
            textStack.Children().Append(addrText);

            if (!bp.condition.empty())
            {
                TextBlock condText;
                condText.FontFamily(consolasFont);
                condText.FontSize(11);
                condText.Foreground(condFg);
                auto condStr = std::format("  if {}", bp.condition);
                condText.Text(winrt::to_hstring(condStr));
                textStack.Children().Append(condText);
            }
            Grid::SetColumn(textStack, 1);

            // Edit condition button
            Button editBtn;
            editBtn.Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_EditCondition")));
            editBtn.Background(btnBg);
            editBtn.Foreground(editFg);
            editBtn.BorderBrush(btnBorder);
            editBtn.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(6, 2, 6, 2));
            editBtn.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 0, 0, 0));
            editBtn.FontSize(11);
            editBtn.VerticalAlignment(VerticalAlignment::Center);
            std::string capturedCond = bp.condition;
            editBtn.Click([this, capturedAddr, capturedCond](auto&&, auto&&)
            {
                ShowEditConditionDialog(capturedAddr, capturedCond);
            });
            Grid::SetColumn(editBtn, 2);

            // Delete button
            Button delBtn;
            delBtn.Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_Delete")));
            delBtn.Background(btnBg);
            delBtn.Foreground(deleteFg);
            delBtn.BorderBrush(btnBorder);
            delBtn.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(6, 2, 6, 2));
            delBtn.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 0, 4, 0));
            delBtn.FontSize(11);
            delBtn.VerticalAlignment(VerticalAlignment::Center);
            delBtn.HorizontalAlignment(HorizontalAlignment::Right);
            delBtn.Click([this, capturedAddr](auto&&, auto&&)
            {
                if (OnDelete) OnDelete(capturedAddr);
            });
            Grid::SetColumn(delBtn, 3);

            row.Children().Append(cb);
            row.Children().Append(textStack);
            row.Children().Append(editBtn);
            row.Children().Append(delBtn);

            items.Append(row);
        }

        // ---- Watchpoints section ----
        if (!watchpoints.empty())
        {
            TextBlock wpHeader;
            wpHeader.Text(ResourceHelper::GetString(L"Breakpoints_SectionWatchpoints"));
            wpHeader.Foreground(headerFg);
            wpHeader.FontSize(12);
            wpHeader.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
            wpHeader.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(8, 8, 0, 2));
            items.Append(wpHeader);
        }

        // Sort watchpoints by address
        std::vector<std::pair<uint32_t, WatchpointData>> sortedWP(
            watchpoints.begin(), watchpoints.end());
        std::sort(sortedWP.begin(), sortedWP.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [addr, wp] : sortedWP)
        {
            Grid row;
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(40));
            row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().GetAt(3).Width(GridLengthHelper::Auto());
            row.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 2, 4, 2));
            row.Tag(winrt::box_value(addr));

            CheckBox cb;
            cb.IsChecked(wp.enabled);
            cb.MinWidth(0);
            cb.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0, 0, 0, 0));
            cb.VerticalAlignment(VerticalAlignment::Center);
            uint32_t capturedAddr = addr;
            cb.Checked([this, capturedAddr](auto&&, auto&&)
            {
                if (OnToggleWatchpointEnabled) OnToggleWatchpointEnabled(capturedAddr, true);
            });
            cb.Unchecked([this, capturedAddr](auto&&, auto&&)
            {
                if (OnToggleWatchpointEnabled) OnToggleWatchpointEnabled(capturedAddr, false);
            });
            Grid::SetColumn(cb, 0);

            StackPanel textStack;
            textStack.Orientation(Orientation::Vertical);
            textStack.VerticalAlignment(VerticalAlignment::Center);

            std::string sizeStr = wp.size == WatchpointSize::Byte ? ".B" :
                                  wp.size == WatchpointSize::Long ? ".L" : ".W";
            std::string typeStr = wp.type == WatchpointType::Read ? "R" :
                                  wp.type == WatchpointType::Write ? "W" : "RW";
            auto wpStr = std::format("${:08X}{} [{}]", addr, sizeStr, typeStr);

            TextBlock wpText;
            wpText.FontFamily(consolasFont);
            wpText.FontSize(13);
            wpText.Foreground(watchFg);
            wpText.Text(winrt::to_hstring(wpStr));
            textStack.Children().Append(wpText);

            if (!wp.condition.empty())
            {
                TextBlock condText;
                condText.FontFamily(consolasFont);
                condText.FontSize(11);
                condText.Foreground(condFg);
                condText.Text(winrt::to_hstring(std::format("  if {}", wp.condition)));
                textStack.Children().Append(condText);
            }
            Grid::SetColumn(textStack, 1);

            // Edit watchpoint button
            Button wpEditBtn;
            wpEditBtn.Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_Edit")));
            wpEditBtn.Background(btnBg);
            wpEditBtn.Foreground(editFg);
            wpEditBtn.BorderBrush(btnBorder);
            wpEditBtn.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(6, 2, 6, 2));
            wpEditBtn.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 0, 0, 0));
            wpEditBtn.FontSize(11);
            wpEditBtn.VerticalAlignment(VerticalAlignment::Center);
            WatchpointSize capturedSize = wp.size;
            WatchpointType capturedType = wp.type;
            std::string capturedCond = wp.condition;
            wpEditBtn.Click([this, capturedAddr, capturedSize, capturedType, capturedCond](auto&&, auto&&)
            {
                ShowEditWatchpointDialog(capturedAddr, capturedSize, capturedType, capturedCond);
            });
            Grid::SetColumn(wpEditBtn, 2);

            Button delBtn;
            delBtn.Content(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_Delete")));
            delBtn.Background(btnBg);
            delBtn.Foreground(deleteFg);
            delBtn.BorderBrush(btnBorder);
            delBtn.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(6, 2, 6, 2));
            delBtn.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 0, 4, 0));
            delBtn.FontSize(11);
            delBtn.VerticalAlignment(VerticalAlignment::Center);
            delBtn.HorizontalAlignment(HorizontalAlignment::Right);
            delBtn.Click([this, capturedAddr](auto&&, auto&&)
            {
                if (OnDeleteWatchpoint) OnDeleteWatchpoint(capturedAddr);
            });
            Grid::SetColumn(delBtn, 3);

            row.Children().Append(cb);
            row.Children().Append(textStack);
            row.Children().Append(wpEditBtn);
            row.Children().Append(delBtn);

            items.Append(row);
        }
    }

    winrt::fire_and_forget BreakpointsWindow::ShowAddWatchpointDialog()
    {
        // Build dialog content
        StackPanel panel;
        panel.Spacing(8);
        panel.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));

        auto normalFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xD4, 0xD4, 0xD4 });

        // Address input
        TextBlock addrLabel;
        addrLabel.Text(ResourceHelper::GetString(L"Watchpoint_AddressLabel"));
        addrLabel.Foreground(normalFg);
        addrLabel.FontSize(12);
        panel.Children().Append(addrLabel);

        TextBox addrBox;
        addrBox.PlaceholderText(L"e.g. 0x1000 or $1000");
        addrBox.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        panel.Children().Append(addrBox);

        // Size selector
        TextBlock sizeLabel;
        sizeLabel.Text(ResourceHelper::GetString(L"Watchpoint_SizeLabel"));
        sizeLabel.Foreground(normalFg);
        sizeLabel.FontSize(12);
        panel.Children().Append(sizeLabel);

        ComboBox sizeCombo;
        sizeCombo.Items().Append(winrt::box_value(L"Byte (.B)"));
        sizeCombo.Items().Append(winrt::box_value(L"Word (.W)"));
        sizeCombo.Items().Append(winrt::box_value(L"Long (.L)"));
        sizeCombo.SelectedIndex(1); // default Word
        panel.Children().Append(sizeCombo);

        // Type selector
        TextBlock typeLabel;
        typeLabel.Text(ResourceHelper::GetString(L"Watchpoint_TypeLabel"));
        typeLabel.Foreground(normalFg);
        typeLabel.FontSize(12);
        panel.Children().Append(typeLabel);

        ComboBox typeCombo;
        typeCombo.Items().Append(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_TypeWrite")));
        typeCombo.Items().Append(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_TypeRead")));
        typeCombo.Items().Append(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_TypeReadWrite")));
        typeCombo.SelectedIndex(0); // default Write
        panel.Children().Append(typeCombo);

        // Condition input (optional)
        TextBlock condLabel;
        condLabel.Text(ResourceHelper::GetString(L"Watchpoint_ConditionLabel"));
        condLabel.Foreground(normalFg);
        condLabel.FontSize(12);
        panel.Children().Append(condLabel);

        TextBox condBox;
        condBox.PlaceholderText(L"e.g. D0==0x1234 (optional)");
        condBox.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        panel.Children().Append(condBox);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_DialogTitle")));
        dialog.Content(panel);
        dialog.PrimaryButtonText(ResourceHelper::GetString(L"Watchpoint_Add"));
        dialog.CloseButtonText(ResourceHelper::GetString(L"Watchpoint_Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result = co_await dialog.ShowAsync();
        if (result != ContentDialogResult::Primary) co_return;

        // Parse address
        auto addrStr = winrt::to_string(addrBox.Text());
        uint32_t addr = 0;
        bool parsed = false;
        if (addrStr.size() > 2 && addrStr[0] == '0' && (addrStr[1] == 'x' || addrStr[1] == 'X'))
        {
            char* end = nullptr;
            addr = static_cast<uint32_t>(std::strtoull(addrStr.c_str() + 2, &end, 16));
            parsed = (end != addrStr.c_str() + 2);
        }
        else if (!addrStr.empty() && addrStr[0] == '$')
        {
            char* end = nullptr;
            addr = static_cast<uint32_t>(std::strtoull(addrStr.c_str() + 1, &end, 16));
            parsed = (end != addrStr.c_str() + 1);
        }
        else
        {
            char* end = nullptr;
            addr = static_cast<uint32_t>(std::strtoull(addrStr.c_str(), &end, 16));
            parsed = (end != addrStr.c_str());
        }
        if (!parsed) co_return;

        WatchpointSize size = WatchpointSize::Word;
        if (sizeCombo.SelectedIndex() == 0) size = WatchpointSize::Byte;
        else if (sizeCombo.SelectedIndex() == 2) size = WatchpointSize::Long;

        WatchpointType type = WatchpointType::Write;
        if (typeCombo.SelectedIndex() == 1) type = WatchpointType::Read;
        else if (typeCombo.SelectedIndex() == 2) type = WatchpointType::ReadWrite;

        auto condStr = winrt::to_string(condBox.Text());

        if (OnAddWatchpoint) OnAddWatchpoint(addr, size, type, condStr);
    }

    winrt::fire_and_forget BreakpointsWindow::ShowEditConditionDialog(uint32_t addr, std::string currentCondition)
    {
        auto normalFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xD4, 0xD4, 0xD4 });

        StackPanel panel;
        panel.Spacing(8);

        TextBlock label;
        label.Text(winrt::to_hstring(std::format("{} ${:08X}",
            winrt::to_string(ResourceHelper::GetString(L"Breakpoints_ConditionFor")), addr)));
        label.Foreground(normalFg);
        label.FontSize(12);
        panel.Children().Append(label);

        TextBlock hint;
        hint.Text(ResourceHelper::GetString(L"Breakpoints_ConditionHint"));
        hint.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0x90, 0x90, 0x90 }));
        hint.FontSize(11);
        hint.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(hint);

        TextBox condBox;
        condBox.Text(winrt::to_hstring(currentCondition));
        condBox.PlaceholderText(L"e.g. D0==0x1234, SR&0x2000!=0");
        condBox.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        panel.Children().Append(condBox);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(ResourceHelper::GetString(L"Breakpoints_EditConditionTitle")));
        dialog.Content(panel);
        dialog.PrimaryButtonText(L"OK");
        dialog.SecondaryButtonText(ResourceHelper::GetString(L"Breakpoints_ClearCondition"));
        dialog.CloseButtonText(ResourceHelper::GetString(L"Watchpoint_Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary)
        {
            auto newCond = winrt::to_string(condBox.Text());
            if (OnSetCondition) OnSetCondition(addr, newCond);
        }
        else if (result == ContentDialogResult::Secondary)
        {
            if (OnSetCondition) OnSetCondition(addr, "");
        }
    }

    winrt::fire_and_forget BreakpointsWindow::ShowEditWatchpointDialog(
        uint32_t oldAddr, WatchpointSize oldSize, WatchpointType oldType, std::string oldCondition)
    {
        auto normalFg = Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0xFF, 0xD4, 0xD4, 0xD4 });

        StackPanel panel;
        panel.Spacing(8);
        panel.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
            Windows::UI::Color{ 0x00, 0x00, 0x00, 0x00 }));

        // Address
        TextBlock addrLabel;
        addrLabel.Text(ResourceHelper::GetString(L"Watchpoint_AddressLabel"));
        addrLabel.Foreground(normalFg);
        addrLabel.FontSize(12);
        panel.Children().Append(addrLabel);

        TextBox addrBox;
        addrBox.Text(winrt::to_hstring(std::format("0x{:X}", oldAddr)));
        addrBox.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        panel.Children().Append(addrBox);

        // Size
        TextBlock sizeLabel;
        sizeLabel.Text(ResourceHelper::GetString(L"Watchpoint_SizeLabel"));
        sizeLabel.Foreground(normalFg);
        sizeLabel.FontSize(12);
        panel.Children().Append(sizeLabel);

        ComboBox sizeCombo;
        sizeCombo.Items().Append(winrt::box_value(L"Byte (.B)"));
        sizeCombo.Items().Append(winrt::box_value(L"Word (.W)"));
        sizeCombo.Items().Append(winrt::box_value(L"Long (.L)"));
        sizeCombo.SelectedIndex(oldSize == WatchpointSize::Byte ? 0 : oldSize == WatchpointSize::Long ? 2 : 1);
        panel.Children().Append(sizeCombo);

        // Type
        TextBlock typeLabel;
        typeLabel.Text(ResourceHelper::GetString(L"Watchpoint_TypeLabel"));
        typeLabel.Foreground(normalFg);
        typeLabel.FontSize(12);
        panel.Children().Append(typeLabel);

        ComboBox typeCombo;
        typeCombo.Items().Append(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_TypeWrite")));
        typeCombo.Items().Append(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_TypeRead")));
        typeCombo.Items().Append(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_TypeReadWrite")));
        typeCombo.SelectedIndex(oldType == WatchpointType::Read ? 1 : oldType == WatchpointType::ReadWrite ? 2 : 0);
        panel.Children().Append(typeCombo);

        // Condition
        TextBlock condLabel;
        condLabel.Text(ResourceHelper::GetString(L"Watchpoint_ConditionLabel"));
        condLabel.Foreground(normalFg);
        condLabel.FontSize(12);
        panel.Children().Append(condLabel);

        TextBox condBox;
        condBox.Text(winrt::to_hstring(oldCondition));
        condBox.PlaceholderText(L"e.g. D0==0x1234 (optional)");
        condBox.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        panel.Children().Append(condBox);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(ResourceHelper::GetString(L"Watchpoint_EditTitle")));
        dialog.Content(panel);
        dialog.PrimaryButtonText(L"OK");
        dialog.CloseButtonText(ResourceHelper::GetString(L"Watchpoint_Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result = co_await dialog.ShowAsync();
        if (result != ContentDialogResult::Primary) co_return;

        // Parse address
        auto addrStr = winrt::to_string(addrBox.Text());
        uint32_t addr = 0;
        bool parsed = false;
        if (addrStr.size() > 2 && addrStr[0] == '0' && (addrStr[1] == 'x' || addrStr[1] == 'X'))
        {
            char* end = nullptr;
            addr = static_cast<uint32_t>(std::strtoull(addrStr.c_str() + 2, &end, 16));
            parsed = (end != addrStr.c_str() + 2);
        }
        else if (!addrStr.empty() && addrStr[0] == '$')
        {
            char* end = nullptr;
            addr = static_cast<uint32_t>(std::strtoull(addrStr.c_str() + 1, &end, 16));
            parsed = (end != addrStr.c_str() + 1);
        }
        else
        {
            char* end = nullptr;
            addr = static_cast<uint32_t>(std::strtoull(addrStr.c_str(), &end, 16));
            parsed = (end != addrStr.c_str());
        }
        if (!parsed) co_return;

        WatchpointSize size = WatchpointSize::Word;
        if (sizeCombo.SelectedIndex() == 0) size = WatchpointSize::Byte;
        else if (sizeCombo.SelectedIndex() == 2) size = WatchpointSize::Long;

        WatchpointType type = WatchpointType::Write;
        if (typeCombo.SelectedIndex() == 1) type = WatchpointType::Read;
        else if (typeCombo.SelectedIndex() == 2) type = WatchpointType::ReadWrite;

        auto condStr = winrt::to_string(condBox.Text());

        if (OnEditWatchpoint) OnEditWatchpoint(oldAddr, addr, size, type, condStr);
    }
}
