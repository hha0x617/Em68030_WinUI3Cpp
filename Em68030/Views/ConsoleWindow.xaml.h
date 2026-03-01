#pragma once

#include "ConsoleWindow.g.h"
#include "Views/Vt100Terminal.h"

#include <queue>
#include <mutex>
#include <functional>
#include <string>
#include <atomic>

namespace winrt::Em68030::implementation
{
    struct ConsoleWindow : ConsoleWindowT<ConsoleWindow>
    {
        ConsoleWindow();

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
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct ConsoleWindow : ConsoleWindowT<ConsoleWindow, implementation::ConsoleWindow>
    {
    };
}
