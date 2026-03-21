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
#include "Views/FramebufferWindow.xaml.h"
#if __has_include("FramebufferWindow.g.cpp")
#include "FramebufferWindow.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <microsoft.ui.xaml.window.h> // IWindowNative
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Content.h>
#include <format>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <robuffer.h> // IBufferByteAccess
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include "IO/KeyMapping.h"

using namespace winrt::Windows::System;

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace winrt::Em68030::implementation
{
    FramebufferWindow::FramebufferWindow()
    {
        InitializeComponent();
        if (auto root = Content().try_as<::winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            m_displayImage = root.FindName(L"DisplayImage").try_as<Controls::Image>();
        }
    }

    void FramebufferWindow::Init(::Em68030::Core::Memory& memory, ::Em68030::IO::FramebufferDevice& device,
                                 ::Em68030::IO::InputDevice* inputDevice)
    {
        m_memory = &memory;
        m_device = &device;
        m_inputDevice = inputDevice;
        m_width = device.Width();
        m_height = device.Height();
        m_bpp = device.Bpp();
        m_vramOffset = device.VramBase();

        Title(hstring(std::format(L"Em68030 Framebuffer - {}x{}x{}bpp", m_width, m_height, m_bpp)));

        // Create WriteableBitmap (BGRA32 format, 4 bytes per pixel)
        m_bitmap = WriteableBitmap(m_width, m_height);
        m_pixelBuffer.resize(static_cast<size_t>(m_width) * m_height * 4);

        if (m_displayImage)
            m_displayImage.Source(m_bitmap);

        // Resize window to fit framebuffer content (physical pixels)
        // Add chrome overhead for title bar (~32px) and border (~16px each side)
        auto appWindow = AppWindow();
        winrt::Windows::Graphics::SizeInt32 size;
        size.Width = m_width + 16;
        size.Height = m_height + 39;
        appWindow.Resize(size);

        // 30fps render timer
        auto queue = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_renderTimer = queue.CreateTimer();
        m_renderTimer.Interval(std::chrono::milliseconds(33));
        m_renderTimer.Tick({ this, &FramebufferWindow::OnRenderTick });
        m_renderTimer.Start();

        // Input event handlers (keyboard on root content, pointer on image)
        if (m_inputDevice)
        {
            auto content = Content();
            content.KeyDown({ this, &FramebufferWindow::OnKeyDown });
            content.KeyUp({ this, &FramebufferWindow::OnKeyUp });

            if (m_displayImage)
            {
                m_displayImage.PointerMoved({ this, &FramebufferWindow::OnPointerMoved });
                m_displayImage.PointerPressed({ this, &FramebufferWindow::OnPointerPressed });
                m_displayImage.PointerReleased({ this, &FramebufferWindow::OnPointerReleased });
            }
        }

        // Update grab rect when window moves or resizes
        SizeChanged([this](auto&&, auto&&) { UpdateGrabRect(); });
        AppWindow().Changed([this](auto&&, auto&&) { UpdateGrabRect(); });

        // Release grab when window loses focus
        Activated([this](auto&&, Microsoft::UI::Xaml::WindowActivatedEventArgs const& args) {
            if (args.WindowActivationState() == Microsoft::UI::Xaml::WindowActivationState::Deactivated)
                UngrabMouse();
        });

        // Release grab when window closes
        Closed([this](auto&&, auto&&) { UngrabMouse(); });
    }

    void FramebufferWindow::OnRenderTick(
        [[maybe_unused]] Microsoft::UI::Dispatching::DispatcherQueueTimer const& sender,
        [[maybe_unused]] Windows::Foundation::IInspectable const& args)
    {
        RenderFrame();
    }

    void FramebufferWindow::RenderFrame()
    {
        if (!m_device || !m_memory || !m_device->Enabled()) return;

        auto ram = m_memory->GetFastRamPointer();
        if (!ram) return;

        uint32_t vramEnd = m_vramOffset + static_cast<uint32_t>(m_device->Stride() * m_height);
        if (vramEnd > m_memory->GetFastRamSize()) return;

        switch (m_bpp)
        {
        case 16: RenderFrame16bpp(ram); break;
        case 8:  RenderFrame8bpp(ram);  break;
        case 32: RenderFrame32bpp(ram); break;
        default: return;
        }

        // Copy pixel buffer into WriteableBitmap
        auto buffer = m_bitmap.PixelBuffer();
        auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
        uint8_t* pixels = nullptr;
        byteAccess->Buffer(&pixels);
        if (pixels)
        {
            std::memcpy(pixels, m_pixelBuffer.data(), m_pixelBuffer.size());
        }
        m_bitmap.Invalidate();
    }

    /// 16bpp r5g6b5 big-endian -> BGRA32
    void FramebufferWindow::RenderFrame16bpp(const uint8_t* ram)
    {
        int srcOffset = static_cast<int>(m_vramOffset);
        int dstOffset = 0;

        for (int y = 0; y < m_height; y++)
        {
            for (int x = 0; x < m_width; x++)
            {
                int idx = srcOffset + (y * m_width + x) * 2;
                uint16_t pixel = static_cast<uint16_t>((ram[idx] << 8) | ram[idx + 1]);

                int r = (pixel >> 11) & 0x1F;
                int g = (pixel >> 5) & 0x3F;
                int b = pixel & 0x1F;

                m_pixelBuffer[dstOffset]     = static_cast<uint8_t>((b << 3) | (b >> 2)); // B
                m_pixelBuffer[dstOffset + 1] = static_cast<uint8_t>((g << 2) | (g >> 4)); // G
                m_pixelBuffer[dstOffset + 2] = static_cast<uint8_t>((r << 3) | (r >> 2)); // R
                m_pixelBuffer[dstOffset + 3] = 0xFF;                                      // A
                dstOffset += 4;
            }
        }
    }

    /// 8bpp palette index -> BGRA32
    void FramebufferWindow::RenderFrame8bpp(const uint8_t* ram)
    {
        int srcOffset = static_cast<int>(m_vramOffset);
        int dstOffset = 0;

        for (int i = 0; i < m_width * m_height; i++)
        {
            uint8_t index = ram[srcOffset + i];
            auto [r, g, b] = m_device->GetPaletteEntry(index);

            m_pixelBuffer[dstOffset]     = b;    // B
            m_pixelBuffer[dstOffset + 1] = g;    // G
            m_pixelBuffer[dstOffset + 2] = r;    // R
            m_pixelBuffer[dstOffset + 3] = 0xFF; // A
            dstOffset += 4;
        }
    }

    /// 32bpp ARGB big-endian -> BGRA32
    void FramebufferWindow::RenderFrame32bpp(const uint8_t* ram)
    {
        int srcOffset = static_cast<int>(m_vramOffset);
        int dstOffset = 0;

        for (int i = 0; i < m_width * m_height; i++)
        {
            int idx = srcOffset + i * 4;
            uint8_t a = ram[idx];
            uint8_t r = ram[idx + 1];
            uint8_t g = ram[idx + 2];
            uint8_t b = ram[idx + 3];

            m_pixelBuffer[dstOffset]     = b; // B
            m_pixelBuffer[dstOffset + 1] = g; // G
            m_pixelBuffer[dstOffset + 2] = r; // R
            m_pixelBuffer[dstOffset + 3] = a; // A
            dstOffset += 4;
        }
    }

    // ========================================================================
    // Input event handlers
    // ========================================================================

    void FramebufferWindow::OnKeyDown(
        [[maybe_unused]] Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (!m_inputDevice) return;

        // Ctrl+Shift+G: toggle mouse grab
        if (e.Key() == winrt::Windows::System::VirtualKey::G &&
            (::GetKeyState(VK_CONTROL) & 0x8000) &&
            (::GetKeyState(VK_SHIFT) & 0x8000))
        {
            if (m_mouseGrabbed)
                UngrabMouse();
            else
                GrabMouse();
            e.Handled(true);
            return;
        }

        // Ctrl+Shift+V: paste clipboard text as key events
        if (e.Key() == winrt::Windows::System::VirtualKey::V &&
            (::GetKeyState(VK_CONTROL) & 0x8000) &&
            (::GetKeyState(VK_SHIFT) & 0x8000))
        {
            // Release Ctrl and Shift first — they were already sent to the guest as key presses,
            // so the guest would interpret pasted keys as modified keys without this.
            m_inputDevice->PushKeyEvent(42, 0); // KEY_LEFTSHIFT release
            m_inputDevice->PushKeyEvent(29, 0); // KEY_LEFTCTRL release
            PasteFromClipboard();
            e.Handled(true);
            return;
        }

        auto code = ::Em68030::IO::WindowsVkToLinuxKey(static_cast<int>(e.Key()));
        if (code != 0)
        {
            m_inputDevice->PushKeyEvent(code, 1);
            e.Handled(true);
        }
    }

    void FramebufferWindow::OnKeyUp(
        [[maybe_unused]] Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (!m_inputDevice) return;
        auto code = ::Em68030::IO::WindowsVkToLinuxKey(static_cast<int>(e.Key()));
        if (code != 0)
        {
            m_inputDevice->PushKeyEvent(code, 0);
            e.Handled(true);
        }
    }

    void FramebufferWindow::OnPointerMoved(
        [[maybe_unused]] Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        if (!m_inputDevice || !m_displayImage) return;

        auto point = e.GetCurrentPoint(m_displayImage);
        auto pos = point.Position();

        auto actualW = m_displayImage.ActualWidth();
        auto actualH = m_displayImage.ActualHeight();
        if (actualW <= 0 || actualH <= 0) return;

        // Update absolute position registers (tablet device for X Window System)
        auto absX = static_cast<uint16_t>(std::clamp(pos.X * m_width / actualW, 0.0, static_cast<double>(m_width - 1)));
        auto absY = static_cast<uint16_t>(std::clamp(pos.Y * m_height / actualH, 0.0, static_cast<double>(m_height - 1)));
        m_inputDevice->SetMouseAbsPosition(absX, absY);

        // Relative deltas for gpm (relative mouse device).
        // gpm's evdev handler does NOT reset state->dy between events, and the
        // Linux kernel drops REL_Y=0 (EV_REL with value=0 is filtered).
        // This causes gpm to retain stale dy values, creating vertical drift.
        //
        // Fix: when dy is 0 but dx is non-zero, send alternating +1/-1 in dy.
        // These cancel out over 2 frames (net Y movement = 0), while ensuring
        // gpm always receives a fresh REL_Y event that overwrites the stale value.
        // The 1-pixel oscillation is within a single character cell and invisible.
        if (m_lastMouseValid)
        {
            auto dx = static_cast<int16_t>(pos.X - m_lastMouseX);
            auto dy = static_cast<int16_t>(pos.Y - m_lastMouseY);

            if (dx != 0 && dy == 0)
            {
                // Inject alternating ±1 to prevent gpm stale dy
                dy = static_cast<int16_t>(m_yDither);
                m_yDither = -m_yDither;
            }

            if (dx != 0 || dy != 0)
                m_inputDevice->PushMouseMoveEvent(dx, dy);
        }
        m_lastMouseX = pos.X;
        m_lastMouseY = pos.Y;
        m_lastMouseValid = true;
    }

    void FramebufferWindow::OnPointerPressed(
        [[maybe_unused]] Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        if (!m_inputDevice || !m_displayImage) return;

        // Capture pointer for drag tracking
        m_displayImage.CapturePointer(e.Pointer());

        auto point = e.GetCurrentPoint(m_displayImage);
        auto props = point.Properties();

        // Linux BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112
        if (props.IsLeftButtonPressed())
            m_inputDevice->PushMouseButtonEvent(0x110, 1);
        if (props.IsRightButtonPressed())
            m_inputDevice->PushMouseButtonEvent(0x111, 1);
        if (props.IsMiddleButtonPressed())
            m_inputDevice->PushMouseButtonEvent(0x112, 1);
    }

    void FramebufferWindow::OnPointerReleased(
        [[maybe_unused]] Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        if (!m_inputDevice || !m_displayImage) return;

        m_displayImage.ReleasePointerCapture(e.Pointer());

        auto point = e.GetCurrentPoint(m_displayImage);
        auto props = point.Properties();

        // Report release for buttons that are no longer pressed
        if (!props.IsLeftButtonPressed())
            m_inputDevice->PushMouseButtonEvent(0x110, 0);
        if (!props.IsRightButtonPressed())
            m_inputDevice->PushMouseButtonEvent(0x111, 0);
        if (!props.IsMiddleButtonPressed())
            m_inputDevice->PushMouseButtonEvent(0x112, 0);
    }

    winrt::fire_and_forget FramebufferWindow::PasteFromClipboard()
    {
        if (!m_inputDevice) co_return;

        auto dataPackage = Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
        if (!dataPackage.Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text()))
            co_return;

        auto text = co_await dataPackage.GetTextAsync();
        if (text.empty()) co_return;

        m_inputDevice->PushTextInput(winrt::to_string(text));
    }

    // ========================================================================
    // Mouse grab (pointer confinement)
    // ========================================================================

    void FramebufferWindow::SetContentCursor(Microsoft::UI::Input::InputCursor const& cursor)
    {
        // Replace the XAML Grid with our CursorGrid on first call
        if (!m_cursorGrid)
        {
            auto oldContent = Content().try_as<Controls::Grid>();
            if (!oldContent) return;

            auto newGrid = winrt::make_self<CursorGrid>();
            newGrid->Background(oldContent.Background());

            // Move children from old Grid to new Grid
            while (oldContent.Children().Size() > 0)
            {
                auto child = oldContent.Children().GetAt(0);
                oldContent.Children().RemoveAt(0);
                newGrid->Children().Append(child);
            }

            Content(*newGrid);
            m_cursorGrid = newGrid;

            // Re-wire keyboard events on new content
            if (m_inputDevice)
            {
                newGrid->KeyDown({ this, &FramebufferWindow::OnKeyDown });
                newGrid->KeyUp({ this, &FramebufferWindow::OnKeyUp });
            }
        }

        if (m_cursorGrid)
            m_cursorGrid->SetCursor(cursor);
    }

    void FramebufferWindow::GrabMouse()
    {
        if (m_mouseGrabbed) return;

        if (!m_hwnd)
        {
            auto windowNative = this->try_as<::IWindowNative>();
            if (windowNative)
                windowNative->get_WindowHandle(&m_hwnd);
        }
        if (!m_hwnd) return;

        m_mouseGrabbed = true;
        UpdateGrabRect();

        // Hide cursor by setting a disposed cursor
        auto blankCursor = Microsoft::UI::Input::InputSystemCursor::Create(
            Microsoft::UI::Input::InputSystemCursorShape::Arrow);
        blankCursor.Close();
        SetContentCursor(blankCursor);

        UpdateTitleGrabStatus();
    }

    void FramebufferWindow::UngrabMouse()
    {
        if (!m_mouseGrabbed) return;

        m_mouseGrabbed = false;
        ::ClipCursor(nullptr);

        // Restore cursor
        SetContentCursor(Microsoft::UI::Input::InputSystemCursor::Create(
            Microsoft::UI::Input::InputSystemCursorShape::Arrow));

        UpdateTitleGrabStatus();
    }

    void FramebufferWindow::UpdateGrabRect()
    {
        if (!m_mouseGrabbed || !m_hwnd) return;

        RECT windowRect;
        if (::GetClientRect(m_hwnd, &windowRect))
        {
            // Convert client rect to screen coordinates
            POINT topLeft = { windowRect.left, windowRect.top };
            POINT bottomRight = { windowRect.right, windowRect.bottom };
            ::ClientToScreen(m_hwnd, &topLeft);
            ::ClientToScreen(m_hwnd, &bottomRight);

            RECT clipRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
            ::ClipCursor(&clipRect);
        }
    }

    void FramebufferWindow::UpdateTitleGrabStatus()
    {
        auto baseTitle = std::wstring(L"Em68030 Framebuffer");
        if (m_device)
        {
            baseTitle += std::format(L" - {}x{}x{}bpp", m_width, m_height, m_bpp);
        }
        if (m_mouseGrabbed)
        {
            baseTitle += L" [Mouse Grabbed - Ctrl+Shift+G to release]";
        }
        Title(winrt::hstring(baseTitle));
    }

}
