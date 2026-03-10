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

namespace Em68030::Core
{

// Forward declarations
class MC68030;
class Fpu;
enum class AddressingMode : int;

/// Decodes and executes MC68881/MC68882 FPU coprocessor instructions.
/// Called from InstructionDecoder when a Line-F with cpID=1 is detected.
class FpuInstructionDecoder
{
public:
    FpuInstructionDecoder(MC68030& cpu, Fpu& fpu);

    /// Decode and execute an FPU instruction. opcode is the first word already fetched.
    void Execute(uint16_t opcode);

private:
    void ExecuteGeneral(uint16_t opcode, int eaMode, int eaReg);
    void ExecuteArithmetic(int op, double src, int dstReg);
    void ExecuteFBcc(uint16_t opcode, bool longDisp);
    void ExecuteFSccDBccTRAPcc(uint16_t opcode, int eaMode, int eaReg);
    void ExecuteFSave(uint16_t opcode, int eaMode, int eaReg);
    void ExecuteFRestore(uint16_t opcode, int eaMode, int eaReg);

    /// Read an FP value from the effective address in the given format.
    double ReadEAFloat(int eaMode, int eaReg, int format);

    /// Write an FP value to the effective address in the given format.
    void WriteEAFloat(int eaMode, int eaReg, int format, double value);

    /// Helper for multi-register FMOVEM control register transfers.
    AddressingMode AdvanceEA(AddressingMode mode, int& reg, int size, int origEaMode, int origEaReg);

    /// MC68882 FMOVECR constant ROM lookup.
    static double GetFmovecrConstant(int offset);

    MC68030& _cpu;
    Fpu& _fpu;
};

} // namespace Em68030::Core
