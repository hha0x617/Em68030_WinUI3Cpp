#include "pch.h"

#include "Mvme147IoSpaceDevice.h"

namespace Em68030::IO {

uint8_t Mvme147IoSpaceDevice::ReadByte(uint32_t /*address*/) { return 0; }
uint16_t Mvme147IoSpaceDevice::ReadWord(uint32_t /*address*/) { return 0; }
uint32_t Mvme147IoSpaceDevice::ReadLong(uint32_t /*address*/) { return 0; }
void Mvme147IoSpaceDevice::WriteByte(uint32_t /*address*/, uint8_t /*value*/) {}
void Mvme147IoSpaceDevice::WriteWord(uint32_t /*address*/, uint16_t /*value*/) {}
void Mvme147IoSpaceDevice::WriteLong(uint32_t /*address*/, uint32_t /*value*/) {}

} // namespace Em68030::IO
