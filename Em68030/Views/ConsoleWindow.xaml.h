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

#include "ConsoleWindow.g.h"
#include "Views/Vt100Terminal.h"

#include <queue>
#include <mutex>
#include <functional>
#include <string>
#include <atomic>
#include <windows.h>

namespace winrt::Em68030::implementation
{
    struct ConsoleWindow : ConsoleWindowT<ConsoleWindow>
    {
        ConsoleWindow();
        ConsoleWindow(int cols, int rows, int scrollbackLines);

        // --- Thread-safe output: called from emulation background thread ---

        /// Enqueue a single character for display. Thread-safe.
        void AppendChar(char ch);

        /// Enqueue a string for display. Thread-safe.
        void AppendString(const std::string& s);

        // --- Thread-safe input: called from emulation background thread ---

        /// Dequeue a single buffered character (Generic mode). Returns '\0' if empty.
        char ReadChar();

        /// Dequeue a buffered line (Generic mode). Returns "" if empty.
        std::string ReadString();

        /// Resize the scrollback buffer. Must be called on the UI thread.
        void SetScrollbackLines(int lines);

        /// Resize the terminal and adjust window size to match.
        void SetTerminalSize(int cols, int rows);

        /// Callback to feed characters directly to the SCC device (MVME147 mode).
        /// When set, keyboard input bypasses line-buffered mode and sends raw bytes.
        std::function<void(uint8_t)> OnCharInput;

        // --- XAML event handlers ---
        void OutputBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OutputBox_CharacterReceived(winrt::Microsoft::UI::Xaml::UIElement const& sender,
                                         winrt::Microsoft::UI::Xaml::Input::CharacterReceivedRoutedEventArgs const& e);
        void ScrollbackToggle_Click(winrt::Windows::Foundation::IInspectable const& sender,
                                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        // VT100 terminal emulator (character cell buffer)
        ::Em68030::Views::Vt100Terminal m_terminal;

        // Thread-safe output queue: emulation thread enqueues, UI timer drains
        std::queue<char> m_outputQueue;
        std::mutex m_outputMutex;

        // Input buffers for Generic mode (line-buffered input)
        std::queue<char> m_charBuffer;
        std::queue<std::string> m_lineBuffer;
        std::mutex m_inputMutex;

        // Inline input buffer for Generic mode line editing
        std::string m_inputLine;

        // Render timer (DispatcherQueueTimer, 100ms interval)
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_renderTimer{ nullptr };
        winrt::event_token m_renderTimerToken;

        // Scrollback / live toggle
        bool m_showScrollback = false;
        bool m_autoScroll = true;

        // Cursor blink
        int m_blinkCounter = 0;
        bool m_cursorVisible = true;

        // Render the terminal screen to the TextBox
        void RenderScreen();

        // Submit line-buffered input (Generic mode)
        void SubmitGenericInput();

        // Send raw bytes to SCC RX FIFO (MVME147 mode)
        void SendRawBytes(const std::string& data);

        // Copy selected text to clipboard (context menu handler)
        void OnContextCopy();

        // Paste clipboard text (async). rawMode=true sends to SCC, false appends to input buffer.
        winrt::fire_and_forget PasteFromClipboard(bool rawMode);

        // Character cell measurement for resize
        float m_charWidth = 0;
        float m_charHeight = 0;
        bool m_charMeasured = false;

        // Cached reference to the ScrollViewer inside the TextBox template
        // (named "ContentElement"), used for accurate text area measurement.
        Microsoft::UI::Xaml::Controls::ScrollViewer m_contentScrollViewer{ nullptr };

        // Empirically measured overhead (padding/chrome) inside the TextBox/ScrollViewer
        float m_textAreaOverhead = 0;

        // Minimum window size enforcement via WM_GETMINMAXINFO subclass
        HWND m_hwnd = nullptr;
        int m_minWindowWidth = 0;
        int m_minWindowHeight = 0;
        static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR subclassId, DWORD_PTR refData);

        void MeasureCharCell();
        void UpdateTitle();
        void OnSizeChanged(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& e);

        // Initialization helper (shared between constructors)
        void InitConsoleWindow();
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct ConsoleWindow : ConsoleWindowT<ConsoleWindow, implementation::ConsoleWindow>
    {
    };
}
