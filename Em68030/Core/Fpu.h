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
#include <array>
#include <string>

namespace Em68030::Core
{

// Forward declaration
class MC68030;

enum class RoundingMode
{
    ToNearest = 0,
    TowardZero = 1,
    TowardNegInf = 2,
    TowardPosInf = 3
};

enum class RoundingPrecision
{
    Extended = 0,
    Single = 1,
    Double = 2,
    Reserved = 3
};

/// MC68881/MC68882 compatible Floating Point Unit.
/// Uses double (64-bit) internally as approximation of 80-bit extended precision.
class Fpu
{
public:
    // FP data registers
    std::array<double, 8> FP{};

    // FPU control registers
    uint32_t FPCR = 0;    // Floating-Point Control Register
    uint32_t FPSR = 0;    // Floating-Point Status Register
    uint32_t FPIAR = 0;   // Floating-Point Instruction Address Register

    // FPSR condition code bits (bits 27-24)
    bool getCondN() const;
    void setCondN(bool value);

    bool getCondZ() const;
    void setCondZ(bool value);

    bool getCondI() const;
    void setCondI(bool value);

    bool getCondNAN() const;
    void setCondNAN(bool value);

    // FPCR rounding mode (bits 5-4)
    RoundingMode getRoundMode() const;

    // FPCR rounding precision (bits 7-6)
    RoundingPrecision getRoundPrec() const;

    void Reset();

    /// Update FPSR condition codes from a result value.
    void SetConditionCodes(double value);

    /// Evaluate FPU condition predicate.
    bool EvaluateCondition(int condition);

    /// Read a floating-point value from memory in the specified format.
    static double ReadFromMemory(MC68030& cpu, uint32_t address, int format);

    /// Write a floating-point value to memory in the specified format.
    static void WriteToMemory(MC68030& cpu, uint32_t address, int format, double value);

    /// Get the byte size of a data format.
    static int FormatSize(int format);

    /// Get the format name string.
    static std::string FormatName(int format);
};

} // namespace Em68030::Core
