#pragma once

#include "App.g.h"

namespace winrt::Em68030::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
