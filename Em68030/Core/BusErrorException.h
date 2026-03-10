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
#include <stdexcept>
#include <string>
#include <format>

namespace Em68030::Core {

class BusErrorException : public std::exception {
public:
    uint32_t FaultAddress;
    bool IsWrite;
    uint8_t FunctionCode;
    uint16_t SpecialStatusWord;

    BusErrorException(uint32_t faultAddress, bool isWrite, uint8_t functionCode, uint16_t ssw)
        : FaultAddress(faultAddress), IsWrite(isWrite), FunctionCode(functionCode), SpecialStatusWord(ssw)
    {
        m_message = std::format("Bus error at ${:08X}", faultAddress);
    }

    const char* what() const noexcept override { return m_message.c_str(); }

private:
    std::string m_message;
};

} // namespace Em68030::Core
