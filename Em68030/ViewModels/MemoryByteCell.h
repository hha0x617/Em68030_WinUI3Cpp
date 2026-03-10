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

#include "MemoryByteCell.g.h"

namespace winrt::Em68030::implementation
{
    struct MemoryByteCell : MemoryByteCellT<MemoryByteCell>
    {
        MemoryByteCell() = default;

        // Non-projected constructor used from C++ code
        void Init(uint32_t address, uint8_t value, int32_t row, int32_t col);

        // Properties
        hstring EditText() const { return m_editText; }
        void EditText(hstring const& value);

        bool IsModified() const;

        bool IsSelected() const { return m_isSelected; }
        void IsSelected(bool value);

        uint32_t Address() const { return m_address; }
        uint8_t OriginalValue() const { return m_originalValue; }

        int32_t Row() const { return m_row; }
        void Row(int32_t value) { m_row = value; }

        int32_t Column() const { return m_column; }
        void Column(int32_t value) { m_column = value; }

        // Reload cell with new data (for in-place update)
        void Reload(uint32_t address, uint8_t value);

        // Get edited byte value, or std::nullopt if parse fails
        std::optional<uint8_t> GetEditedValue() const;

        // INotifyPropertyChanged
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RaisePropertyChanged(hstring const& propertyName);

        uint32_t m_address = 0;
        uint8_t m_originalValue = 0;
        hstring m_editText;
        bool m_isSelected = false;
        int32_t m_row = 0;
        int32_t m_column = 0;

        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct MemoryByteCell : MemoryByteCellT<MemoryByteCell, implementation::MemoryByteCell>
    {
    };
}
