#pragma once

#include "MemoryDumpRow.g.h"
#include "MemoryByteCell.h"

// Forward declaration for native Memory class
namespace Em68030::Core { class Memory; }

namespace winrt::Em68030::implementation
{
    struct MemoryDumpRow : MemoryDumpRowT<MemoryDumpRow>
    {
        MemoryDumpRow();

        // Non-projected initializer: populate from native Memory
        void Init(uint32_t address, ::Em68030::Core::Memory& memory, int rowIndex);

        // Update existing row in-place with new address/data
        void Update(uint32_t address, ::Em68030::Core::Memory& memory, int rowIndex);

        // Properties
        hstring AddressText() const;
        hstring AsciiText() const;
        uint32_t Address() const { return m_address; }

        Windows::Foundation::Collections::IObservableVector<Em68030::MemoryByteCell> Cells() const { return m_cells; }

        void RefreshAscii();

        // INotifyPropertyChanged
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RaisePropertyChanged(hstring const& propertyName);

        uint32_t m_address = 0;
        Windows::Foundation::Collections::IObservableVector<Em68030::MemoryByteCell> m_cells{ nullptr };

        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct MemoryDumpRow : MemoryDumpRowT<MemoryDumpRow, implementation::MemoryDumpRow>
    {
    };
}
