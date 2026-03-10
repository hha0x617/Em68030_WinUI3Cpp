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

#include "AddressingModes.h"
#include "MC68030.h"

#include <stdexcept>
#include <string>

namespace Em68030::Core
{

// Read-modify-write cached address
static uint32_t s_rmwAddress;

std::pair<AddressingMode, int> EffectiveAddress::Decode(int modeField, int regField)
{
    switch (modeField)
    {
        case 0: return { AddressingMode::DataRegDirect, regField };
        case 1: return { AddressingMode::AddrRegDirect, regField };
        case 2: return { AddressingMode::AddrRegIndirect, regField };
        case 3: return { AddressingMode::AddrRegPostInc, regField };
        case 4: return { AddressingMode::AddrRegPreDec, regField };
        case 5: return { AddressingMode::AddrRegDisp, regField };
        case 6: return { AddressingMode::AddrRegIndex, regField };
        case 7:
            switch (regField)
            {
                case 0: return { AddressingMode::AbsShort, 0 };
                case 1: return { AddressingMode::AbsLong, 0 };
                case 2: return { AddressingMode::PcDisp, 0 };
                case 3: return { AddressingMode::PcIndex, 0 };
                case 4: return { AddressingMode::Immediate, 0 };
                default:
                    throw std::runtime_error("Invalid addressing mode 7/" + std::to_string(regField));
            }
        default:
            throw std::runtime_error("Invalid addressing mode " + std::to_string(modeField));
    }
}

uint32_t EffectiveAddress::ResolveAddress(MC68030& cpu, AddressingMode mode, int reg, int size)
{
    switch (mode)
    {
        case AddressingMode::AddrRegIndirect:
            return cpu.A[reg];

        case AddressingMode::AddrRegPostInc:
        {
            uint32_t addr = cpu.A[reg];
            int inc = size;
            if (reg == 7 && size == 1) inc = 2; // SP always word-aligned
            cpu.A[reg] += static_cast<uint32_t>(inc);
            return addr;
        }

        case AddressingMode::AddrRegPreDec:
        {
            int dec = size;
            if (reg == 7 && size == 1) dec = 2;
            cpu.A[reg] -= static_cast<uint32_t>(dec);
            return cpu.A[reg];
        }

        case AddressingMode::AddrRegDisp:
        {
            auto disp = static_cast<int16_t>(cpu.FetchWord());
            return static_cast<uint32_t>(cpu.A[reg] + disp);
        }

        case AddressingMode::AddrRegIndex:
            return ResolveIndexed(cpu, cpu.A[reg]);

        case AddressingMode::AbsShort:
        {
            auto addr = static_cast<int16_t>(cpu.FetchWord());
            return static_cast<uint32_t>(addr);
        }

        case AddressingMode::AbsLong:
            return cpu.FetchLong();

        case AddressingMode::PcDisp:
        {
            uint32_t pc = cpu.PC;
            auto disp = static_cast<int16_t>(cpu.FetchWord());
            return static_cast<uint32_t>(pc + disp);
        }

        case AddressingMode::PcIndex:
        {
            uint32_t pc = cpu.PC;
            return ResolveIndexed(cpu, pc);
        }

        default:
            return 0;
    }
}

uint32_t EffectiveAddress::ResolveIndexed(MC68030& cpu, uint32_t baseAddr)
{
    uint16_t ext = cpu.FetchWord();
    bool isLong = (ext & 0x0800) != 0;
    int indexReg = (ext >> 12) & 0xF;
    bool isAddrReg = (ext & 0x8000) != 0;
    int scale = 1 << ((ext >> 9) & 3);

    int32_t indexVal;
    if (isAddrReg)
        indexVal = isLong
            ? static_cast<int32_t>(cpu.A[indexReg & 7])
            : static_cast<int16_t>(static_cast<uint16_t>(cpu.A[indexReg & 7]));
    else
        indexVal = isLong
            ? static_cast<int32_t>(cpu.D[indexReg & 7])
            : static_cast<int16_t>(static_cast<uint16_t>(cpu.D[indexReg & 7]));

    indexVal *= scale;

    if ((ext & 0x0100) != 0)
    {
        // Full extension word format (68020+)
        int bdSize = (ext >> 4) & 3;
        bool bs = (ext & 0x0080) != 0; // base suppress
        bool is_ = (ext & 0x0040) != 0; // index suppress

        int32_t baseDisp = 0;
        if (bdSize == 2) baseDisp = static_cast<int16_t>(cpu.FetchWord());
        else if (bdSize == 3) baseDisp = static_cast<int32_t>(cpu.FetchLong());

        uint32_t bAddr = bs ? 0 : baseAddr;
        int32_t idx = is_ ? 0 : indexVal;

        int iis = ext & 7;
        if (iis == 0)
        {
            // No memory indirect: base + displacement + index
            return static_cast<uint32_t>(bAddr + baseDisp + idx);
        }
        else if ((iis & 0x04) == 0)
        {
            // Pre-indexed memory indirect (I/IS = 001, 010, 011)
            // EA = ([base + disp + index]) + outer_disp
            uint32_t intermediate = cpu.ReadLong(static_cast<uint32_t>(bAddr + baseDisp + idx));
            int32_t outerDisp = 0;
            if ((iis & 3) == 2) outerDisp = static_cast<int16_t>(cpu.FetchWord());
            else if ((iis & 3) == 3) outerDisp = static_cast<int32_t>(cpu.FetchLong());
            return static_cast<uint32_t>(intermediate + outerDisp);
        }
        else
        {
            // Post-indexed memory indirect (I/IS = 101, 110, 111)
            // EA = ([base + disp]) + index + outer_disp
            uint32_t intermediate = cpu.ReadLong(static_cast<uint32_t>(bAddr + baseDisp));
            int32_t outerDisp = 0;
            if ((iis & 3) == 2) outerDisp = static_cast<int16_t>(cpu.FetchWord());
            else if ((iis & 3) == 3) outerDisp = static_cast<int32_t>(cpu.FetchLong());
            return static_cast<uint32_t>(intermediate + idx + outerDisp);
        }
    }
    else
    {
        // Brief extension word
        auto disp = static_cast<int8_t>(ext & 0xFF);
        return static_cast<uint32_t>(baseAddr + disp + indexVal);
    }
}

uint32_t EffectiveAddress::ReadValue(MC68030& cpu, AddressingMode mode, int reg, int size)
{
    switch (mode)
    {
        case AddressingMode::DataRegDirect:
            switch (size)
            {
                case 1: return cpu.D[reg] & 0xFF;
                case 2: return cpu.D[reg] & 0xFFFF;
                default: return cpu.D[reg];
            }

        case AddressingMode::AddrRegDirect:
            switch (size)
            {
                case 2: return cpu.A[reg] & 0xFFFF;
                default: return cpu.A[reg];
            }

        case AddressingMode::Immediate:
            switch (size)
            {
                case 1: return static_cast<uint32_t>(cpu.FetchWord() & 0xFF);
                case 2: return cpu.FetchWord();
                default: return cpu.FetchLong();
            }

        default:
        {
            uint32_t addr = ResolveAddress(cpu, mode, reg, size);
            switch (size)
            {
                case 1: return cpu.ReadByte(addr);
                case 2: return cpu.ReadWord(addr);
                default: return cpu.ReadLong(addr);
            }
        }
    }
}

uint32_t EffectiveAddress::ReadValueForModify(MC68030& cpu, AddressingMode mode, int reg, int size)
{
    switch (mode)
    {
        case AddressingMode::DataRegDirect:
            switch (size)
            {
                case 1: return cpu.D[reg] & 0xFF;
                case 2: return cpu.D[reg] & 0xFFFF;
                default: return cpu.D[reg];
            }

        case AddressingMode::AddrRegDirect:
            switch (size)
            {
                case 2: return cpu.A[reg] & 0xFFFF;
                default: return cpu.A[reg];
            }

        default:
            s_rmwAddress = ResolveAddress(cpu, mode, reg, size);
            switch (size)
            {
                case 1: return cpu.ReadByte(s_rmwAddress);
                case 2: return cpu.ReadWord(s_rmwAddress);
                default: return cpu.ReadLong(s_rmwAddress);
            }
    }
}

void EffectiveAddress::WriteValueFromModify(MC68030& cpu, AddressingMode mode, int reg, int size, uint32_t value)
{
    switch (mode)
    {
        case AddressingMode::DataRegDirect:
            switch (size)
            {
                case 1: cpu.D[reg] = (cpu.D[reg] & 0xFFFFFF00) | (value & 0xFF); break;
                case 2: cpu.D[reg] = (cpu.D[reg] & 0xFFFF0000) | (value & 0xFFFF); break;
                default: cpu.D[reg] = value; break;
            }
            break;

        case AddressingMode::AddrRegDirect:
            cpu.A[reg] = (size == 2)
                ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(value))))
                : value;
            break;

        default:
            switch (size)
            {
                case 1: cpu.WriteByte(s_rmwAddress, static_cast<uint8_t>(value)); break;
                case 2: cpu.WriteWord(s_rmwAddress, static_cast<uint16_t>(value)); break;
                default: cpu.WriteLong(s_rmwAddress, value); break;
            }
            break;
    }
}

void EffectiveAddress::WriteValue(MC68030& cpu, AddressingMode mode, int reg, int size, uint32_t value)
{
    switch (mode)
    {
        case AddressingMode::DataRegDirect:
            switch (size)
            {
                case 1:
                    cpu.D[reg] = (cpu.D[reg] & 0xFFFFFF00) | (value & 0xFF);
                    break;
                case 2:
                    cpu.D[reg] = (cpu.D[reg] & 0xFFFF0000) | (value & 0xFFFF);
                    break;
                default:
                    cpu.D[reg] = value;
                    break;
            }
            break;

        case AddressingMode::AddrRegDirect:
            cpu.A[reg] = (size == 2)
                ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(value))))
                : value;
            break;

        default:
        {
            uint32_t addr = ResolveAddress(cpu, mode, reg, size);
            switch (size)
            {
                case 1: cpu.WriteByte(addr, static_cast<uint8_t>(value)); break;
                case 2: cpu.WriteWord(addr, static_cast<uint16_t>(value)); break;
                default: cpu.WriteLong(addr, value); break;
            }
        }
        break;
    }
}

} // namespace Em68030::Core
