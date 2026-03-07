#pragma once

#include "SettingsWindow.g.h"
#include "Config/EmulatorConfig.h"
#include <functional>

namespace winrt::Em68030::implementation
{
    struct DiskRowState
    {
        Microsoft::UI::Xaml::Controls::Grid RowPanel{ nullptr };
        Microsoft::UI::Xaml::Controls::TextBox PathBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox IdBox{ nullptr };
        Microsoft::UI::Xaml::Controls::Button BrowseBtn{ nullptr };
        Microsoft::UI::Xaml::Controls::Button RemoveBtn{ nullptr };
        int DesiredId = 0; // tracks the desired SCSI ID during refresh
    };

    struct SettingsWindow : SettingsWindowT<SettingsWindow>
    {
        SettingsWindow();

        /// Load settings from config into the dialog controls.
        void LoadConfig(const ::Em68030::Config::EmulatorConfig& config);

        /// Read settings from dialog controls into the config.
        /// Returns true if all values parsed successfully.
        bool SaveConfig(::Em68030::Config::EmulatorConfig& config);

        /// Show the dialog. Returns ContentDialogResult (Primary = OK).
        Windows::Foundation::IAsyncOperation<Microsoft::UI::Xaml::Controls::ContentDialogResult>
            ShowAsync(Microsoft::UI::Xaml::UIElement const& parent);

        // --- XAML event handlers ---
        void BoardType_Changed(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void BrowseRom_Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void BrowseHdd_Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void BrowseScsiCdrom_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CreateScsiImage_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CreateImage_Click(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void UpdateMvme147Visibility();
        void UpdateNatGatewayEnabled();

        // Get HWND for file picker initialization.
        HWND GetOwnerHwnd() const;

        // Helper to show a file open picker for a specific filter
        winrt::fire_and_forget BrowseFile(winrt::hstring filterName, winrt::hstring filterExt,
                                          winrt::hstring title,
                                          Microsoft::UI::Xaml::Controls::TextBox targetBox);

        // Helper to show a file save picker
        winrt::fire_and_forget SaveFile(winrt::hstring filterName, winrt::hstring filterExt,
                                        winrt::hstring title,
                                        Microsoft::UI::Xaml::Controls::TextBox targetBox,
                                        bool isScsiDisk);

        // Dynamic SCSI disk row management
        void AddDiskRow(const std::string& path, int scsiId);
        void RemoveDiskRow(size_t index);
        void RefreshScsiIdOptions();
        int GetSelectedScsiId(const Microsoft::UI::Xaml::Controls::ComboBox& box) const;

        std::vector<DiskRowState> m_diskRows;
        int m_desiredCdromId = 3; // desired CD-ROM SCSI ID (used during refresh)
        bool m_refreshingIds = false; // guard against re-entrant refresh
        std::function<void()> m_unmountScsiDisks; // callback to unmount disks before creating new images
    public:
        void SetUnmountCallback(std::function<void()> callback) { m_unmountScsiDisks = std::move(callback); }
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow, implementation::SettingsWindow>
    {
    };
}
