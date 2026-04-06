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

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

#include "Memory.h"
#include "Mmu.h"
#include "Fpu.h"
#include "BusErrorException.h"
#include "JitCompiler.h"

namespace Em68030::Core {

// Forward declaration
class InstructionDecoder;

class MC68030 {
    friend class CompiledBlock; // Allow JIT Execute() to access data page cache

public:
    // ========================================================================
    // Nested types
    // ========================================================================

    // ========================================================================
    // Constructor / Destructor
    // ========================================================================

    explicit MC68030(Memory& memory);
    ~MC68030();

    // Non-copyable, non-movable (due to reference member and unique_ptr)
    MC68030(const MC68030&) = delete;
    MC68030& operator=(const MC68030&) = delete;
    MC68030(MC68030&&) = delete;
    MC68030& operator=(MC68030&&) = delete;

    // ========================================================================
    // Registers
    // ========================================================================

    // Data registers D0-D7
    uint32_t D[8]{};

    // Address registers A0-A7 (A7 = USP in user mode)
    uint32_t A[8]{};

    // Program Counter
    uint32_t PC = 0;

    // Status Register (16-bit)
    // Bits 15-8: System byte (T1,T0,S,M,0,I2,I1,I0)
    // Bits 7-0: CCR (0,0,0,X,N,Z,V,C)
    uint16_t SR = 0;

    // Supervisor Stack Pointer
    uint32_t SSP = 0;

    // User Stack Pointer (explicit, separate from SSP)
    uint32_t USP = 0;

    // Vector Base Register
    uint32_t VBR = 0;

    // Cache Control Register
    uint32_t CACR = 0;

    // Cache Address Register
    uint32_t CAAR = 0;

    // Source Function Code / Destination Function Code registers
    uint32_t SFC = 0;
    uint32_t DFC = 0;

    // ========================================================================
    // Execution state
    // ========================================================================

    bool Halted = false;
    bool Stopped = false;
    std::string StopReason;   // empty = none
    int64_t CycleCount = 0;
    int64_t InstructionCount = 0;

    // STOP idle time tracking — accumulated wall-clock time spent in STOP state
    std::chrono::steady_clock::time_point _stopEnteredTime{};
    std::chrono::steady_clock::duration _totalStopDuration{};
    bool _stopTimingActive = false;

    // Returns accumulated STOP idle duration and resets the accumulator.
    std::chrono::steady_clock::duration ConsumeStopDuration()
    {
        auto d = _totalStopDuration;
        _totalStopDuration = std::chrono::steady_clock::duration{};
        return d;
    }

    // MOVES instruction override: when >= 0, GetFunctionCode returns this value
    // instead of computing from supervisor mode. Used for DFC/SFC.
    int FunctionCodeOverride = -1;

    // Enable verbose bus-error / syscall tracing (off by default to avoid UI flood)
    bool VerboseTrace = false;

    // JIT compiler
    bool JitEnabled = false;

    bool TrapHandled = false;

    // Last PC before instruction execution (for caller-side bus error recovery)
    uint32_t _lastPC = 0;

    // ========================================================================
    // Callbacks / Events
    // ========================================================================

    std::function<void(int)> TrapExecuted;
    std::function<void(const std::string&)> ExceptionOccurred;
    std::function<void(const std::string&)> DiagnosticOutput;

    /// Callback invoked when the CPU executes the RESET instruction (0x4E70) in supervisor mode.
    /// On real hardware, RESET asserts the RSTO signal to reset all external devices.
    std::function<void()> OnResetInstruction;

    /// Memory watchpoint callback. Called on every data read/write when WatchpointsEnabled is true.
    /// Parameters: (address, accessSize, isWrite, oldValue, newValue)
    std::function<void(uint32_t, uint32_t, bool, uint32_t, uint32_t)> OnMemoryAccess;
    bool WatchpointsEnabled = false;

    // ========================================================================
    // Accessors for owned subsystems
    // ========================================================================

    Mmu& GetMmu() { return m_mmu; }
    const Mmu& GetMmu() const { return m_mmu; }

    Fpu& GetFpu() { return m_fpu; }
    const Fpu& GetFpu() const { return m_fpu; }

    Memory& GetMemory() { return m_memory; }
    const Memory& GetMemory() const { return m_memory; }

    // ========================================================================
    // SR flag accessors
    // ========================================================================

    inline bool GetFlagC() const { return (SR & 0x0001) != 0; }
    inline void SetFlagC(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x0001 : SR & ~0x0001); }

    inline bool GetFlagV() const { return (SR & 0x0002) != 0; }
    inline void SetFlagV(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x0002 : SR & ~0x0002); }

    inline bool GetFlagZ() const { return (SR & 0x0004) != 0; }
    inline void SetFlagZ(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x0004 : SR & ~0x0004); }

    inline bool GetFlagN() const { return (SR & 0x0008) != 0; }
    inline void SetFlagN(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x0008 : SR & ~0x0008); }

    inline bool GetFlagX() const { return (SR & 0x0010) != 0; }
    inline void SetFlagX(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x0010 : SR & ~0x0010); }

    inline uint8_t GetCCR() const { return static_cast<uint8_t>(SR & 0xFF); }
    inline void SetCCRByte(uint8_t value) { SR = static_cast<uint16_t>((SR & 0xFF00) | value); }

    inline bool GetSupervisorMode() const { return (SR & 0x2000) != 0; }
    inline void SetSupervisorMode(bool value) {
        bool wasSuper = (SR & 0x2000) != 0;
        SR = static_cast<uint16_t>(value ? SR | 0x2000 : SR & ~0x2000);
        // Invalidate fetch cache when privilege level changes.
        // With SRE enabled, supervisor uses SRP and user uses CRP — different
        // root pointers mean VA→PA mappings differ between modes.
        if (wasSuper != value) {
            _fetchCacheValid = false;
            _dataCacheValid = false;
        }
    }

    inline int GetInterruptMask() const { return (SR >> 8) & 7; }
    inline void SetInterruptMask(int value) { SR = static_cast<uint16_t>((SR & 0xF8FF) | ((value & 7) << 8)); }
    inline int GetPendingIPL() const { return _pendingIPL; }
    inline int GetPendingVector() const { return _pendingVector; }

    inline bool GetTraceT1() const { return (SR & 0x8000) != 0; }
    inline void SetTraceT1(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x8000 : SR & ~0x8000); }

    inline bool GetTraceT0() const { return (SR & 0x4000) != 0; }
    inline void SetTraceT0(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x4000 : SR & ~0x4000); }

    inline bool GetMasterMode() const { return (SR & 0x1000) != 0; }
    inline void SetMasterMode(bool value) { SR = static_cast<uint16_t>(value ? SR | 0x1000 : SR & ~0x1000); }

    // ========================================================================
    // Core methods
    // ========================================================================

    void Reset();

    void ExecuteStep();
    bool ExecuteNextFast();
    bool ExecuteNextFastJit();

    void HandleBusError(const BusErrorException& ex);

    // Take the deferred register snapshot (called by group decoders before register modification)
    inline void EnsureRegSnapshot() {
        if (_regSnapshotNeeded) {
            std::copy(std::begin(A), std::end(A), std::begin(_savedA));
            std::copy(std::begin(D), std::end(D), std::begin(_savedD));
            _regSnapshotNeeded = false;
        }
    }
    void InvalidateDataCache() { _dataCacheValid = false; }
    void SetupDataCache(uint32_t va, uint32_t pa, uint32_t mask) {
        _dataPageVA = va & ~mask;
        _dataPagePA = pa & ~mask;
        _dataPageMask = mask;
        _dataCacheValid = true;
    }
    void RaiseException(int vector);
    void RaiseBusError(uint32_t faultAddress, bool isWrite, uint8_t functionCode, uint16_t ssw);
    void RaiseTrap(int trapNum);

    void PushWord(uint16_t value);
    void PushLong(uint32_t value);
    uint16_t PopWord();
    uint32_t PopLong();

    bool EvaluateCondition(int condCode);

    void SetSR(uint16_t newSR);

    // JIT cache access
    int GetJitBlockCount() const { return m_jitCache.GetBlockCount(); }
    void InvalidateJitCache() { m_jitCache.InvalidateAll(); }

    uint32_t TranslateRead(uint32_t logicalAddr, bool isProgram = false);
    uint32_t TranslateWrite(uint32_t logicalAddr);
    uint32_t TranslateAddress(uint32_t logicalAddr);

    uint8_t ReadByte(uint32_t addr);
    uint16_t ReadWord(uint32_t addr);
    uint32_t ReadLong(uint32_t addr);

    void WriteByte(uint32_t addr, uint8_t val);
    void WriteWord(uint32_t addr, uint16_t val);
    void WriteLong(uint32_t addr, uint32_t val);

    uint16_t FetchWord();
    uint32_t FetchLong();

    inline uint8_t GetFunctionCode(bool isProgram) {
        if (FunctionCodeOverride >= 0)
            return static_cast<uint8_t>(FunctionCodeOverride & 7);
        return static_cast<uint8_t>((SR & 0x2000) != 0 ? (isProgram ? 6 : 5) : (isProgram ? 2 : 1));
    }

    void SetCCR(uint8_t ccr);
    void UpdateCCR(uint8_t flags, uint8_t mask);
    void InvalidateFetchCache();

    void LogException(const std::string& message);

    // External device IPL control and tick handlers
    void SetIPL(int level, int vector = -1);
    void AddTickHandler(std::function<void()> handler);
    void ClearTickHandlers();
    bool HasExternalDevices() const;

    /// Suppress interrupt processing for the given number of instructions.
    /// Used by PCC to defer SCSI interrupt delivery so the driver can finish
    /// setting up state (hostdata->connected, hostdata->state) before the ISR runs.
    /// WD33C93 SAT commands complete synchronously during WriteByte, but real
    /// hardware takes milliseconds for selection/transfer.
    void SuppressInterrupt(int instructions)
    {
        _interruptSuppress = instructions;
    }

private:
    // Read a longword from user virtual address via MMU (FC=1 user data). Returns nullopt on fault.
    std::optional<uint32_t> ReadUserLong(uint32_t virtualAddr);

    // Read a word from user virtual address via MMU (FC=1 user data). Returns nullopt on fault.
    std::optional<uint16_t> ReadUserWord(uint32_t virtualAddr);

    // Write a longword to user virtual address via MMU (FC=1 user data). Returns false on fault.
    bool WriteUserLong(uint32_t virtualAddr, uint32_t value);

    struct FixupResult {
        uint32_t faultAddress;
        bool isWrite;
        uint8_t functionCode;
        uint16_t ssw;
    };
    FixupResult FixupPhysicalBusError(const BusErrorException& ex);

    void ProcessInterrupt(int level);

    // JIT execution helpers
    __declspec(noinline) bool ExecuteNextJit(CompiledBlock* block);
    __declspec(noinline) void JitSamplePC();

    // ========================================================================
    // Owned subsystems
    // ========================================================================

    Memory& m_memory;
    Mmu m_mmu;
    Fpu m_fpu;
    std::unique_ptr<InstructionDecoder> m_decoder;

    // ========================================================================
    // Internal state
    // ========================================================================

    // Double bus fault detection
    bool _processingBusError = false;

    // Register snapshot for bus error recovery
    // Deferred: Fast handlers (register-only) skip the copy entirely.
    // Group decoders call EnsureRegSnapshot() at entry, before any register modification.
    bool _regSnapshotNeeded = false;
    uint32_t _savedA[8]{};
    uint32_t _savedD[8]{};
    uint16_t _savedSR = 0;

    // Fetch page cache
    uint32_t _fetchPageVA = 0;
    uint32_t _fetchPagePA = 0;
    uint32_t _fetchPageMask = 0;
    bool _fetchCacheValid = false;

    // Data read page cache (1-entry, read-only — writes bypass to avoid M-bit issues)
    uint32_t _dataPageVA = 0;
    uint32_t _dataPagePA = 0;
    uint32_t _dataPageMask = 0;
    bool _dataCacheValid = false;

    // External interrupt support
    int _pendingIPL = 0;
    int _pendingVector = -1;
    std::vector<std::function<void()>> _tickHandlers;
    int _tickDivider = 0;
    int _interruptSuppress = 0;

    static constexpr int TickInterval = 256;

    // JIT (internals)
    mutable JitCache m_jitCache;
    JitCompiler m_jitCompiler;

public:
    // JIT settings (configured by ViewModel)
    uint8_t JitCompileThreshold = 32;
    int JitMinBlockLength = 3;
    static constexpr uint16_t JitBailoutBlacklistThreshold = 64;
};

} // namespace Em68030::Core
