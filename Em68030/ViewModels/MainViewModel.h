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

#include "MainViewModel.g.h"
#include "RelayCommand.h"
#include "DisasmLineViewModel.h"
#include "MemoryDumpRow.h"
#include "MemoryByteCell.h"

#include "Core/MC68030.h"
#include "Core/Memory.h"
#include "Core/Disassembler.h"

#include <unordered_map>

#include "IO/ConsoleDevice.h"
#include "IO/HddDevice.h"
#include "IO/PccDevice.h"
#include "IO/Mk48t02Device.h"
#include "IO/LanceDevice.h"
#include "IO/Mvme147IoSpaceDevice.h"
#include "IO/Z8530Device.h"
#include "IO/Uart16550Device.h"
#include "IO/Wd33c93Device.h"
#include "IO/ScsiDisk.h"
#include "IO/ScsiCdrom.h"
#include "IO/FileLoader.h"
#include "IO/FramebufferDevice.h"
#include "IO/InputDevice.h"
#include "IO/TapNetworkHandler.h"
#include "Config/EmulatorConfig.h"

namespace winrt::Em68030::implementation
{
    struct BreakpointData
    {
        uint32_t address = 0;
        bool enabled = true;
        std::string condition;  // e.g. "D0==0x1234", "A7<0x10000", "[0x1000].w==0xFF"
    };

    struct CallStackEntry
    {
        uint32_t address = 0;     // Return address (or current PC for top frame)
        uint32_t framePointer = 0; // A6 value at this frame (0 if unknown)
        std::string label;         // Symbolic label if available
    };

    enum class WatchpointType { Read, Write, ReadWrite };
    enum class WatchpointSize { Byte = 1, Word = 2, Long = 4 };

    struct WatchpointData
    {
        uint32_t address = 0;
        WatchpointSize size = WatchpointSize::Word;
        WatchpointType type = WatchpointType::Write;
        bool enabled = true;
        std::string condition;
    };

    // Result of a watchpoint hit, set by MC68030 callback, consumed by emulation loop
    struct WatchpointHitInfo
    {
        uint32_t address = 0;
        uint32_t oldValue = 0;
        uint32_t newValue = 0;
        WatchpointSize size = WatchpointSize::Word;
        bool isWrite = true;
    };

    struct MainViewModel : MainViewModelT<MainViewModel>
    {
        MainViewModel();
        ~MainViewModel();

        // ==================================================================
        // Data register properties D0-D7
        // ==================================================================
        uint32_t D0() const;  void D0(uint32_t value);
        uint32_t D1() const;  void D1(uint32_t value);
        uint32_t D2() const;  void D2(uint32_t value);
        uint32_t D3() const;  void D3(uint32_t value);
        uint32_t D4() const;  void D4(uint32_t value);
        uint32_t D5() const;  void D5(uint32_t value);
        uint32_t D6() const;  void D6(uint32_t value);
        uint32_t D7() const;  void D7(uint32_t value);

        // ==================================================================
        // Address register properties A0-A7
        // ==================================================================
        uint32_t A0() const;  void A0(uint32_t value);
        uint32_t A1() const;  void A1(uint32_t value);
        uint32_t A2() const;  void A2(uint32_t value);
        uint32_t A3() const;  void A3(uint32_t value);
        uint32_t A4() const;  void A4(uint32_t value);
        uint32_t A5() const;  void A5(uint32_t value);
        uint32_t A6() const;  void A6(uint32_t value);
        uint32_t A7() const;  void A7(uint32_t value);

        // ==================================================================
        // Special registers
        // ==================================================================
        uint32_t PC() const;   void PC(uint32_t value);
        uint32_t SR() const;   void SR(uint32_t value);
        uint32_t SSP() const;  void SSP(uint32_t value);
        uint32_t VBR() const;  void VBR(uint32_t value);
        uint32_t CACR() const; void CACR(uint32_t value);

        // ==================================================================
        // FPU registers
        // ==================================================================
        double FP0() const;  void FP0(double value);
        double FP1() const;  void FP1(double value);
        double FP2() const;  void FP2(double value);
        double FP3() const;  void FP3(double value);
        double FP4() const;  void FP4(double value);
        double FP5() const;  void FP5(double value);
        double FP6() const;  void FP6(double value);
        double FP7() const;  void FP7(double value);
        uint32_t FPCR() const;  void FPCR(uint32_t value);
        uint32_t FPSR() const;  void FPSR(uint32_t value);
        uint32_t FPIAR() const; void FPIAR(uint32_t value);

        // ==================================================================
        // Flags
        // ==================================================================
        bool FlagX() const;  void FlagX(bool value);
        bool FlagN() const;  void FlagN(bool value);
        bool FlagZ() const;  void FlagZ(bool value);
        bool FlagV() const;  void FlagV(bool value);
        bool FlagC() const;  void FlagC(bool value);
        bool FlagS() const;  void FlagS(bool value);
        bool FlagT() const;  void FlagT(bool value);
        int32_t InterruptMask() const; void InterruptMask(int32_t value);

        // ==================================================================
        // State properties
        // ==================================================================
        bool IsRunning() const;       void IsRunning(bool value);
        bool IsMemoryEditMode() const; void IsMemoryEditMode(bool value);
        bool IsRegisterEditMode() const; void IsRegisterEditMode(bool value);
        bool IsHalted() const;
        bool IsStopped() const;
        hstring StopReason() const;
        int64_t CycleCount() const;
        hstring EstimatedMHz() const;

        // ==================================================================
        // File / display properties
        // ==================================================================
        bool ShowLst() const;        void ShowLst(bool value);
        bool HasLstFile() const;
        bool HasProgramLoaded() const;
        hstring LoadedFileName() const; void LoadedFileName(hstring const& value);
        uint32_t MemoryDumpAddress() const; void MemoryDumpAddress(uint32_t value);

        // ==================================================================
        // Observable collections
        // ==================================================================
        Windows::Foundation::Collections::IObservableVector<Em68030::DisasmLineViewModel> DisassemblyLines() const;
        Windows::Foundation::Collections::IObservableVector<Em68030::MemoryDumpRow> MemoryDumpRows() const;

        // ==================================================================
        // Commands
        // ==================================================================
        Em68030::RelayCommand StepCommand() const { return m_stepCommand; }
        Em68030::RelayCommand RunCommand() const { return m_runCommand; }
        Em68030::RelayCommand StopCommand() const { return m_stopCommand; }
        Em68030::RelayCommand ResetCommand() const { return m_resetCommand; }
        Em68030::RelayCommand EditMemoryCommand() const { return m_editMemoryCommand; }
        Em68030::RelayCommand ApplyMemoryCommand() const { return m_applyMemoryCommand; }
        Em68030::RelayCommand CancelMemoryCommand() const { return m_cancelMemoryCommand; }
        Em68030::RelayCommand EditRegistersCommand() const { return m_editRegistersCommand; }
        Em68030::RelayCommand ApplyRegistersCommand() const { return m_applyRegistersCommand; }
        Em68030::RelayCommand CancelRegistersCommand() const { return m_cancelRegistersCommand; }
        Em68030::RelayCommand ToggleTraceCommand() const { return m_toggleTraceCommand; }

        // ==================================================================
        // Methods
        // ==================================================================
        void SendConsoleChar(uint8_t ch);
        void LoadBinaryFile(hstring const& path, uint32_t loadAddress);
        void LoadSRecordFile(hstring const& path);
        ::Em68030::IO::ElfLoadResult LoadElfFileNative(const std::string& path);

        void NavigateMemoryDump(uint32_t address, uint32_t sizeBytes = 256);
        void NavigateDisassembly(uint32_t address, uint32_t sizeBytes = 0);
        void ScrollToAddress(uint32_t address);
        void NavigateToProgram();

        void Step();
        void StepOver();
        void StepOut();
        void Run();
        void RunToCursor(uint32_t address);
        void Stop();
        void DoReset();
        void DoFullReset();

        std::vector<CallStackEntry> GetCallStack(int maxDepth = 32) const;
        void SetPCToCursor(uint32_t address);
        void ToggleBreakpoint(uint32_t address);
        void EnableBreakpoint(uint32_t addr, bool enabled);
        void SetBreakpointCondition(uint32_t addr, const std::string& condition);
        void RemoveBreakpoint(uint32_t addr);
        void ClearAllBreakpoints();
        const std::unordered_map<uint32_t, BreakpointData>& AllBreakpoints() const { return m_breakpoints; }

        // Watchpoints
        void AddWatchpoint(uint32_t addr, WatchpointSize size, WatchpointType type,
                           const std::string& condition = "");
        void EnableWatchpoint(uint32_t addr, bool enabled);
        void EditWatchpoint(uint32_t oldAddr, uint32_t newAddr, WatchpointSize size,
                            WatchpointType type, const std::string& condition);
        void RemoveWatchpoint(uint32_t addr);
        void ClearAllWatchpoints();
        const std::unordered_map<uint32_t, WatchpointData>& AllWatchpoints() const { return m_watchpoints; }

        void UnmountAllScsiDisks();
        void ApplyConfig(::Em68030::Config::EmulatorConfig const& newConfig);
        void ToggleMhzDisplayMode();
        void ToggleTrace();
        void ResetDisasmFollowPC();
        bool DisasmFollowPC() const { return m_disasmFollowPC; }
        void SetDisasmFollowPC(bool value) {
            if (m_disasmFollowPC != value) {
                m_disasmFollowPC = value;
                RaisePropertyChanged(L"DisasmFollowPC");
            }
        }

        void UpdateDisassembly();
        void UpdateMemoryDump();
        void RefreshAll();

        // ==================================================================
        // Events (for View layer to subscribe)
        // ==================================================================
        winrt::event_token ConsoleCharOutput(Windows::Foundation::EventHandler<uint8_t> const& handler)
        { return m_consoleCharOutput.add(handler); }
        void ConsoleCharOutput(winrt::event_token const& token) noexcept
        { m_consoleCharOutput.remove(token); }

        winrt::event_token ConsoleStringOutput(Windows::Foundation::EventHandler<hstring> const& handler)
        { return m_consoleStringOutput.add(handler); }
        void ConsoleStringOutput(winrt::event_token const& token) noexcept
        { m_consoleStringOutput.remove(token); }

        winrt::event_token ScrollToLineRequested(Windows::Foundation::EventHandler<int32_t> const& handler)
        { return m_scrollToLineRequested.add(handler); }
        void ScrollToLineRequested(winrt::event_token const& token) noexcept
        { m_scrollToLineRequested.remove(token); }

        // ==================================================================
        // INotifyPropertyChanged
        // ==================================================================
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // ==================================================================
        // Native accessors (for View/C++ code that needs direct access)
        // ==================================================================
        ::Em68030::Core::MC68030& Cpu() { return *m_cpu; }
        ::Em68030::Core::Memory& Memory() { return *m_memory; }
        ::Em68030::IO::FramebufferDevice* FramebufferDevice() const { return m_framebufferDevice.get(); }
        ::Em68030::IO::InputDevice* InputDevice() const { return m_inputDevice.get(); }

        /// Called from emulation thread when framebuffer device is recreated (warm reboot).
        /// The UI must close and reopen the framebuffer window to pick up new device pointers.
        std::function<void()> OnFramebufferDeviceReset;
        ::Em68030::Config::EmulatorConfig& Config() { return m_config; }
        std::unordered_set<uint32_t>& EnabledBreakpoints() { return m_enabledBreakpoints; }

        // Console input callback setters (for View layer)
        void SetConsoleCharInput(std::function<char()> fn) { m_consoleCharInput = std::move(fn); }
        void SetConsoleStringInput(std::function<std::string()> fn) { m_consoleStringInput = std::move(fn); }

        // ==================================================================
        // Memory edit mode (public for View access)
        // ==================================================================
        void EnterMemoryEditMode();
        void ApplyMemoryEdits();
        void CancelMemoryEdits();

        // ==================================================================
        // Register edit mode (public for View access)
        // ==================================================================
        void EnterRegisterEditMode();
        void ApplyRegisterEdits();
        void CancelRegisterEdits();

    private:
        // ==================================================================
        // Property change notification
        // ==================================================================
        void RaisePropertyChanged(hstring const& propertyName);
        void NotifyAllRegisters();
        void NotifyFlagChanges();

        // ==================================================================
        // Setup
        // ==================================================================
        void SetupGeneric();
        void SetupMvme147();
        void SetupTrapHandler();
        void Handle147BugCall();
        void SetupMvme147BootStub(uint32_t topOfRam);
        void SetupMvme147LinuxBootStub(uint32_t topOfRam, uint32_t endOfKernel);
        void RecreateFramebufferDeviceIfNeeded();
        void ClearVram();
        void WriteBoardIdPacket(uint32_t addr);
        void InitStackPointer();
        void CheckForLstFile(const std::string& filePath);
        static void EnsureCpuDisklabel(const std::string& path);
        static uint8_t ToBcd(int val);
        static std::string GetNvramPath();

        // ==================================================================
        // Emulation thread
        // ==================================================================
        void StartEmulation();
        void EmulationThreadLoop();
        void RequestStopOnUI();
        void FinalizeStop();
        void StopEmulation();

        // ==================================================================
        // Disassembly helpers
        // ==================================================================
        void UpdatePCHighlight();
        void UpdateDisassemblyAt(uint32_t startAddress);
        void UpdateDisassemblyRange(uint32_t startAddress, uint32_t endAddress);

        // ==================================================================
        // Raise commands can-execute changed
        // ==================================================================
        void RaiseAllCommandsCanExecuteChanged();

        // ==================================================================
        // Core emulator objects (owned)
        // ==================================================================
        std::unique_ptr<::Em68030::Core::Memory> m_memory;
        std::unique_ptr<::Em68030::Core::MC68030> m_cpu;
        std::unique_ptr<::Em68030::Core::Disassembler> m_disassembler;
        std::unique_ptr<::Em68030::IO::ConsoleDevice> m_consoleDevice;
        std::unique_ptr<::Em68030::IO::HddDevice> m_hddDevice;

        // MVME147 devices (owned)
        std::unique_ptr<::Em68030::IO::PccDevice> m_pccDevice;
        std::unique_ptr<::Em68030::IO::Z8530Device> m_sccDevice;
        std::unique_ptr<::Em68030::IO::Mk48t02Device> m_rtcDevice;
        std::unique_ptr<::Em68030::IO::Wd33c93Device> m_scsiDevice;
        std::unique_ptr<::Em68030::IO::LanceDevice> m_lanceDevice;
        std::unique_ptr<::Em68030::IO::Uart16550Device> m_uartDevice;
        std::unique_ptr<::Em68030::IO::Mvme147IoSpaceDevice> m_ioSpaceDevice;
        std::vector<std::unique_ptr<::Em68030::IO::ScsiDisk>> m_scsiDisks;
        std::unique_ptr<::Em68030::IO::ScsiCdrom> m_scsiCdrom;
        std::unique_ptr<::Em68030::IO::FramebufferDevice> m_framebufferDevice;
        std::unique_ptr<::Em68030::IO::InputDevice> m_inputDevice;
        int m_scsiCdromId = -1; // Current SCSI ID of the CD-ROM (-1 = not attached)

        uint32_t m_brdIdAddress = 0;
        bool m_systemBooted = false; // True after first .BRD_ID call; used to detect warm reboot

        // Config
        ::Em68030::Config::EmulatorConfig m_config;

        // ==================================================================
        // Emulation thread state
        // ==================================================================
        std::thread m_emulationThread;
        std::atomic<bool> m_stopRequested{ false };
        std::optional<uint32_t> m_runToCursorAddress;
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcherQueue{ nullptr };

        // ==================================================================
        // UI state
        // ==================================================================
        bool m_isRunning = false;
        bool m_isMemoryEditMode = false;
        bool m_isRegisterEditMode = false;

        // Listing file support
        std::string m_lstFilePath;
        std::vector<::Em68030::IO::LstLine> m_lstLines;
        bool m_hasLstLines = false;
        bool m_showLst = false;

        // Memory dump
        uint32_t m_memoryDumpAddress = 0;
        uint32_t m_memoryDumpRowCount = 16;

        // Disassembly navigation
        uint32_t m_disasmAddress = 0;
        uint32_t m_programStartAddress = 0;
        uint32_t m_programEndAddress = 0;
        bool m_disasmFollowPC = true;
        bool m_fullProgramDisassembled = false;
        hstring m_loadedFileName;

        // Clock frequency estimation
        int64_t m_mhzCyclesSnapshot = 0;
        int64_t m_mipsInsnSnapshot = 0;
        std::chrono::steady_clock::time_point m_mhzTimestamp;
        double m_estimatedMHz = 0.0;
        double m_estimatedMips = 0.0;

        // Average MHz/MIPS (cumulative since Run started)
        int64_t m_runStartCycleCount = 0;
        int64_t m_runStartInsnCount = 0;
        std::chrono::steady_clock::time_point m_runStartTimestamp;
        double m_avgMHz = 0.0;
        double m_avgMips = 0.0;
        double m_totalStopSeconds = 0.0;
        bool m_showAvgMhz = false;

        // Breakpoints
        std::unordered_map<uint32_t, BreakpointData> m_breakpoints;
        std::unordered_set<uint32_t> m_enabledBreakpoints;
        bool m_hasConditionalBreakpoints = false;
        void RebuildEnabledSet();

        // Watchpoints
        std::unordered_map<uint32_t, WatchpointData> m_watchpoints;
        bool m_hasEnabledWatchpoints = false;
        std::optional<WatchpointHitInfo> m_watchpointHit;
        void RebuildWatchpointState();
        void CheckWatchpoint(uint32_t addr, uint32_t size, bool isWrite,
                             uint32_t oldValue, uint32_t newValue);

        // Condition expression evaluator
        bool EvaluateCondition(const std::string& condition) const;

        // Trace file
        std::unique_ptr<std::ofstream> m_traceWriter;

        // Console input callbacks
        std::function<char()> m_consoleCharInput;
        std::function<std::string()> m_consoleStringInput;

        // Register edit snapshot
        uint32_t m_savedD[8]{};
        uint32_t m_savedA[8]{};
        uint32_t m_savedPC = 0;
        uint16_t m_savedSR = 0;
        uint32_t m_savedSSP = 0;
        uint32_t m_savedVBR = 0;
        uint32_t m_savedCACR = 0;
        double m_savedFP[8]{};
        uint32_t m_savedFPCR = 0;
        uint32_t m_savedFPSR = 0;
        uint32_t m_savedFPIAR = 0;

        // ==================================================================
        // Observable collections
        // ==================================================================
        Windows::Foundation::Collections::IObservableVector<Em68030::DisasmLineViewModel> m_disassemblyLines{ nullptr };
        Windows::Foundation::Collections::IObservableVector<Em68030::MemoryDumpRow> m_memoryDumpRows{ nullptr };

        // ==================================================================
        // Commands
        // ==================================================================
        Em68030::RelayCommand m_stepCommand{ nullptr };
        Em68030::RelayCommand m_runCommand{ nullptr };
        Em68030::RelayCommand m_stopCommand{ nullptr };
        Em68030::RelayCommand m_resetCommand{ nullptr };
        Em68030::RelayCommand m_editMemoryCommand{ nullptr };
        Em68030::RelayCommand m_applyMemoryCommand{ nullptr };
        Em68030::RelayCommand m_cancelMemoryCommand{ nullptr };
        Em68030::RelayCommand m_editRegistersCommand{ nullptr };
        Em68030::RelayCommand m_applyRegistersCommand{ nullptr };
        Em68030::RelayCommand m_cancelRegistersCommand{ nullptr };
        Em68030::RelayCommand m_toggleTraceCommand{ nullptr };

        // ==================================================================
        // Events
        // ==================================================================
        winrt::event<Windows::Foundation::EventHandler<uint8_t>> m_consoleCharOutput;
        winrt::event<Windows::Foundation::EventHandler<hstring>> m_consoleStringOutput;
        winrt::event<Windows::Foundation::EventHandler<int32_t>> m_scrollToLineRequested;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct MainViewModel : MainViewModelT<MainViewModel, implementation::MainViewModel>
    {
    };
}
