#pragma once

#include "RelayCommand.g.h"

namespace winrt::Em68030::implementation
{
    struct RelayCommand : RelayCommandT<RelayCommand>
    {
        RelayCommand() = default;

        // Constructor with execute and optional canExecute callbacks.
        // Not exposed to XAML; used from C++ code only.
        RelayCommand(
            std::function<void(Windows::Foundation::IInspectable const&)> execute,
            std::function<bool(Windows::Foundation::IInspectable const&)> canExecute = nullptr);

        // ICommand
        bool CanExecute(Windows::Foundation::IInspectable const& parameter);
        void Execute(Windows::Foundation::IInspectable const& parameter);
        winrt::event_token CanExecuteChanged(Windows::Foundation::EventHandler<Windows::Foundation::IInspectable> const& handler);
        void CanExecuteChanged(winrt::event_token const& token) noexcept;

        void RaiseCanExecuteChanged();

    private:
        std::function<void(Windows::Foundation::IInspectable const&)> m_execute;
        std::function<bool(Windows::Foundation::IInspectable const&)> m_canExecute;
        winrt::event<Windows::Foundation::EventHandler<Windows::Foundation::IInspectable>> m_canExecuteChanged;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct RelayCommand : RelayCommandT<RelayCommand, implementation::RelayCommand>
    {
    };
}
