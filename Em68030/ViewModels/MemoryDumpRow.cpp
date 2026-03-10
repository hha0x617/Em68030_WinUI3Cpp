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
#include "MemoryDumpRow.h"
#if __has_include("MemoryDumpRow.g.cpp")
#include "MemoryDumpRow.g.cpp"
#endif
#include "Core/Memory.h"

namespace winrt::Em68030::implementation
{
    MemoryDumpRow::MemoryDumpRow()
    {
        m_cells = winrt::single_threaded_observable_vector<Em68030::MemoryByteCell>();
    }

    void MemoryDumpRow::Init(uint32_t address, ::Em68030::Core::Memory& memory, int rowIndex)
    {
        m_address = address;
        m_cells.Clear();

        for (int i = 0; i < 16; i++)
        {
            uint8_t b = memory.PeekByte(address + static_cast<uint32_t>(i));
            auto cell = winrt::make<implementation::MemoryByteCell>();
            cell.as<implementation::MemoryByteCell>()->Init(
                address + static_cast<uint32_t>(i), b, rowIndex, i);
            m_cells.Append(cell);
        }
    }

    void MemoryDumpRow::Update(uint32_t address, ::Em68030::Core::Memory& memory, int rowIndex)
    {
        m_address = address;
        RaisePropertyChanged(L"AddressText");

        for (int i = 0; i < 16; i++)
        {
            uint8_t b = memory.PeekByte(address + static_cast<uint32_t>(i));
            if (i < static_cast<int>(m_cells.Size()))
            {
                auto cell = m_cells.GetAt(i);
                cell.as<implementation::MemoryByteCell>()->Reload(
                    address + static_cast<uint32_t>(i), b);
            }
        }

        RaisePropertyChanged(L"AsciiText");
    }

    hstring MemoryDumpRow::AddressText() const
    {
        wchar_t buf[12];
        swprintf_s(buf, L"%08X", m_address);
        return buf;
    }

    hstring MemoryDumpRow::AsciiText() const
    {
        std::wstring result;
        result.reserve(16);

        for (uint32_t i = 0; i < m_cells.Size(); i++)
        {
            auto cell = m_cells.GetAt(i);
            auto impl = cell.as<implementation::MemoryByteCell>();
            auto edited = impl->GetEditedValue();
            uint8_t b = edited.value_or(impl->OriginalValue());
            result += (b >= 0x20 && b < 0x7F) ? static_cast<wchar_t>(b) : L'.';
        }

        return hstring(result);
    }

    void MemoryDumpRow::RefreshAscii()
    {
        RaisePropertyChanged(L"AsciiText");
    }

    winrt::event_token MemoryDumpRow::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void MemoryDumpRow::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void MemoryDumpRow::RaisePropertyChanged(hstring const& propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
    }
}
