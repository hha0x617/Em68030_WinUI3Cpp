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
#include "App.xaml.h"
// Note: We do NOT include App.g.cpp here because its projected constructor
// (App::App() : App(make<impl::App>())) doesn't compile for composable
// Application types. We provide winrt_make ourselves.
#include "MainWindow.xaml.h"

// Activation entry point required by the WinUI runtime
void* winrt_make_Em68030_App()
{
    return winrt::detach_abi(winrt::make<winrt::Em68030::factory_implementation::App>());
}

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// Application entry point (DISABLE_XAML_GENERATED_MAIN is defined)
int WINAPI wWinMain(_In_ HINSTANCE /*hInstance*/, _In_opt_ HINSTANCE /*hPrevInstance*/,
                    _In_ LPWSTR /*lpCmdLine*/, _In_ int /*nShowCmd*/)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) {
            ::winrt::make<::winrt::Em68030::implementation::App>();
        });

    return 0;
}

namespace winrt::Em68030::implementation
{
    App::App()
    {
        // Xaml objects should not call InitializeComponent during construction.
        // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& args)
    {
        m_window = make<MainWindow>();
        m_window.Activate();
    }
}
