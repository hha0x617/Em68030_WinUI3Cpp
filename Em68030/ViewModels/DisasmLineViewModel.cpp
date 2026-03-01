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
