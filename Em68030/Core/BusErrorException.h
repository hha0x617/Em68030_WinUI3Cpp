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
