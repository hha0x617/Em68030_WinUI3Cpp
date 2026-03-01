#pragma once

#include <vector>
#include "MainWindow.g.h"

namespace winrt::Em68030::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        // --- Menu item click handlers ---
        void OpenBinary_Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OpenSRecord_Click(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OpenElf_Click(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Exit_Click(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- Run menu ---
        void Run_Click(winrt::Windows::Foundation::IInspectable const& sender,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Stop_Click(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Step_Click(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Reset_Click(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void FullReset_Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Trace_Click(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RunToCursor_Click(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void SetPCToCursor_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- View menu ---
        void ShowConsole_Click(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ShowBreakpoints_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ToggleLst_Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- Settings menu ---
        void Settings_Click(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- Keyboard accelerators ---
        void RunAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
                                    winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void StopAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
                                     winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void StepAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
                                     winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void RunToCursorAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
                                            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

        // --- Disassembly navigation ---
        void DisasmAddrBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void DisasmGo_Click(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void DisasmFollowPC_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void DisasmList_DoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);
        void DisasmList_KeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void DisasmList_RightTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                    winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& e);
        void DisasmCopy_Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ContextRunToCursor_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ContextSetPC_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- Manual disassembly ---
        void ManualUpdate_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- Memory dump ---
        void MemAddrBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void MemGo_Click(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void EditMemory_Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ApplyMemory_Click(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CancelMemory_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // --- Register edit ---
        void EditRegisters_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ApplyRegisters_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CancelRegisters_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        // Memory edit grid helpers
        void BuildMemoryEditGrid();
        void SelectMemoryCell(int row, int col);
        void OnMemCellKeyDown(int row, int col, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnMemCellTextChanged(int row, int col);
        void UpdateMemEditAscii(int row);

        // Helpers
        HWND GetHwnd();
        void NavigateDisassembly();
        void NavigateMemory();
        void CopyDisasmSelection();
        void RebuildDisasmList();
        void ScrollDisasmToCenter(int32_t index);
        void UpdateDisasmListAppearance();
        void UpdateRegisterDisplay();
        void UpdateMemoryDumpDisplay();
        void UpdateStatusBar();
        void UpdateToolbarInfo();
        void UpdateButtonStates();
        void EnsureConsoleWindow();
        void EnsureBreakpointsWindow();
        void RefreshBreakpointsWindow();
        uint32_t GetSelectedDisasmAddress();

        winrt::fire_and_forget ShowOpenBinaryDialog();
        winrt::fire_and_forget ShowOpenSRecordDialog();
        winrt::fire_and_forget ShowOpenElfDialog();
        winrt::fire_and_forget ShowSettingsDialog();
        winrt::fire_and_forget ShowMessageDialog(winrt::hstring title, winrt::hstring message);

        // ViewModel
        winrt::Em68030::MainViewModel m_viewModel{ nullptr };

        // Console window (separate native window)
        winrt::Em68030::ConsoleWindow m_consoleWindow{ nullptr };
        // Breakpoints window
        winrt::Em68030::BreakpointsWindow m_breakpointsWindow{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcherQueue{ nullptr };

        // Event tokens for ViewModel subscriptions
        winrt::event_token m_propertyChangedToken;
        winrt::event_token m_scrollToLineToken;
        winrt::event_token m_consoleCharToken;
        winrt::event_token m_consoleStringToken;

        // Memory edit grid state
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_memEditPanel{ nullptr };
        std::vector<std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBox>> m_memCellBoxes;
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_memAsciiLabels;
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_memAddrLabels;
        bool m_memEditGridBuilt = false;
        bool m_memEditPopulating = false;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
