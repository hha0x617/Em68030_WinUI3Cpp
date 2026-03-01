#include "pch.h"

#include "MC68030.h"
#include "InstructionDecoder.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace Em68030::Core {

// ============================================================================
// Constructor / Destructor
// ============================================================================

MC68030::MC68030(Memory& memory)
    : m_memory(memory)
    , m_mmu(memory)
    , m_fpu()
{
    m_mmu.OnFlush = [this]() { _fetchCacheValid = false; _dataCacheValid = false; };
    m_decoder = std::make_unique<InstructionDecoder>(*this);
}

MC68030::~MC68030() = default;

// ============================================================================
// Reset
// ============================================================================

void MC68030::Reset()
{
    for (int i = 0; i < 8; i++) { D[i] = 0; A[i] = 0; }

    // Read initial SSP and PC from reset vectors (physical memory, no MMU)
    SSP = m_memory.ReadLong(0);
    PC = m_memory.ReadLong(4);
    A[7] = SSP;
    USP = 0;

    SR = 0x2700; // Supervisor mode, interrupt mask 7
    VBR = 0;
    CACR = 0;
    CAAR = 0;
    SFC = 0;
    DFC = 0;
    FunctionCodeOverride = -1;

    m_mmu.Reset();
    _fetchCacheValid = false;
    _dataCacheValid = false;
    _processingBusError = false;
    _pendingIPL = 0;
    _pendingVector = -1;
    _tickDivider = 0;
    Halted = false;
    Stopped = false;
    StopReason.clear();
    CycleCount = 0;
}

// ============================================================================
// SR change with USP/SSP swap
// ============================================================================

void MC68030::SetSR(uint16_t newSR)
{
    bool wasSuper = GetSupervisorMode();
    bool willBeSuper = (newSR & 0x2000) != 0;

    if (wasSuper && !willBeSuper)
    {
        // Supervisor -> User: save SSP, restore USP
        SSP = A[7];
        A[7] = USP;
    }
    else if (!wasSuper && willBeSuper)
    {
        // User -> Supervisor: save USP, restore SSP
        USP = A[7];
        A[7] = SSP;
    }

    // Invalidate fetch cache on privilege mode change.
    // With SRE enabled, supervisor and user modes use different root pointers
    // (SRP vs CRP), so cached VA->PA translations become invalid.
    if (wasSuper != willBeSuper) {
        _fetchCacheValid = false;
        _dataCacheValid = false;
    }

    SR = newSR;
}

// ============================================================================
// Address translation (Read / Write / Fetch separated)
// ============================================================================

uint32_t MC68030::TranslateRead(uint32_t logicalAddr, bool isProgram)
{
    uint8_t fc = GetFunctionCode(isProgram);
    // Inline ATC fast path for reads — avoids full Translate overhead on hit
    uint32_t pa = m_mmu.TranslateReadFast(logicalAddr, fc);
    if (pa != UINT32_MAX) return pa;
    return m_mmu.Translate(logicalAddr, GetSupervisorMode(), false, fc);
}

uint32_t MC68030::TranslateWrite(uint32_t logicalAddr)
{
    uint8_t fc = GetFunctionCode(false); // writes are always data
    return m_mmu.Translate(logicalAddr, GetSupervisorMode(), true, fc);
}

uint32_t MC68030::TranslateAddress(uint32_t logicalAddr)
{
    return TranslateRead(logicalAddr);
}

// ============================================================================
// Memory access (with MMU translation)
// ============================================================================

uint8_t MC68030::ReadByte(uint32_t addr)
{
    // Data page cache fast path (read-only; MOVES invalidates cache before overriding FC)
    if (_dataCacheValid && (addr & ~_dataPageMask) == _dataPageVA)
        return m_memory.ReadByte(_dataPagePA + (addr & _dataPageMask));
    uint32_t pa = TranslateRead(addr);
    if (m_mmu.GetEnabled()) {
        _dataPageMask = m_mmu.CachedPageMask;
        _dataPageVA = addr & ~_dataPageMask;
        _dataPagePA = pa & ~_dataPageMask;
        _dataCacheValid = true;
    }
    return m_memory.ReadByte(pa);
}

uint16_t MC68030::ReadWord(uint32_t addr)
{
    // Data page cache fast path (read-only; MOVES invalidates cache before overriding FC)
    if (_dataCacheValid && (addr & ~_dataPageMask) == _dataPageVA
        && (addr & _dataPageMask) + 1 < _dataPageMask)
        return m_memory.ReadWord(_dataPagePA + (addr & _dataPageMask));
    // A word read can cross a page boundary when addr is at the last byte of a page.
    if (m_mmu.GetEnabled() && (addr & m_mmu.CachedPageMask) == m_mmu.CachedPageMask)
    {
        uint8_t hi = m_memory.ReadByte(TranslateRead(addr));
        uint8_t lo = m_memory.ReadByte(TranslateRead(addr + 1));
        return static_cast<uint16_t>((hi << 8) | lo);
    }
    uint32_t pa = TranslateRead(addr);
    if (m_mmu.GetEnabled()) {
        _dataPageMask = m_mmu.CachedPageMask;
        _dataPageVA = addr & ~_dataPageMask;
        _dataPagePA = pa & ~_dataPageMask;
        _dataCacheValid = true;
    }
    return m_memory.ReadWord(pa);
}

uint32_t MC68030::ReadLong(uint32_t addr)
{
    // Data page cache fast path (read-only; MOVES invalidates cache before overriding FC)
    if (_dataCacheValid && (addr & ~_dataPageMask) == _dataPageVA
        && (addr & _dataPageMask) + 3 < _dataPageMask)
        return m_memory.ReadLong(_dataPagePA + (addr & _dataPageMask));
    // A longword read can cross a page boundary when within 3 bytes of page end.
    if (m_mmu.GetEnabled())
    {
        uint32_t offset = addr & m_mmu.CachedPageMask;
        if (offset + 3 > m_mmu.CachedPageMask)
        {
            uint16_t hi = ReadWord(addr);
            uint16_t lo = ReadWord(addr + 2);
            return (static_cast<uint32_t>(hi) << 16) | lo;
        }
    }
    uint32_t pa = TranslateRead(addr);
    if (m_mmu.GetEnabled()) {
        _dataPageMask = m_mmu.CachedPageMask;
        _dataPageVA = addr & ~_dataPageMask;
        _dataPagePA = pa & ~_dataPageMask;
        _dataCacheValid = true;
    }
    return m_memory.ReadLong(pa);
}

void MC68030::WriteByte(uint32_t addr, uint8_t val)
{
    _dataCacheValid = false;
    m_memory.WriteByte(TranslateWrite(addr), val);
}

void MC68030::WriteWord(uint32_t addr, uint16_t val)
{
    _dataCacheValid = false;
    if (m_mmu.GetEnabled() && (addr & m_mmu.CachedPageMask) == m_mmu.CachedPageMask)
    {
        m_memory.WriteByte(TranslateWrite(addr), static_cast<uint8_t>(val >> 8));
        m_memory.WriteByte(TranslateWrite(addr + 1), static_cast<uint8_t>(val));
        return;
    }
    m_memory.WriteWord(TranslateWrite(addr), val);
}

void MC68030::WriteLong(uint32_t addr, uint32_t val)
{
    _dataCacheValid = false;
    if (m_mmu.GetEnabled())
    {
        uint32_t offset = addr & m_mmu.CachedPageMask;
        if (offset + 3 > m_mmu.CachedPageMask)
        {
            WriteWord(addr, static_cast<uint16_t>(val >> 16));
            WriteWord(addr + 2, static_cast<uint16_t>(val));
            return;
        }
    }
    m_memory.WriteLong(TranslateWrite(addr), val);
}

// ============================================================================
// Instruction fetching (with page cache optimization)
// ============================================================================

uint16_t MC68030::FetchWord()
{
    uint32_t pc = PC;
    if (_fetchCacheValid && (pc & ~_fetchPageMask) == _fetchPageVA
        && (pc & _fetchPageMask) + 1 < _fetchPageMask)
    {
        uint16_t val = m_memory.ReadWord(_fetchPagePA + (pc & _fetchPageMask));
        PC = pc + 2;
        return val;
    }
    uint32_t pa = TranslateRead(pc, true);
    if (m_mmu.GetEnabled())
    {
        _fetchPageMask = m_mmu.CachedPageMask;
        _fetchPageVA = pc & ~_fetchPageMask;
        _fetchPagePA = pa & ~_fetchPageMask;
        _fetchCacheValid = true;
    }
    uint16_t v = m_memory.ReadWord(pa);
    PC = pc + 2;
    return v;
}

uint32_t MC68030::FetchLong()
{
    uint32_t pc = PC;
    if (_fetchCacheValid && (pc & ~_fetchPageMask) == _fetchPageVA
        && (pc & _fetchPageMask) + 3 < _fetchPageMask)
    {
        uint32_t val = m_memory.ReadLong(_fetchPagePA + (pc & _fetchPageMask));
        PC = pc + 4;
        return val;
    }
    // A longword fetch can cross a page boundary (e.g., PC=$xxFFE reads
    // from two different virtual pages that may map to non-contiguous
    // physical pages).  Always split into two word fetches for correctness.
    uint32_t hi = FetchWord();
    uint32_t lo = FetchWord();
    return (hi << 16) | lo;
}

void MC68030::InvalidateFetchCache()
{
    _fetchCacheValid = false;
    _dataCacheValid = false;
}

// ============================================================================
// CCR helpers
// ============================================================================

void MC68030::SetCCR(uint8_t ccr)
{
    SetCCRByte(ccr);
}

void MC68030::UpdateCCR(uint8_t flags, uint8_t mask)
{
    SetCCRByte(static_cast<uint8_t>((GetCCR() & ~mask) | (flags & mask)));
}

// ============================================================================
// User memory helpers (used by ioctl interception)
// ============================================================================

std::optional<uint32_t> MC68030::ReadUserLong(uint32_t virtualAddr)
{
    try
    {
        uint32_t physAddr = m_mmu.Translate(virtualAddr, false, false, 1);
        return m_memory.ReadLong(physAddr);
    }
    catch (...) { return std::nullopt; }
}

std::optional<uint16_t> MC68030::ReadUserWord(uint32_t virtualAddr)
{
    try
    {
        uint32_t physAddr = m_mmu.Translate(virtualAddr, false, false, 1);
        return m_memory.ReadWord(physAddr);
    }
    catch (...) { return std::nullopt; }
}

bool MC68030::WriteUserLong(uint32_t virtualAddr, uint32_t value)
{
    try
    {
        uint32_t physAddr = m_mmu.Translate(virtualAddr, false, true, 1);
        m_memory.WriteLong(physAddr, value);
        return true;
    }
    catch (...) { return false; }
}

// ============================================================================
// Exception / event logging
// ============================================================================

void MC68030::LogException(const std::string& message)
{
    if (ExceptionOccurred) ExceptionOccurred(message);
}

// ============================================================================
// External device IPL control and tick handlers
// ============================================================================

void MC68030::SetIPL(int level, int vector)
{
    _pendingIPL = level;
    _pendingVector = vector;
}

void MC68030::AddTickHandler(std::function<void()> handler)
{
    _tickHandlers.push_back(std::move(handler));
}

void MC68030::ClearTickHandlers()
{
    _tickHandlers.clear();
}

bool MC68030::HasExternalDevices() const
{
    return !_tickHandlers.empty();
}

// ============================================================================
// Bus error fixup
// ============================================================================

MC68030::FixupResult MC68030::FixupPhysicalBusError(const BusErrorException& ex)
{
    uint8_t fc = ex.FunctionCode;
    uint16_t ssw = ex.SpecialStatusWord;

    if (ssw == 0 && fc == 0)
    {
        fc = GetFunctionCode(false); // Assume data access
        ssw = static_cast<uint16_t>(fc & 7);      // FC bits 2-0
        ssw |= 0x0100;               // DF bit 8
        if (!ex.IsWrite) ssw |= 0x0040; // RW bit 6 (1=read, 0=write)
    }

    return { ex.FaultAddress, ex.IsWrite, fc, ssw };
}

// ============================================================================
// HandleBusError
// ============================================================================

void MC68030::HandleBusError(const BusErrorException& ex)
{
    PC = _lastPC;
    // If the deferred register snapshot was taken (EnsureRegSnapshot called),
    // restore A[]/D[] from the snapshot.  If not (bus error during opcode fetch
    // before any handler ran), registers haven't been modified by the faulting
    // instruction and are already in the correct pre-instruction state.
    if (!_regSnapshotNeeded)
    {
        std::copy(std::begin(_savedA), std::end(_savedA), std::begin(A));
        std::copy(std::begin(_savedD), std::end(_savedD), std::begin(D));
    }
    // Restore SR directly (not via SetSR, which would do USP/SSP swap based
    // on current A[7] -- but A[7] was just restored from _savedA and doesn't
    // correspond to the current mode).  RaiseBusError will handle mode switching.
    SR = _savedSR;
    _fetchCacheValid = false; // privilege mode may have changed
    _dataCacheValid = false;
    auto [faultAddr, isWrite, fc, ssw] = FixupPhysicalBusError(ex);
    RaiseBusError(faultAddr, isWrite, fc, ssw);
}

// ============================================================================
// ExecuteStep
// ============================================================================

void MC68030::ExecuteStep()
{
    if (Halted) return;

    // Tick external device handlers at reduced frequency (timers run even when CPU is stopped)
    if (++_tickDivider >= TickInterval)
    {
        _tickDivider = 0;
        for (size_t i = 0; i < _tickHandlers.size(); i++)
            _tickHandlers[i]();
    }

    // Check for pending interrupts (checked even during STOP)
    if (_pendingIPL > 0 && (_pendingIPL == 7 || _pendingIPL > GetInterruptMask()))
    {
        Stopped = false;
        StopReason.clear();
        // Save state before interrupt processing for bus error recovery
        _lastPC = PC;
        std::copy(std::begin(A), std::end(A), std::begin(_savedA));
        std::copy(std::begin(D), std::end(D), std::begin(_savedD));
        _savedSR = SR;
        try
        {
            ProcessInterrupt(_pendingIPL);
        }
        catch (const BusErrorException& ex)
        {
            PC = _lastPC;
            std::copy(std::begin(_savedA), std::end(_savedA), std::begin(A));
            std::copy(std::begin(_savedD), std::end(_savedD), std::begin(D));
            SR = _savedSR;
            _fetchCacheValid = false;
            _dataCacheValid = false;
            auto [faultAddr, isWrite, fc, ssw] = FixupPhysicalBusError(ex);
            RaiseBusError(faultAddr, isWrite, fc, ssw);
        }
        CycleCount++;
        return;
    }

    if (Stopped) return;

    uint32_t savedPC = PC;
    uint16_t savedSR = SR;
    std::copy(std::begin(A), std::end(A), std::begin(_savedA));
    std::copy(std::begin(D), std::end(D), std::begin(_savedD));
    try
    {
        m_decoder->ExecuteNext();
    }
    catch (const BusErrorException& ex)
    {
        PC = savedPC;
        std::copy(std::begin(_savedA), std::end(_savedA), std::begin(A));
        std::copy(std::begin(_savedD), std::end(_savedD), std::begin(D));
        SR = savedSR;
        _fetchCacheValid = false;
        _dataCacheValid = false;
        auto [faultAddr, isWrite, fc, ssw] = FixupPhysicalBusError(ex);
        RaiseBusError(faultAddr, isWrite, fc, ssw);
    }

    CycleCount++;
}

// ============================================================================
// ExecuteNextFast
// ============================================================================

bool MC68030::ExecuteNextFast()
{
    if (++_tickDivider >= TickInterval)
    {
        _tickDivider = 0;
        for (size_t i = 0; i < _tickHandlers.size(); i++)
            _tickHandlers[i]();
    }

    if (_pendingIPL > 0 && (_pendingIPL == 7 || _pendingIPL > GetInterruptMask()))
    {
        Stopped = false;
        StopReason.clear();
        // Save state before interrupt processing so HandleBusError can restore
        // correctly if the interrupt frame push faults (e.g. unmapped SSP page).
        // On real MC68030, bus error during non-bus-error exception processing
        // creates a Format $A frame with pre-exception register state.
        _lastPC = PC;
        std::copy(std::begin(A), std::end(A), std::begin(_savedA));
        std::copy(std::begin(D), std::end(D), std::begin(_savedD));
        _savedSR = SR;
        ProcessInterrupt(_pendingIPL);
        CycleCount++;
        return !Halted;
    }

    if (Stopped) return false;

    _lastPC = PC;
    _savedSR = SR;
    _regSnapshotNeeded = true;  // Deferred — group decoders call EnsureRegSnapshot()

    m_decoder->ExecuteNext();

    CycleCount++;
    return true;
}

// ============================================================================
// RaiseException
// ============================================================================

void MC68030::RaiseException(int vector)
{
    // Save SR and switch to supervisor mode
    uint16_t oldSR = SR;
    SetSupervisorMode(true);

    // Swap to SSP if not already in supervisor mode
    if ((oldSR & 0x2000) == 0)
    {
        USP = A[7];
        A[7] = SSP;
    }

    // Push Format 0 exception frame: SR, PC, Format/Vector
    // If a bus error occurs during frame push or vector read, it propagates
    // to the execution loop which calls HandleBusError → RaiseBusError.
    // RaiseBusError has double-fault protection, so cascading failures halt the CPU.
    uint16_t formatVector = static_cast<uint16_t>(0x0000 | ((vector * 4) & 0x0FFF)); // Format 0
    try
    {
        PushWord(formatVector);
        PushLong(PC);
        PushWord(oldSR);
    }
    catch (const BusErrorException&)
    {
        // Bus error during exception frame push.
        // Restore to pre-exception state and re-throw so HandleBusError
        // can process it properly (may result in double fault → halt).
        SR = oldSR;
        if ((oldSR & 0x2000) == 0)
            A[7] = USP;
        throw;
    }

    // Read vector address from vector table (supervisor data mode, through MMU)
    uint32_t vectorAddr = VBR + static_cast<uint32_t>(vector * 4);
    PC = ReadLong(vectorAddr);

    if (ExceptionOccurred)
        ExceptionOccurred(std::format("Exception vector {} at ${:08X}, new PC=${:08X}", vector, vectorAddr, PC));

    // Diagnostic: log fatal user-mode exceptions to console
    if ((oldSR & 0x2000) == 0 && DiagnosticOutput)
    {
        // The faulting PC was pushed as part of the frame
        // For Format 0: stack has SR(2), PC(4), FmtVec(2) -- PC is at A[7]+2
        uint32_t savedPC = m_memory.ReadLong(TranslateRead(A[7] + 2));

        switch (vector)
        {
            case 3: // Address error
                DiagnosticOutput(std::format("\n[EMU] ADDRESS ERROR at PC=${:08X}, SR=${:04X}\n", savedPC, oldSR));
                break;
            case 4: // Illegal instruction
            {
                uint16_t opcode = 0;
                try { opcode = m_memory.ReadWord(m_mmu.Translate(savedPC, false, false, 2)); } catch (...) {}
                DiagnosticOutput(std::format("\n[EMU] ILLEGAL INSTRUCTION at PC=${:08X}, opcode=${:04X}, SR=${:04X}\n", savedPC, opcode, oldSR));
                break;
            }
            case 8: // Privilege violation
            {
                uint16_t opcode = 0;
                try { opcode = m_memory.ReadWord(TranslateRead(savedPC)); } catch (...) {}
                DiagnosticOutput(std::format("\n[EMU] PRIVILEGE VIOLATION at PC=${:08X}, opcode=${:04X}, SR=${:04X}\n", savedPC, opcode, oldSR));
                break;
            }
            case 10: // Line-A emulator
            {
                uint16_t opcode = 0;
                try { opcode = m_memory.ReadWord(TranslateRead(savedPC)); } catch (...) {}
                DiagnosticOutput(std::format("\n[EMU] LINE-A TRAP at PC=${:08X}, opcode=${:04X}\n", savedPC, opcode));
                break;
            }
            case 11: // Line-F emulator (coprocessor)
            {
                uint16_t opcode = 0;
                try { opcode = m_memory.ReadWord(TranslateRead(savedPC)); } catch (...) {}
                DiagnosticOutput(std::format("\n[EMU] LINE-F TRAP at PC=${:08X}, opcode=${:04X}\n", savedPC, opcode));
                break;
            }
            default:
            {
                // Catch-all: log ANY user-mode exception not already handled above
                if (VerboseTrace && vector != 32) // skip TRAP#0 (syscall, traced separately)
                {
                    uint32_t crpLo = static_cast<uint32_t>(m_mmu.CRP & 0xFFFFFFFF);
                    uint16_t opcode = 0;
                    try { opcode = m_memory.ReadWord(TranslateRead(savedPC)); } catch (...) {}
                    DiagnosticOutput(std::format("[EX] V={} PC=${:08X} OP=${:04X} CRP=${:08X}\n", vector, savedPC, opcode, crpLo));
                }
                break;
            }
        }
    }

    // Compact syscall trace: log syscall number + CRP from user mode.
    if (VerboseTrace && vector == 32 && (oldSR & 0x2000) == 0 && DiagnosticOutput)
    {
        uint32_t sn = D[0];
        uint32_t crpLo = static_cast<uint32_t>(m_mmu.CRP & 0xFFFFFFFF);
        DiagnosticOutput(std::format("[s]{} ${:08X}\n", sn, crpLo));
    }

    // Syscall interception: when TRAP #0 (vector 32) fires from user mode,
    // rewrite blocking tty ioctls to non-blocking equivalents to avoid
    // output drain stalls on the emulated SCC serial port.
    if (vector == 32 && (oldSR & 0x2000) == 0 && D[0] == 54) // ioctl
    {
        auto cmd = ReadUserLong(USP + 8);
        if (cmd.has_value())
        {
            if (cmd.value() == 0x802C7415 || cmd.value() == 0x802C7416) // TIOCSETAW / TIOCSETAF
            {
                WriteUserLong(USP + 8, 0x802C7414); // -> TIOCSETA (no drain wait)
            }
            else if (cmd.value() == 0x2000745E) // TIOCDRAIN (tcdrain)
            {
                // Short-circuit: pop exception frame, return success immediately.
                uint16_t frameSR = PopWord();
                uint32_t framePC = PopLong();
                PopWord(); // Format/Vector
                SetSR(frameSR);
                PC = framePC;
                D[0] = 0;      // Return 0 (success)
                SetFlagC(false);  // No error
                return;         // Skip syscall entirely
            }
        }
    }
}

// ============================================================================
// RaiseBusError
// ============================================================================

void MC68030::RaiseBusError(uint32_t faultAddress, bool isWrite, uint8_t functionCode, uint16_t ssw)
{
    if (_processingBusError)
    {
        // Double bus fault -> halt the processor
        Halted = true;
        StopReason = "Double bus fault";
        if (ExceptionOccurred)
            ExceptionOccurred(std::format("DOUBLE BUS FAULT at ${:08X} - CPU halted", faultAddress));
        return;
    }

    _processingBusError = true;
    struct ResetGuard { bool& flag; ~ResetGuard() { flag = false; } } resetGuard{_processingBusError};

    // Compute diagnostic MMUSR via PTest (Translate no longer modifies MMUSR,
    // so the global MMUSR may be stale from the last PTEST instruction).
    m_mmu.PTest(faultAddress, (SR & 0x2000) != 0, isWrite, functionCode, 7);
    uint16_t savedMMUSR = m_mmu.MMUSR;

    // Compact one-line log for ALL bus errors
    if (VerboseTrace && DiagnosticOutput)
    {
        uint32_t crpLo = static_cast<uint32_t>(m_mmu.CRP & 0xFFFFFFFF);
        uint32_t srpLo = static_cast<uint32_t>(m_mmu.SRP & 0xFFFFFFFF);
        bool isMovesFault = (SR & 0x2000) != 0 && functionCode >= 1 && functionCode <= 2;
        const char* mode = isMovesFault ? "MOVES" : ((SR & 0x2000) != 0 ? "S" : "U");
        bool isSupervisorAccess = (functionCode & 4) != 0;
        std::string rpInfo = (isSupervisorAccess && m_mmu.GetSRE())
            ? std::format("SRP=${:08X} (SRE=1)", srpLo)
            : std::format("CRP=${:08X}", crpLo);
        DiagnosticOutput(std::format("[BE:{}] PC=${:08X} FA=${:08X} SSW=${:04X} MMUSR=${:04X} {} FC={} {} USP=${:08X}\n",
            mode, PC, faultAddress, ssw, savedMMUSR, (isWrite ? "W" : "R"), functionCode, rpInfo,
            (SR & 0x2000) == 0 ? A[7] : USP));
    }

    try
    {
        uint32_t oldPC = PC; // PC of faulting instruction (restored by ExecuteStep)
        uint16_t oldSR = SR;
        SetSupervisorMode(true);

        // Swap to SSP if coming from user mode
        if ((oldSR & 0x2000) == 0)
        {
            USP = A[7];
            A[7] = SSP;
        }

        // Format $A stack frame (MC68030 Short Bus Cycle Fault)
        // Total: 16 words = 32 bytes
        // Push from bottom of frame upward (stack pre-decrement)

        // +$1C: Internal registers (4 bytes)
        PushLong(0);
        // +$18: Data output buffer (4 bytes)
        PushLong(0);
        // +$14: Internal register (2 bytes)
        PushWord(0);
        // +$16: Internal register (2 bytes)
        PushWord(0);
        // +$10: Data cycle fault address (4 bytes)
        PushLong(faultAddress);
        // +$0E: Instruction pipe stage B (2 bytes)
        PushWord(0);
        // +$0C: Instruction pipe stage C (2 bytes)
        PushWord(0);
        // +$0A: Special Status Word (2 bytes)
        PushWord(ssw);
        // +$08: Internal register (2 bytes)
        PushWord(0);

        // +$06: Format/Vector word: format=$A, vector offset = 2*4 = 8
        uint16_t formatVector = static_cast<uint16_t>(0xA000 | (2 * 4));
        PushWord(formatVector);
        // +$02: PC (4 bytes)
        PushLong(PC);
        // +$00: SR (2 bytes)
        PushWord(oldSR);

        // Read vector from vector table (supervisor data mode, through MMU)
        uint32_t vectorAddr = VBR + (2 * 4); // Vector 2 = bus error
        uint32_t vectorPhys = TranslateRead(vectorAddr);
        PC = m_memory.ReadLong(vectorPhys);

        std::string modeTag = (oldSR & 0x2000) == 0 ? " [USER-FAULT]" : "";
        if (ExceptionOccurred)
        {
            ExceptionOccurred(std::format(
                "Bus Error at ${:08X}, SSW=${:04X}, SR=${:04X}{}, MMUSR=${:04X}, "
                "vectorVA=${:08X}>PA=${:08X}, new PC=${:08X} (faulting PC=${:08X}, VBR=${:08X})",
                faultAddress, ssw, oldSR, modeTag, savedMMUSR, vectorAddr, vectorPhys, PC, oldPC, VBR));
        }
    }
    catch (const BusErrorException& ex2)
    {
        // Bus error during bus error frame construction = double fault
        Halted = true;
        StopReason = "Double bus fault (during stack frame construction)";
        if (ExceptionOccurred)
        {
            ExceptionOccurred(std::format(
                "DOUBLE BUS FAULT - CPU halted\n"
                "  Original fault: addr=${:08X}, write={}, FC={}, SSW=${:04X}\n"
                "  Frame push fault: addr=${:08X}, write={}\n"
                "  CPU: PC=${:08X}, SR=${:04X}, A7=${:08X}, SSP=${:08X}, USP=${:08X}, VBR=${:08X}",
                faultAddress, isWrite, functionCode, ssw,
                ex2.FaultAddress, ex2.IsWrite,
                PC, SR, A[7], SSP, USP, VBR));
        }
    }
}

// ============================================================================
// ProcessInterrupt
// ============================================================================

void MC68030::ProcessInterrupt(int level)
{
    uint16_t oldSR = SR;
    SetSupervisorMode(true);
    SetInterruptMask(level);

    if ((oldSR & 0x2000) == 0)
    {
        USP = A[7];
        A[7] = SSP;
    }

    // Use PCC-provided vector if available, otherwise autovector
    int vector = _pendingVector >= 0 ? _pendingVector : 24 + level;
    uint16_t formatVector = static_cast<uint16_t>(0x0000 | ((vector * 4) & 0x0FFF));
    PushWord(formatVector);
    PushLong(PC);
    PushWord(oldSR);

    uint32_t vectorAddr = VBR + static_cast<uint32_t>(vector * 4);
    PC = ReadLong(vectorAddr);
}

// ============================================================================
// RaiseTrap
// ============================================================================

void MC68030::RaiseTrap(int trapNum)
{
    TrapHandled = false;
    if (TrapExecuted) TrapExecuted(trapNum);
    if (!TrapHandled)
        RaiseException(32 + trapNum);
}

// ============================================================================
// Stack operations
// ============================================================================

void MC68030::PushWord(uint16_t value)
{
    A[7] -= 2;
    WriteWord(A[7], value);
}

void MC68030::PushLong(uint32_t value)
{
    A[7] -= 4;
    WriteLong(A[7], value);
}

uint16_t MC68030::PopWord()
{
    uint16_t val = ReadWord(A[7]);
    A[7] += 2;
    return val;
}

uint32_t MC68030::PopLong()
{
    uint32_t val = ReadLong(A[7]);
    A[7] += 4;
    return val;
}

// ============================================================================
// Condition evaluation
// ============================================================================

bool MC68030::EvaluateCondition(int condCode)
{
    bool c = GetFlagC(), v = GetFlagV(), z = GetFlagZ(), n = GetFlagN();
    switch (condCode)
    {
        case 0x0: return true;              // T (True)
        case 0x1: return false;             // F (False)
        case 0x2: return !c && !z;          // HI
        case 0x3: return c || z;            // LS
        case 0x4: return !c;               // CC (HI)
        case 0x5: return c;                // CS (LO)
        case 0x6: return !z;              // NE
        case 0x7: return z;               // EQ
        case 0x8: return !v;              // VC
        case 0x9: return v;               // VS
        case 0xA: return !n;              // PL
        case 0xB: return n;               // MI
        case 0xC: return n == v;           // GE
        case 0xD: return n != v;           // LT
        case 0xE: return !z && (n == v);   // GT
        case 0xF: return z || (n != v);    // LE
        default:  return false;
    }
}

} // namespace Em68030::Core
