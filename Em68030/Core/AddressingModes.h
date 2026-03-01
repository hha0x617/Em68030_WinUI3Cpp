#pragma once

#include <cstdint>
#include <utility>

namespace Em68030::Core
{

// Forward declaration
class MC68030;

enum class AddressingMode : int
{
    DataRegDirect,      // Dn
    AddrRegDirect,      // An
    AddrRegIndirect,    // (An)
    AddrRegPostInc,     // (An)+
    AddrRegPreDec,      // -(An)
    AddrRegDisp,        // (d16,An)
    AddrRegIndex,       // (d8,An,Xn)
    AbsShort,           // (xxx).W
    AbsLong,            // (xxx).L
    Immediate,          // #imm
    PcDisp,             // (d16,PC)
    PcIndex,            // (d8,PC,Xn)
};

struct EffectiveAddress
{
    AddressingMode Mode;
    int Register;
    uint32_t Address;
    uint32_t Value;
    int Size;       // 1=byte, 2=word, 4=long
    int ExtWords;   // extension words consumed

    static std::pair<AddressingMode, int> Decode(int modeField, int regField);

    static uint32_t ResolveAddress(MC68030& cpu, AddressingMode mode, int reg, int size);

    static uint32_t ReadValue(MC68030& cpu, AddressingMode mode, int reg, int size);

    // --- Read-Modify-Write support ---
    // For instructions that read a value, modify it, and write it back to the same EA,
    // the address must be resolved only ONCE. ReadValueForModify caches the resolved
    // address, and WriteValueFromModify reuses it instead of re-resolving (which would
    // consume extension words from the next instruction).
    static uint32_t ReadValueForModify(MC68030& cpu, AddressingMode mode, int reg, int size);
    static void WriteValueFromModify(MC68030& cpu, AddressingMode mode, int reg, int size, uint32_t value);

    static void WriteValue(MC68030& cpu, AddressingMode mode, int reg, int size, uint32_t value);

private:
    static uint32_t ResolveIndexed(MC68030& cpu, uint32_t baseAddr);
};

} // namespace Em68030::Core
