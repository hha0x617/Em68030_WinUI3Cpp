#include "pch.h"
#include "MemoryByteCell.h"
#if __has_include("MemoryByteCell.g.cpp")
#include "MemoryByteCell.g.cpp"
#endif

namespace winrt::Em68030::implementation
{
    void MemoryByteCell::Init(uint32_t address, uint8_t value, int32_t row, int32_t col)
    {
        m_address = address;
        m_originalValue = value;
        wchar_t buf[4];
        swprintf_s(buf, L"%02X", value);
        m_editText = buf;
        m_row = row;
        m_column = col;
    }

    void MemoryByteCell::EditText(hstring const& value)
    {
        if (m_editText != value)
        {
            m_editText = value;
            RaisePropertyChanged(L"EditText");
            RaisePropertyChanged(L"IsModified");
        }
    }

    bool MemoryByteCell::IsModified() const
    {
        auto edited = GetEditedValue();
        if (edited.has_value())
            return edited.value() != m_originalValue;
        return false;
    }

    void MemoryByteCell::IsSelected(bool value)
    {
        if (m_isSelected != value)
        {
            m_isSelected = value;
            RaisePropertyChanged(L"IsSelected");
        }
    }

    void MemoryByteCell::Reload(uint32_t address, uint8_t value)
    {
        m_address = address;
        m_originalValue = value;
        wchar_t buf[4];
        swprintf_s(buf, L"%02X", value);
        m_editText = buf;
        m_isSelected = false;

        RaisePropertyChanged(L"Address");
        RaisePropertyChanged(L"OriginalValue");
        RaisePropertyChanged(L"EditText");
        RaisePropertyChanged(L"IsModified");
        RaisePropertyChanged(L"IsSelected");
    }

    std::optional<uint8_t> MemoryByteCell::GetEditedValue() const
    {
        std::wstring s(m_editText.c_str(), m_editText.size());
        if (s.empty() || s.size() > 2)
            return std::nullopt;

        unsigned long val = 0;
        try
        {
            val = std::stoul(s, nullptr, 16);
        }
        catch (...)
        {
            return std::nullopt;
        }

        if (val > 0xFF)
            return std::nullopt;

        return static_cast<uint8_t>(val);
    }

    winrt::event_token MemoryByteCell::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void MemoryByteCell::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void MemoryByteCell::RaisePropertyChanged(hstring const& propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
    }
}
