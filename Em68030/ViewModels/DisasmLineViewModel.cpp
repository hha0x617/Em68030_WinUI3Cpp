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
#include "DisasmLineViewModel.h"
#if __has_include("DisasmLineViewModel.g.cpp")
#include "DisasmLineViewModel.g.cpp"
#endif

namespace winrt::Em68030::implementation
{
    void DisasmLineViewModel::Address(uint32_t value)
    {
        if (m_address != value)
        {
            m_address = value;
            RaisePropertyChanged(L"Address");
        }
    }

    void DisasmLineViewModel::HasAddress(bool value)
    {
        if (m_hasAddress != value)
        {
            m_hasAddress = value;
            RaisePropertyChanged(L"HasAddress");
        }
    }

    void DisasmLineViewModel::Text(hstring const& value)
    {
        if (m_text != value)
        {
            m_text = value;
            RaisePropertyChanged(L"Text");
        }
    }

    void DisasmLineViewModel::IsCurrentPC(bool value)
    {
        if (m_isCurrentPC != value)
        {
            m_isCurrentPC = value;
            RaisePropertyChanged(L"IsCurrentPC");
        }
    }

    void DisasmLineViewModel::HasBreakpoint(bool value)
    {
        if (m_hasBreakpoint != value)
        {
            m_hasBreakpoint = value;
            RaisePropertyChanged(L"HasBreakpoint");
        }
    }

    void DisasmLineViewModel::HasDisabledBreakpoint(bool value)
    {
        if (m_hasDisabledBreakpoint != value)
        {
            m_hasDisabledBreakpoint = value;
            RaisePropertyChanged(L"HasDisabledBreakpoint");
        }
    }

    void DisasmLineViewModel::RawBytes(hstring const& value)
    {
        if (m_rawBytes != value)
        {
            m_rawBytes = value;
            RaisePropertyChanged(L"RawBytes");
        }
    }

    void DisasmLineViewModel::Mnemonic(hstring const& value)
    {
        if (m_mnemonic != value)
        {
            m_mnemonic = value;
            RaisePropertyChanged(L"Mnemonic");
        }
    }

    void DisasmLineViewModel::Operands(hstring const& value)
    {
        if (m_operands != value)
        {
            m_operands = value;
            RaisePropertyChanged(L"Operands");
        }
    }

    void DisasmLineViewModel::Length(int32_t value)
    {
        if (m_length != value)
        {
            m_length = value;
            RaisePropertyChanged(L"Length");
        }
    }

    winrt::event_token DisasmLineViewModel::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void DisasmLineViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void DisasmLineViewModel::RaisePropertyChanged(hstring const& propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
    }
}
