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

#include "FramebufferWindow.g.h"
#include "Core/Memory.h"
#include "IO/FramebufferDevice.h"
#include "IO/InputDevice.h"

namespace winrt::Em68030::implementation
{
    // XAML template implementations
    template <typename D, typename... I>
    void FramebufferWindowT<D, I...>::InitializeComponent()
    {
        if (!_contentLoaded)
        {
            _contentLoaded = true;
            ::winrt::Windows::Foundation::Uri resourceLocator{ L"ms-appx:///Views/FramebufferWindow.xaml" };
            ::winrt::Microsoft::UI::Xaml::Application::LoadComponent(*this, resourceLocator);
        }
    }
    template <typename D, typename... I>
    void FramebufferWindowT<D, I...>::Connect(int32_t, IInspectable const&) { _contentLoaded = true; }
    template <typename D, typename... I>
    ::winrt::Microsoft::UI::Xaml::Markup::IComponentConnector
    FramebufferWindowT<D, I...>::GetBindingConnector(int32_t, IInspectable const&) { return nullptr; }
    template <typename D, typename... I>
    void FramebufferWindowT<D, I...>::UnloadObject(::winrt::Microsoft::UI::Xaml::DependencyObject const&)
    { throw ::winrt::hresult_not_implemented(); }
    template <typename D, typename... I>
    void FramebufferWindowT<D, I...>::DisconnectUnloadedObject(int32_t) {}

    struct FramebufferWindow : FramebufferWindowT<FramebufferWindow>
    {
        FramebufferWindow();

        void Init(::Em68030::Core::Memory& memory, ::Em68030::IO::FramebufferDevice& device,
                  ::Em68030::IO::InputDevice* inputDevice);

    private:
        void OnRenderTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const& sender,
                          Windows::Foundation::IInspectable const& args);
        void RenderFrame();
        void RenderFrame16bpp(const uint8_t* ram);
        void RenderFrame8bpp(const uint8_t* ram);
        void RenderFrame32bpp(const uint8_t* ram);

        void OnKeyDown(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnKeyUp(Windows::Foundation::IInspectable const& sender,
                     Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnPointerMoved(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerPressed(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerReleased(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

        winrt::fire_and_forget PasteFromClipboard();

        ::Em68030::Core::Memory* m_memory = nullptr;
        ::Em68030::IO::FramebufferDevice* m_device = nullptr;
        ::Em68030::IO::InputDevice* m_inputDevice = nullptr;

        Microsoft::UI::Xaml::Controls::Image m_displayImage{ nullptr };
        Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_renderTimer{ nullptr };
        std::vector<uint8_t> m_pixelBuffer;

        int m_width = 0;
        int m_height = 0;
        int m_bpp = 0;
        uint32_t m_vramOffset = 0;

        // Relative mouse delta tracking (for gpm via FIFO)
        double m_lastMouseX = 0;
        double m_lastMouseY = 0;
        double m_accumDx = 0;
        double m_accumDy = 0;
        bool m_lastMouseValid = false;

        // Mouse grab (pointer confinement)
        bool m_mouseGrabbed = false;
        HWND m_hwnd = nullptr;

        void GrabMouse();
        void UngrabMouse();
        void UpdateGrabRect();
        void UpdateTitleGrabStatus();
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct FramebufferWindow : FramebufferWindowT<FramebufferWindow, implementation::FramebufferWindow>
    {
    };
}
