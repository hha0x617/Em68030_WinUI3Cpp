#pragma once

#include "DisasmLineViewModel.g.h"

namespace winrt::Em68030::implementation
{
    struct DisasmLineViewModel : DisasmLineViewModelT<DisasmLineViewModel>
    {
        DisasmLineViewModel() = default;

        // Properties
        uint32_t Address() const { return m_address; }
        void Address(uint32_t value);

        bool HasAddress() const { return m_hasAddress; }
        void HasAddress(bool value);

        hstring Text() const { return m_text; }
        void Text(hstring const& value);

        bool IsCurrentPC() const { return m_isCurrentPC; }
        void IsCurrentPC(bool value);

        bool HasBreakpoint() const { return m_hasBreakpoint; }
        void HasBreakpoint(bool value);

        bool HasDisabledBreakpoint() const { return m_hasDisabledBreakpoint; }
        void HasDisabledBreakpoint(bool value);

        hstring RawBytes() const { return m_rawBytes; }
        void RawBytes(hstring const& value);

        hstring Mnemonic() const { return m_mnemonic; }
        void Mnemonic(hstring const& value);

        hstring Operands() const { return m_operands; }
        void Operands(hstring const& value);

        int32_t Length() const { return m_length; }
        void Length(int32_t value);

        // INotifyPropertyChanged
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RaisePropertyChanged(hstring const& propertyName);

        uint32_t m_address = 0;
        bool m_hasAddress = false;
        hstring m_text;
        bool m_isCurrentPC = false;
        bool m_hasBreakpoint = false;
        bool m_hasDisabledBreakpoint = false;
        hstring m_rawBytes;
        hstring m_mnemonic;
        hstring m_operands;
        int32_t m_length = 0;

        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct DisasmLineViewModel : DisasmLineViewModelT<DisasmLineViewModel, implementation::DisasmLineViewModel>
    {
    };
}
