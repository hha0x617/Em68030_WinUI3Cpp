#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <cstring>

namespace Em68030::Core {

class MC68030; // forward declaration

// Pre-decoded instruction operation type
enum class JitOpType : uint8_t {
    Moveq,        // MOVEQ #imm8, Dn
    MoveLDnDm,    // MOVE.L Dn, Dm
    AddLDnDm,     // ADD.L Dn, Dm
    SubLDnDm,     // SUB.L Dn, Dm
    CmpLDnDm,     // CMP.L Dn, Dm
    AndLDnDm,     // AND.L Dn, Dm
    OrLDnDm,      // OR.L Dn, Dm
    EorLDnDm,     // EOR.L Dn, Dm
    BccB,         // Bcc.B (conditional branch)
    BraB,         // BRA.B (unconditional branch)
    Nop,          // NOP
    AddqLDn,      // ADDQ.L #imm, Dn
    SubqLDn,      // SUBQ.L #imm, Dn
    AddqAn,       // ADDQ #imm, An (no flags)
    SubqAn,       // SUBQ #imm, An (no flags)
    ClrLDn,       // CLR.L Dn (D[reg]=0, Z=1)
    TstLDn,       // TST.L Dn (test D[reg], set NZ)
    MoveLAnDn,    // MOVE.L An, Dn (D[dst]=A[src], set NZ)
    MoveaLDnAn,   // MOVEA.L Dn, An (A[dst]=D[src], no flags)
    MoveaLAnAm,   // MOVEA.L An, Am (A[dst]=A[src], no flags)
    AslImmLDn,    // ASL.L #imm, Dn
    AsrImmLDn,    // ASR.L #imm, Dn
    LslImmLDn,    // LSL.L #imm, Dn
    LsrImmLDn,    // LSR.L #imm, Dn
    ExgDnDm,      // EXG Dn,Dm
    ExgAnAm,      // EXG An,Am
    ExgDnAn,      // EXG Dn,An
    SwapDn,        // SWAP Dn
    ExtWDn,        // EXT.W Dn
    ExtLDn,        // EXT.L Dn
    ExtbLDn,       // EXTB.L Dn
    NegLDn,        // NEG.L Dn
    NotLDn,        // NOT.L Dn
};

struct JitOp {
    JitOpType type;
    uint8_t srcReg;      // source register (0-7)
    uint8_t dstReg;      // destination register (0-7)
    bool needsFlags;     // dead flag elimination: whether flags must be updated
    int32_t immediate;   // MOVEQ immediate / Bcc/BRA displacement
    uint8_t condition;   // Bcc condition code (0-15)
    uint32_t branchTarget;   // branch target PC (Bcc/BRA)
    uint32_t fallthroughPC;  // fallthrough PC when branch not taken (Bcc)
};

// Compiled basic block
class CompiledBlock {
public:
    uint32_t PhysicalAddress;
    int InstructionCount;
    int TotalCycles;
    int ByteLength;
    uint32_t FallthroughPC;  // next PC after block end (no branch)
    std::vector<JitOp> Ops;

    uint32_t Execute(MC68030& cpu) const;
};

// Block cache + execution count tracking (same structure as C# version)
class JitCache {
public:
    static constexpr int BlockCacheShift = 13;
    static constexpr int BlockCacheSize = 1 << BlockCacheShift;
    static constexpr int BlockCacheMask = BlockCacheSize - 1;
    static constexpr int CountCacheSize = 65536;
    static constexpr int CountCacheMask = CountCacheSize - 1;

    CompiledBlock* TryGetBlock(uint32_t physAddr);
    void AddBlock(uint32_t physAddr, std::unique_ptr<CompiledBlock> block);
    void InvalidateAll();
    uint8_t IncrementAndGetCount(uint32_t physAddr);
    bool IsUncompilable(uint32_t physAddr) const;
    void MarkUncompilable(uint32_t physAddr);
    int GetBlockCount() const { return m_blockCount; }

private:
    CompiledBlock* m_blockCache[BlockCacheSize]{};
    std::vector<std::unique_ptr<CompiledBlock>> m_blocks;
    uint8_t m_counts[CountCacheSize]{};
    bool m_uncompilable[CountCacheSize]{};
    int m_blockCount = 0;
};

// Basic block scanner + compiler
class JitCompiler {
public:
    static constexpr int MaxBlockLength = 64;


    std::unique_ptr<CompiledBlock> TryCompile(MC68030& cpu, uint32_t startPC, uint32_t startPhysAddr);

private:
    enum class InsnKind {
        Unsupported, Moveq, MoveLDnDm, AddLDnDm, SubLDnDm,
        CmpLDnDm, AndLDnDm, OrLDnDm, EorLDnDm, BranchAlways, Branch, Nop,
        AddqLDn, SubqLDn, AddqAn, SubqAn,
        ClrLDn, TstLDn, MoveLAnDn, MoveaLDnAn, MoveaLAnAm,
        AslImmLDn, AsrImmLDn, LslImmLDn, LsrImmLDn,
        ExgDnDm, ExgAnAm, ExgDnAn,
        SwapDn, ExtWDn, ExtLDn, ExtbLDn, NegLDn, NotLDn
    };
    static InsnKind Classify(uint16_t opcode);
};

} // namespace Em68030::Core
