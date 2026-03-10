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
#include "RelayCommand.h"
#if __has_include("RelayCommand.g.cpp")
#include "RelayCommand.g.cpp"
#endif

namespace winrt::Em68030::implementation
{
    RelayCommand::RelayCommand(
        std::function<void(Windows::Foundation::IInspectable const&)> execute,
        std::function<bool(Windows::Foundation::IInspectable const&)> canExecute)
        : m_execute(std::move(execute))
        , m_canExecute(std::move(canExecute))
    {
    }

    bool RelayCommand::CanExecute(Windows::Foundation::IInspectable const& parameter)
    {
        if (m_canExecute)
            return m_canExecute(parameter);
        return true;
    }

    void RelayCommand::Execute(Windows::Foundation::IInspectable const& parameter)
    {
        if (m_execute)
            m_execute(parameter);
    }

    winrt::event_token RelayCommand::CanExecuteChanged(
        Windows::Foundation::EventHandler<Windows::Foundation::IInspectable> const& handler)
    {
        return m_canExecuteChanged.add(handler);
    }

    void RelayCommand::CanExecuteChanged(winrt::event_token const& token) noexcept
    {
        m_canExecuteChanged.remove(token);
    }

    void RelayCommand::RaiseCanExecuteChanged()
    {
        m_canExecuteChanged(*this, nullptr);
    }
}
