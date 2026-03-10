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
#include <cstdint>

namespace Em68030::IO {

class IMemoryMappedDevice {
public:
    virtual ~IMemoryMappedDevice() = default;

    virtual uint8_t ReadByte(uint32_t address) = 0;
    virtual uint16_t ReadWord(uint32_t address) = 0;
    virtual uint32_t ReadLong(uint32_t address) = 0;
    virtual void WriteByte(uint32_t address, uint8_t value) = 0;
    virtual void WriteWord(uint32_t address, uint16_t value) = 0;
    virtual void WriteLong(uint32_t address, uint32_t value) = 0;
};

} // namespace Em68030::IO
