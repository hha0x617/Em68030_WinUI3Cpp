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
#include "MainViewModel.h"
#if __has_include("MainViewModel.g.cpp")
#include "MainViewModel.g.cpp"
#endif

#include "Core/MC68030.h"
#include "Core/Memory.h"
#include "Core/Disassembler.h"
#include "Core/BusErrorException.h"
#include "IO/ConsoleDevice.h"
#include "IO/HddDevice.h"
#include "IO/PccDevice.h"
#include "IO/Mk48t02Device.h"
#include "IO/LanceDevice.h"
#include "IO/Mvme147IoSpaceDevice.h"
#include "Config/EmulatorConfig.h"

#include "IO/Z8530Device.h"
#include "IO/Wd33c93Device.h"
#include "IO/ScsiDisk.h"
#include "IO/ScsiCdrom.h"
#include "IO/SlirpNetworkHandler.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Microsoft::UI::Xaml::Data;
using namespace Microsoft::UI::Dispatching;

namespace winrt::Em68030::implementation
{
    // ======================================================================
    // Helper: convert std::string to hstring
    // ======================================================================
    static hstring to_hstring_from_std(const std::string& s)
    {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring ws(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), ws.data(), len);
        return hstring(ws);
    }

    // ======================================================================
    // Helper: convert hstring to std::string
    // ======================================================================
    static std::string to_std_string(hstring const& hs)
    {
        if (hs.empty()) return {};
        std::wstring ws(hs.c_str(), hs.size());
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
        std::string s(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), s.data(), len, nullptr, nullptr);
        return s;
    }

    // ======================================================================
    // Constructor
    // ======================================================================
    MainViewModel::MainViewModel()
    {
        m_disassemblyLines = winrt::single_threaded_observable_vector<Em68030::DisasmLineViewModel>();
        m_memoryDumpRows = winrt::single_threaded_observable_vector<Em68030::MemoryDumpRow>();

        m_config = ::Em68030::Config::EmulatorConfig::Load();

        // Initialize console and HDD devices with defaults (may be replaced by SetupGeneric)
        m_consoleDevice = std::make_unique<::Em68030::IO::ConsoleDevice>(m_config.ConsoleBaseAddress);
        m_hddDevice = std::make_unique<::Em68030::IO::HddDevice>(m_config.HddBaseAddress);

        if (m_config.BoardType == "MVME147")
            SetupMvme147();
        else
            SetupGeneric();

        m_disassembler = std::make_unique<::Em68030::Core::Disassembler>(*m_memory);
        SetupTrapHandler();

        m_cpu->Reset();
        m_disasmAddress = m_cpu->PC;

        // Capture the dispatcher queue for UI thread callbacks
        m_dispatcherQueue = DispatcherQueue::GetForCurrentThread();

        // Create commands
        m_stepCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { Step(); },
            [this](IInspectable const&) { return !m_isRunning; });

        m_runCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { Run(); },
            [this](IInspectable const&) { return !m_isRunning; });

        m_stopCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { Stop(); },
            [this](IInspectable const&) { return m_isRunning; });

        m_resetCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { DoReset(); },
            [this](IInspectable const&) { return !m_isRunning; });

        m_editMemoryCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { EnterMemoryEditMode(); },
            [this](IInspectable const&) { return !m_isMemoryEditMode; });

        m_applyMemoryCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { ApplyMemoryEdits(); },
            [this](IInspectable const&) { return m_isMemoryEditMode; });

        m_cancelMemoryCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { CancelMemoryEdits(); },
            [this](IInspectable const&) { return m_isMemoryEditMode; });

        m_editRegistersCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { EnterRegisterEditMode(); },
            [this](IInspectable const&) { return !m_isRunning && !m_isRegisterEditMode; });

        m_applyRegistersCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { ApplyRegisterEdits(); },
            [this](IInspectable const&) { return m_isRegisterEditMode; });

        m_cancelRegistersCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { CancelRegisterEdits(); },
            [this](IInspectable const&) { return m_isRegisterEditMode; });

        m_toggleTraceCommand = winrt::make<implementation::RelayCommand>(
            [this](IInspectable const&) { ToggleTrace(); },
            nullptr);

        UpdateDisassembly();
        UpdateMemoryDump();
    }

    MainViewModel::~MainViewModel()
    {
        // Ensure emulation thread is stopped
        m_stopRequested = true;
        if (m_emulationThread.joinable())
            m_emulationThread.join();

        // Close trace writer
        if (m_traceWriter)
        {
            m_traceWriter->close();
            m_traceWriter.reset();
        }
    }

    // ======================================================================
    // Setup methods
    // ======================================================================

    void MainViewModel::SetupGeneric()
    {
        if (!m_config.MemoryRegions.empty())
        {
            m_memory = std::make_unique<::Em68030::Core::Memory>();
            for (auto& r : m_config.MemoryRegions)
            {
                ::Em68030::Core::RegionType rt = ::Em68030::Core::RegionType::Ram;
                if (r.Type == "Rom") rt = ::Em68030::Core::RegionType::Rom;
                m_memory->AddRegion(r.BaseAddress, r.Size, rt);
            }
        }
        else
        {
            m_memory = std::make_unique<::Em68030::Core::Memory>(m_config.MemorySize);
        }
        m_cpu = std::make_unique<::Em68030::Core::MC68030>(*m_memory);
        m_cpu->JitEnabled = m_config.JitEnabled;
        m_cpu->JitMinBlockLength = m_config.JitMinBlockLength;
        m_cpu->JitCompileThreshold = static_cast<uint8_t>(m_config.JitCompileThreshold);

        m_consoleDevice = std::make_unique<::Em68030::IO::ConsoleDevice>(m_config.ConsoleBaseAddress);
        m_hddDevice = std::make_unique<::Em68030::IO::HddDevice>(m_config.HddBaseAddress);
        m_hddDevice->AttachMemory(m_memory.get());

        if (m_config.ConsoleEnabled)
            m_memory->RegisterDevice(m_config.ConsoleBaseAddress, 256, m_consoleDevice.get());
        if (m_config.HddEnabled)
            m_memory->RegisterDevice(m_config.HddBaseAddress, 256, m_hddDevice.get());

        // Wire console device callbacks
        m_consoleDevice->CharOutput = [this](char ch) {
            m_consoleCharOutput(*this, static_cast<uint8_t>(ch));
            if (m_traceWriter) m_traceWriter->put(ch);
        };
        m_consoleDevice->StringOutput = [this](const std::string& s) {
            m_consoleStringOutput(*this, to_hstring_from_std(s));
            if (m_traceWriter) *m_traceWriter << s;
        };
        m_consoleDevice->CharInput = [this]() -> char {
            if (m_consoleCharInput) return m_consoleCharInput();
            return '\0';
        };
        m_consoleDevice->StringInput = [this]() -> std::string {
            if (m_consoleStringInput) return m_consoleStringInput();
            return "";
        };

        if (!m_config.HddImagePath.empty() && std::filesystem::exists(m_config.HddImagePath))
            m_hddDevice->MountImage(m_config.HddImagePath);
    }

    void MainViewModel::SetupMvme147()
    {
        m_memory = std::make_unique<::Em68030::Core::Memory>();
        m_memory->AddRegion(0x00000000, m_config.MemorySize, ::Em68030::Core::RegionType::Ram);
        if (!m_config.Mvme147RomPath.empty())
            m_memory->AddRegion(0xFF800000, 4 * 1024 * 1024, ::Em68030::Core::RegionType::Rom);

        m_cpu = std::make_unique<::Em68030::Core::MC68030>(*m_memory);
        m_cpu->JitEnabled = m_config.JitEnabled;
        m_cpu->JitMinBlockLength = m_config.JitMinBlockLength;
        m_cpu->JitCompileThreshold = static_cast<uint8_t>(m_config.JitCompileThreshold);

        // Create MVME147 devices
        m_pccDevice = std::make_unique<::Em68030::IO::PccDevice>(*m_cpu);
        m_rtcDevice = std::make_unique<::Em68030::IO::Mk48t02Device>();
        m_sccDevice = std::make_unique<::Em68030::IO::Z8530Device>();
        m_scsiDevice = std::make_unique<::Em68030::IO::Wd33c93Device>();

        // RTC year offset: NetBSD uses YEAR0=1968, Linux uses raw 2-digit year
        if (m_config.TargetOS != "Linux")
            m_rtcDevice->SetYearOffset(68);

        uint8_t ethAddr[] = { 0x21, 0x00, 0x00 }; // 08:00:3E:21:00:00
        m_rtcDevice->SetMvme147Config(
            static_cast<uint32_t>(m_config.MemorySize),
            ethAddr, sizeof(ethAddr));

        m_lanceDevice = std::make_unique<::Em68030::IO::LanceDevice>();
        m_lanceDevice->AttachMemory(m_memory.get());
        if (m_config.NetworkMode == "NAT")
        {
            auto gwIp = ::Em68030::IO::SlirpNetworkHandler::ParseIpAddress(m_config.NatGatewayIp);
            auto gwMac = ::Em68030::IO::SlirpNetworkHandler::ParseMacAddress(m_config.NatGatewayMac);
            auto natHandler = std::make_unique<::Em68030::IO::SlirpNetworkHandler>(gwIp, gwMac);
            natHandler->DiagnosticOutput = [this](const std::string& msg) {
                if (m_traceWriter) *m_traceWriter << msg;
            };
            m_lanceDevice->SetNetworkHandler(std::move(natHandler));
        }

        // Register catch-all for I/O space
        m_ioSpaceDevice = std::make_unique<::Em68030::IO::Mvme147IoSpaceDevice>();
        m_memory->RegisterDevice(0xFFFE0000, 0x10000, m_ioSpaceDevice.get());

        // Register specific devices (overrides catch-all for their ranges)
        m_memory->RegisterDevice(0xFFFE1000, 48, m_pccDevice.get());
        m_memory->RegisterDevice(0xFFFE3000, 8, m_sccDevice.get());
        m_memory->RegisterDevice(0xFFFE4000, 4, m_scsiDevice.get());
        m_memory->RegisterDevice(0xFFFE0000, 2048, m_rtcDevice.get());
        m_memory->RegisterDevice(0xFFFE1800, 4, m_lanceDevice.get());

        // Wire LANCE interrupt through PCC
        m_lanceDevice->InterruptOutput = [this](bool active) {
            m_pccDevice->SetDeviceInterrupt("lance", active);
        };

        // Wire SCC interrupt through PCC
        m_sccDevice->InterruptOutput = [this](bool active) {
            m_pccDevice->SetDeviceInterrupt("scc", active);
        };

        // Wire SCSI interrupt through PCC
        m_scsiDevice->InterruptOutput = [this](bool active) {
            m_pccDevice->SetDeviceInterrupt("scsi", active);
        };

        // Attach memory and PCC to SCSI controller
        m_scsiDevice->AttachMemory(m_memory.get());
        m_scsiDevice->AttachPcc(m_pccDevice.get());
        m_pccDevice->SetScsiDevice(m_scsiDevice.get());
        // SCSI diagnostic logging disabled for performance
        // m_scsiDevice->DiagLog = [this](const std::string& msg) {
        //     if (m_traceWriter)
        //         *m_traceWriter << msg << "\n";
        //     m_consoleStringOutput(*this, to_hstring_from_std(msg + "\n"));
        // };

        // Wire SCC Channel A output to console
        m_sccDevice->GetChannelA().CharTransmitted = [this](uint8_t ch) {
            m_consoleCharOutput(*this, ch);
            if (m_traceWriter) m_traceWriter->put(static_cast<char>(ch));
        };

        // Wire SCC Channel B -> also to console output (kernel may use either channel)
        m_sccDevice->GetChannelB().CharTransmitted = [this](uint8_t ch) {
            m_consoleCharOutput(*this, ch);
            if (m_traceWriter) m_traceWriter->put(static_cast<char>(ch));
        };

        // Virtual 16550 UART at 0xFFFE2000 for Linux serial console
        // Only register for Linux -- NetBSD does not expect a device at this address
        // and will crash if it probes one during bus scanning.
        if (m_config.TargetOS == "Linux") {
            m_uartDevice = std::make_unique<::Em68030::IO::Uart16550Device>(0xFFFE2000);
            m_uartDevice->OnTransmit = [this](uint8_t ch) {
                m_consoleCharOutput(*this, ch);
                if (m_traceWriter) m_traceWriter->put(static_cast<char>(ch));
            };
            m_memory->RegisterDevice(0xFFFE2000, 8, m_uartDevice.get());
        }

        // CPU tick handlers for PCC timer, SCC, and LANCE TX
        m_cpu->AddTickHandler([this]() {
            m_pccDevice->Tick();
            m_sccDevice->Tick(m_cpu->Stopped);
            m_lanceDevice->Tick();



        });

        // RESET instruction: reset all external devices (PCC timers, interrupt lines)
        m_cpu->OnResetInstruction = [this]() {
            m_pccDevice->HardwareReset();
        };

        // PCC watchdog timer: Linux MVME147 uses this for hardware reboot
        m_pccDevice->OnWatchdogReset = [this]() {
            if (m_cpu->DiagnosticOutput)
                m_cpu->DiagnosticOutput("\n[EMU] Watchdog reset triggered — performing warm reboot\n");
            m_pccDevice->HardwareReset();
            if (m_scsiDevice) m_scsiDevice->ResetBusState();
            m_systemBooted = false;

            // Disable MMU before reloading the kernel — the old page tables will be
            // overwritten by LoadElf, so any MMU-translated fetch would use corrupt tables.
            // FlushAll triggers OnFlush which invalidates fetch/data caches and JIT.
            m_cpu->GetMmu().Reset();
            m_cpu->GetMmu().FlushAll();

            if (!m_config.LastOpenedFile.empty() && std::filesystem::exists(m_config.LastOpenedFile))
            {
                auto result = ::Em68030::IO::FileLoader::LoadElf(*m_memory, m_config.LastOpenedFile);
                m_cpu->PC = result.EntryPoint;
                m_programStartAddress = result.StartAddress;
                m_programEndAddress = result.EndAddress;

                if (m_config.BoardType == "MVME147")
                {
                    uint32_t topOfRam = static_cast<uint32_t>(m_config.MemorySize);
                    if (m_config.TargetOS == "Linux")
                        SetupMvme147LinuxBootStub(topOfRam, m_programEndAddress);
                    else
                        SetupMvme147BootStub(topOfRam);
                    m_cpu->SR = 0x2700;
                }
            }
            else
            {
                m_cpu->Reset();
            }
        };

        // Diagnostic output: trace file + console window for critical messages
        m_cpu->DiagnosticOutput = [this](const std::string& msg) {
            if (m_traceWriter)
                *m_traceWriter << msg;
            // Show tagged diagnostic messages on console
            if (msg.find("[EMU]") != std::string::npos)
                m_consoleStringOutput(*this, to_hstring_from_std(msg));
        };

        // Load ROM image if configured
        if (!m_config.Mvme147RomPath.empty() && std::filesystem::exists(m_config.Mvme147RomPath))
        {
            std::ifstream romFile(m_config.Mvme147RomPath, std::ios::binary);
            if (romFile)
            {
                std::vector<uint8_t> rom((std::istreambuf_iterator<char>(romFile)),
                                          std::istreambuf_iterator<char>());
                m_memory->LoadData(0xFF800000, rom);
            }
        }

        // Mount SCSI disk images if configured
        m_scsiDisks.clear();
        for (const auto& diskConfig : m_config.Mvme147ScsiDisks)
        {
            if (!diskConfig.Path.empty() && std::filesystem::exists(diskConfig.Path))
            {
                if (m_config.TargetOS != "Linux")
                    EnsureCpuDisklabel(diskConfig.Path);
                auto disk = std::make_unique<::Em68030::IO::ScsiDisk>();
                disk->MountImage(diskConfig.Path);
                m_scsiDevice->AttachTarget(diskConfig.ScsiId, disk.get());
                m_scsiDisks.push_back(std::move(disk));
            }
        }
        m_scsiCdrom = std::make_unique<::Em68030::IO::ScsiCdrom>();
        if (!m_config.Mvme147ScsiCdromPath.empty() &&
            std::filesystem::exists(m_config.Mvme147ScsiCdromPath))
        {
            m_scsiCdrom->MountImage(m_config.Mvme147ScsiCdromPath);
        }
        m_scsiCdromId = m_config.Mvme147ScsiCdromId;
        m_scsiDevice->AttachTarget(m_scsiCdromId, m_scsiCdrom.get());

        // Create generic devices (unused but avoid null refs)
        m_consoleDevice = std::make_unique<::Em68030::IO::ConsoleDevice>(m_config.ConsoleBaseAddress);
        m_hddDevice = std::make_unique<::Em68030::IO::HddDevice>(m_config.HddBaseAddress);
    }

    void MainViewModel::SetupTrapHandler()
    {
        m_cpu->TrapExecuted = [this](int trapNum) {
            if (m_config.BoardType == "Generic" && trapNum == 15)
            {
                m_consoleDevice->HandleTrap(*m_cpu);
                m_cpu->TrapHandled = true;
            }
            else if (m_config.BoardType == "MVME147" && trapNum == 15)
            {
                Handle147BugCall();
                m_cpu->TrapHandled = true;
            }
        };
    }

    void MainViewModel::Handle147BugCall()
    {
        // Read inline function code from PC and advance past it
        uint16_t funcCode = m_cpu->ReadWord(m_cpu->PC);
        m_cpu->PC += 2;

        if (m_cpu->DiagnosticOutput)
            m_cpu->DiagnosticOutput(std::format("\n[EMU] TRAP #15 function code: 0x{:04X} at PC=${:08X}\n", funcCode, m_cpu->PC - 4));

        switch (funcCode)
        {
            case 0x0000: // .INCHR
                if (m_consoleCharInput)
                {
                    char ch = m_consoleCharInput();
                    m_cpu->D[0] = (m_cpu->D[0] & 0xFFFFFF00) | static_cast<uint8_t>(ch);
                }
                break;

            case 0x0001: // .INSTAT
                m_cpu->SetFlagZ(true);
                break;

            case 0x0020: // .OUTCHR
                m_consoleCharOutput(*this, static_cast<uint8_t>(m_cpu->D[0] & 0xFF));
                break;

            case 0x0021: // .OUTSTR
            {
                uint32_t addr = m_cpu->A[0];
                for (int i = 0; i < 4096; i++)
                {
                    uint8_t b = m_cpu->ReadByte(addr++);
                    if (b == 0) break;
                    m_consoleCharOutput(*this, b);
                }
                break;
            }

            case 0x0022: // .OUTLN
            {
                uint32_t addr = m_cpu->A[0];
                for (int i = 0; i < 4096; i++)
                {
                    uint8_t b = m_cpu->ReadByte(addr++);
                    if (b == 0) break;
                    m_consoleCharOutput(*this, b);
                }
                m_consoleCharOutput(*this, static_cast<uint8_t>('\r'));
                m_consoleCharOutput(*this, static_cast<uint8_t>('\n'));
                break;
            }

            case 0x0026: // .PCRLF
                m_consoleCharOutput(*this, static_cast<uint8_t>('\r'));
                m_consoleCharOutput(*this, static_cast<uint8_t>('\n'));
                break;

            case 0x0053: // .RTC_RD - Read Real Time Clock
            {
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                struct tm tm_buf;
                gmtime_s(&tm_buf, &time_t_now);

                m_cpu->D[0] = (m_cpu->D[0] & 0xFFFFFF00) | ToBcd((tm_buf.tm_year - 68) % 100); // YEAR0=1968
                m_cpu->D[1] = (m_cpu->D[1] & 0xFFFFFF00) | ToBcd(tm_buf.tm_mon + 1);
                m_cpu->D[2] = (m_cpu->D[2] & 0xFFFFFF00) | ToBcd(tm_buf.tm_mday);
                m_cpu->D[3] = (m_cpu->D[3] & 0xFFFFFF00) | ToBcd(tm_buf.tm_hour);
                m_cpu->D[4] = (m_cpu->D[4] & 0xFFFFFF00) | ToBcd(tm_buf.tm_min);
                m_cpu->D[5] = (m_cpu->D[5] & 0xFFFFFF00) | ToBcd(tm_buf.tm_sec);
                break;
            }

            case 0x0060: // .RETURN (alias)
            case 0x0063: // .RETURN / .EXIT - Return to Bug monitor
                m_cpu->Halted = true;
                m_cpu->StopReason = "147Bug .RETURN";
                break;

            case 0x0070: // .BRD_ID
            {
                if (m_systemBooted)
                {
                    // Warm reboot detected: the kernel restarted from the entry point.
                    // On real hardware, the Bug monitor would reload the kernel from disk.
                    // We reload the ELF to give the kernel fresh .text/.data/.bss sections.
                    if (m_cpu->DiagnosticOutput)
                        m_cpu->DiagnosticOutput("\n[EMU] Warm reboot detected — reloading kernel and resetting\n");
                    if (m_pccDevice)
                        m_pccDevice->HardwareReset();
                    m_systemBooted = false;

                    if (!m_config.LastOpenedFile.empty() && std::filesystem::exists(m_config.LastOpenedFile))
                    {
                        auto result = ::Em68030::IO::FileLoader::LoadElf(*m_memory, m_config.LastOpenedFile);
                        m_cpu->PC = result.EntryPoint;
                        m_programStartAddress = result.StartAddress;
                        m_programEndAddress = result.EndAddress;

                        if (m_config.BoardType == "MVME147")
                        {
                            uint32_t topOfRam = static_cast<uint32_t>(m_config.MemorySize);
                            if (m_config.TargetOS == "Linux")
                                SetupMvme147LinuxBootStub(topOfRam, m_programEndAddress);
                            else
                                SetupMvme147BootStub(topOfRam);
                            m_cpu->SR = 0x2700;
                        }
                    }
                    else
                    {
                        m_cpu->Reset();
                    }
                    return;
                }
                m_systemBooted = true;
                uint32_t sp = m_cpu->A[7];
                m_cpu->WriteLong(sp, m_brdIdAddress);
                break;
            }

            default:
            {
                std::string msg = std::format("[147Bug] Unknown TRAP #15 function ${:04X} at PC=${:08X}\n",
                    funcCode, m_cpu->PC - 2);
                m_consoleStringOutput(*this, to_hstring_from_std(msg));
                break;
            }
        }
    }

    uint8_t MainViewModel::ToBcd(int val)
    {
        return static_cast<uint8_t>(((val / 10) << 4) | (val % 10));
    }

    void MainViewModel::WriteBoardIdPacket(uint32_t addr)
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        localtime_s(&tm_buf, &time_t_now);

        m_memory->PokeLong(addr + 0, 0x01234567);     // eye_catcher
        m_memory->PokeByte(addr + 4, 0x01);            // rev
        m_memory->PokeByte(addr + 5, static_cast<uint8_t>(tm_buf.tm_mon + 1));
        m_memory->PokeByte(addr + 6, static_cast<uint8_t>(tm_buf.tm_mday));
        m_memory->PokeByte(addr + 7, static_cast<uint8_t>(tm_buf.tm_year % 100));
        m_memory->PokeWord(addr + 8, 0x00E0);          // size
        m_memory->PokeWord(addr + 10, 0x0000);          // rsv1
        m_memory->PokeWord(addr + 12, 0x0147);          // model = MVME_147
        m_memory->PokeWord(addr + 14, 0x0053);          // suffix = 'S'
        m_memory->PokeWord(addr + 16, 0x0002);          // options
        m_memory->PokeByte(addr + 18, 0x01);            // family (68K)
        m_memory->PokeByte(addr + 19, 0x03);            // cpu (68030)
        m_memory->PokeWord(addr + 20, 0x0000);          // ctrlun
        m_memory->PokeWord(addr + 22, 0x0000);          // devlun
        m_memory->PokeWord(addr + 24, 0x0000);          // devtype
        m_memory->PokeWord(addr + 26, 0x0000);          // devnum
        m_memory->PokeLong(addr + 28, 0x01470000);      // bug version

        // longname at offset 52: "MVME147-010 "
        const char* longname = "MVME147-010 ";
        for (int i = 0; i < 12; i++)
            m_memory->PokeByte(addr + 52 + static_cast<uint32_t>(i),
                i < 12 ? static_cast<uint8_t>(longname[i]) : 0);

        // speed at offset 80: "2500" (25.00 MHz)
        const char* speed = "2500";
        for (int i = 0; i < 4; i++)
            m_memory->PokeByte(addr + 80 + static_cast<uint32_t>(i),
                static_cast<uint8_t>(speed[i]));
    }

    void MainViewModel::SetupMvme147BootStub(uint32_t topOfRam)
    {
        uint32_t ssp = topOfRam - 0x3000;

        // Board ID packet in high RAM
        m_brdIdAddress = topOfRam - 0x2000;
        WriteBoardIdPacket(m_brdIdAddress);

        // NOTE: We do NOT write a vector table or RTE handler to PA=$0-$401.
        // SetupMvme147BootStub() is called AFTER LoadElf() has loaded the kernel,
        // so writing to PA=$0-$401 would OVERWRITE kernel data/code.
        // The kernel sets its own VBR early in locore.s before any exceptions occur.

        // Boot parameters on stack (like real 147Bug bootloader)
        // The kernel entry point (locore.s:start) reads boot arguments from the
        // stack. Without these, bootaddr/bootdevlun are garbage, causing:
        //   - "boot device: <unknown>" (autoconf.c can't match WDSC controller)
        //   - Incorrect root device detection
        //
        // Stack layout (locore.s reads from SP@(4)..SP@(24)):
        //   SP@(0)  : return address (dummy)
        //   SP@(4)  : boothowto   (boot flags, 0=normal)
        //   SP@(8)  : bootaddr    (controller physical address, 0xFFFE3000 for WDSC)
        //   SP@(12) : bootctrllun (controller LUN, 0)
        //   SP@(16) : bootdevlun  (SCSI target ID of boot disk)
        //   SP@(20) : bootpart    (partition number, 0='a')
        //   SP@(24) : esyms       (end of symbol table, 0=none)
        constexpr uint32_t PCC_WDSC_ADDR = 0xFFFE3000;
        uint32_t bootdevlun = static_cast<uint32_t>(m_config.Mvme147ScsiDisks.empty() ? 0 : m_config.Mvme147ScsiDisks[0].ScsiId);
        uint32_t bootArgs = ssp - 28; // 7 longwords below SSP
        m_memory->PokeLong(bootArgs + 0,  0);              // return address (dummy)
        m_memory->PokeLong(bootArgs + 4,  0);              // boothowto (0 = normal)
        m_memory->PokeLong(bootArgs + 8,  PCC_WDSC_ADDR);  // bootaddr (WDSC)
        m_memory->PokeLong(bootArgs + 12, 0);              // bootctrllun (0)
        m_memory->PokeLong(bootArgs + 16, bootdevlun);     // bootdevlun (SCSI ID)
        m_memory->PokeLong(bootArgs + 20, static_cast<uint32_t>(m_config.Mvme147BootPartition)); // bootpart (0='a', 1='b', ...)
        m_memory->PokeLong(bootArgs + 24, 0);              // esyms (0 = none)

        m_cpu->VBR = 0;
        m_cpu->SSP = bootArgs;
        m_cpu->A[7] = bootArgs;
    }

    void MainViewModel::SetupMvme147LinuxBootStub(uint32_t topOfRam, uint32_t endOfKernel)
    {
        // Linux/m68k boot protocol: the kernel's get_bi_record() in head.S
        // searches for bi_record structures starting at _end (the end of
        // the kernel image), NOT from a register pointer.
        //
        // struct bi_record {
        //     uint16_t tag;    // record type
        //     uint16_t size;   // total size in bytes (header + data)
        //     uint32_t data[]; // payload
        // };
        //
        // Reference: arch/m68k/kernel/head.S (get_bi_record: lea %pc@(_end),%a0)
        //            arch/m68k/include/uapi/asm/bootinfo.h

        uint32_t ssp = topOfRam - 0x3000;

        // Place bootinfo chain at _end (end of kernel image), aligned to 4 bytes
        uint32_t biAddr = (endOfKernel + 3) & ~3u;

        // --- BI_MACHTYPE (tag=0x0001): machine type ---
        constexpr uint32_t MACH_MVME147 = 6;
        m_memory->PokeWord(biAddr + 0, 0x0001); // BI_MACHTYPE
        m_memory->PokeWord(biAddr + 2, 8);      // size = 4 (header) + 4 (data)
        m_memory->PokeLong(biAddr + 4, MACH_MVME147);
        biAddr += 8;

        // --- BI_CPUTYPE (tag=0x0002): CPU_68030 = (1 << 1) ---
        m_memory->PokeWord(biAddr + 0, 0x0002); // BI_CPUTYPE
        m_memory->PokeWord(biAddr + 2, 8);
        m_memory->PokeLong(biAddr + 4, (1 << 1)); // CPU_68030
        biAddr += 8;

        // --- BI_FPUTYPE (tag=0x0003): FPU_68882 = (1 << 1) ---
        m_memory->PokeWord(biAddr + 0, 0x0003); // BI_FPUTYPE
        m_memory->PokeWord(biAddr + 2, 8);
        m_memory->PokeLong(biAddr + 4, (1 << 1)); // FPU_68882
        biAddr += 8;

        // --- BI_MMUTYPE (tag=0x0004): MMU_68030 = (1 << 1) ---
        m_memory->PokeWord(biAddr + 0, 0x0004); // BI_MMUTYPE
        m_memory->PokeWord(biAddr + 2, 8);
        m_memory->PokeLong(biAddr + 4, (1 << 1)); // MMU_68030
        biAddr += 8;

        // --- BI_MEMCHUNK (tag=0x0005): memory region ---
        m_memory->PokeWord(biAddr + 0, 0x0005); // BI_MEMCHUNK
        m_memory->PokeWord(biAddr + 2, 12);     // size = 4 (header) + 8 (start + size)
        m_memory->PokeLong(biAddr + 4, 0);      // start address
        m_memory->PokeLong(biAddr + 8, topOfRam); // size
        biAddr += 12;

        // --- BI_COMMAND_LINE (tag=0x0007): kernel command line ---
        const std::string& cmdline = m_config.LinuxCommandLine;
        uint32_t cmdLen = static_cast<uint32_t>(cmdline.size()) + 1; // include NUL
        uint32_t cmdRecSize = 4 + ((cmdLen + 3) & ~3u); // header + padded string
        m_memory->PokeWord(biAddr + 0, 0x0007); // BI_COMMAND_LINE
        m_memory->PokeWord(biAddr + 2, static_cast<uint16_t>(cmdRecSize));
        for (uint32_t i = 0; i < cmdLen; i++)
            m_memory->PokeByte(biAddr + 4 + i, i < cmdline.size() ? static_cast<uint8_t>(cmdline[i]) : 0);
        // Zero-fill padding
        for (uint32_t i = cmdLen; i < ((cmdLen + 3) & ~3u); i++)
            m_memory->PokeByte(biAddr + 4 + i, 0);
        biAddr += cmdRecSize;

        // --- BI_VME_TYPE (tag=0x8000): VME board type ---
        constexpr uint16_t VME_TYPE_MVME147 = 0x0147;
        m_memory->PokeWord(biAddr + 0, 0x8000); // BI_VME_TYPE
        m_memory->PokeWord(biAddr + 2, 8);      // size = 4 + 4
        m_memory->PokeLong(biAddr + 4, VME_TYPE_MVME147);
        biAddr += 8;

        // --- BI_VME_BRDINFO (tag=0x8001): board info (struct mvme_brdinfo) ---
        // Minimal board info: 24 bytes (offset, tag, clun, dlun, ctype, dtype, session, etc.)
        m_memory->PokeWord(biAddr + 0, 0x8001); // BI_VME_BRDINFO
        m_memory->PokeWord(biAddr + 2, 4 + 24); // size = header + 24 bytes of brdinfo
        // Zero-fill brdinfo (kernel uses defaults for most fields)
        for (uint32_t i = 0; i < 24; i++)
            m_memory->PokeByte(biAddr + 4 + i, 0);
        biAddr += 4 + 24;

        // --- BI_LAST (tag=0x0000): end of chain ---
        m_memory->PokeWord(biAddr + 0, 0x0000); // BI_LAST
        m_memory->PokeWord(biAddr + 2, 4);      // size = 4 (header only)

        // Set up CPU state
        m_cpu->VBR = 0;
        m_cpu->SSP = ssp;
        m_cpu->A[7] = ssp;
    }

    void MainViewModel::EnsureCpuDisklabel(const std::string& path)
    {
        // Check if VID/CFG magic values are present. The kernel's readdisklabel()
        // requires both magic1 (offset 0x3A) and magic2 (offset 0x13C) to equal
        // DISKMAGIC (0x82564557). If either is missing, write a fresh disklabel.
        constexpr uint32_t DISKMAGIC = 0x82564557;
        {
            std::ifstream fs(path, std::ios::binary);
            if (!fs) return;
            fs.seekg(0, std::ios::end);
            if (fs.tellg() < 512) return;
            fs.seekg(0, std::ios::beg);
            uint8_t sector[512];
            fs.read(reinterpret_cast<char*>(sector), 512);
            uint32_t magic1 = (uint32_t(sector[0x3A]) << 24) | (uint32_t(sector[0x3B]) << 16)
                            | (uint32_t(sector[0x3C]) << 8) | sector[0x3D];
            uint32_t magic2 = (uint32_t(sector[0x13C]) << 24) | (uint32_t(sector[0x13D]) << 16)
                            | (uint32_t(sector[0x13E]) << 8) | sector[0x13F];
            if (magic1 == DISKMAGIC && magic2 == DISKMAGIC)
                return; // VID is valid
        }
        ::Em68030::IO::ScsiDisk::WriteNetBsdDisklabel(path);
    }

    void MainViewModel::InitStackPointer()
    {
        uint32_t ssp = m_memory->PeekLong(0);
        if (ssp != 0)
        {
            m_cpu->SSP = ssp;
            m_cpu->A[7] = ssp;
        }
    }

    void MainViewModel::CheckForLstFile(const std::string& filePath)
    {
        m_lstFilePath = ::Em68030::IO::FileLoader::FindLstFile(filePath);
        if (!m_lstFilePath.empty())
        {
            m_lstLines = ::Em68030::IO::FileLoader::LoadLstFile(m_lstFilePath);
            m_hasLstLines = true;
        }
        else
        {
            m_lstLines.clear();
            m_hasLstLines = false;
        }
        RaisePropertyChanged(L"HasLstFile");
    }

    // ======================================================================
    // File loading
    // ======================================================================

    void MainViewModel::LoadBinaryFile(hstring const& path, uint32_t loadAddress)
    {
        std::string pathStr = to_std_string(path);
        uint32_t size = ::Em68030::IO::FileLoader::LoadBinary(*m_memory, pathStr, loadAddress);
        m_cpu->PC = loadAddress;
        m_programStartAddress = loadAddress;
        m_programEndAddress = loadAddress + size;
        SetDisasmFollowPC(true);
        m_fullProgramDisassembled = false;
        ClearManualDisasmMode();
        InitStackPointer();

        // Extract filename
        auto fsPath = std::filesystem::path(pathStr);
        m_loadedFileName = to_hstring_from_std(fsPath.filename().string());
        RaisePropertyChanged(L"LoadedFileName");

        CheckForLstFile(pathStr);
        m_config.LastOpenedFile = pathStr;
        m_config.LastLoadAddress = loadAddress;
        m_config.Save();
        RefreshAll();
    }

    void MainViewModel::LoadSRecordFile(hstring const& path)
    {
        std::string pathStr = to_std_string(path);
        auto [start, end, entry, hasEntry] = ::Em68030::IO::FileLoader::LoadSRecord(*m_memory, pathStr);

        if (hasEntry)
        {
            m_cpu->PC = entry;
            m_programStartAddress = entry;
        }
        else
        {
            m_cpu->PC = start;
            m_programStartAddress = start;
        }
        m_programEndAddress = end;
        SetDisasmFollowPC(true);
        m_fullProgramDisassembled = false;
        ClearManualDisasmMode();
        InitStackPointer();

        auto fsPath = std::filesystem::path(pathStr);
        m_loadedFileName = to_hstring_from_std(fsPath.filename().string());
        RaisePropertyChanged(L"LoadedFileName");

        CheckForLstFile(pathStr);
        m_config.LastOpenedFile = pathStr;
        m_config.Save();
        RefreshAll();
    }

    ::Em68030::IO::ElfLoadResult MainViewModel::LoadElfFileNative(const std::string& path)
    {
        auto result = ::Em68030::IO::FileLoader::LoadElf(*m_memory, path);
        m_cpu->PC = result.EntryPoint;
        m_programStartAddress = result.StartAddress;
        m_programEndAddress = result.EndAddress;
        SetDisasmFollowPC(true);
        m_fullProgramDisassembled = false;
        ClearManualDisasmMode();

        if (m_config.BoardType == "MVME147")
        {
            uint32_t topOfRam = static_cast<uint32_t>(m_config.MemorySize);
            if (m_config.TargetOS == "Linux")
                SetupMvme147LinuxBootStub(topOfRam, m_programEndAddress);
            else
                SetupMvme147BootStub(topOfRam);
            m_cpu->SR = 0x2700; // Supervisor mode, IPL 7
        }
        else
        {
            InitStackPointer();
        }

        auto fsPath = std::filesystem::path(path);
        m_loadedFileName = to_hstring_from_std(fsPath.filename().string());
        RaisePropertyChanged(L"LoadedFileName");

        m_config.LastOpenedFile = path;
        m_config.Save();
        RefreshAll();
        return result;
    }

    // ======================================================================
    // Console input
    // ======================================================================

    void MainViewModel::SendConsoleChar(uint8_t ch)
    {
        // In MVME147 mode, feed into SCC Channel A user input staging queue
        if (m_sccDevice)
            m_sccDevice->GetChannelA().QueueInput(ch);
        // Also feed into virtual 16550 UART RX buffer (for Linux serial console)
        if (m_uartDevice)
            m_uartDevice->ReceiveChar(ch);
    }

    // ======================================================================
    // Execution control
    // ======================================================================

    void MainViewModel::Step()
    {
        if (m_cpu->Halted) return;
        if (m_cpu->Stopped && !m_cpu->HasExternalDevices()) return;
        m_cpu->ExecuteStep();
        RefreshAll();
    }

    void MainViewModel::Run()
    {
        if (m_cpu->Halted) return;
        if (m_cpu->Stopped && !m_cpu->HasExternalDevices()) return;
        m_runToCursorAddress = std::nullopt;
        StartEmulation();
    }

    void MainViewModel::RunToCursor(uint32_t address)
    {
        if (m_cpu->Halted) return;
        if (m_cpu->Stopped && !m_cpu->HasExternalDevices()) return;
        m_runToCursorAddress = address;
        StartEmulation();
    }

    void MainViewModel::StartEmulation()
    {
        if (m_emulationThread.joinable()) return; // Already running

        m_stopRequested = false;
        m_isRunning = true;
        RaisePropertyChanged(L"IsRunning");
        RaiseAllCommandsCanExecuteChanged();

        m_mhzCyclesSnapshot = m_cpu->CycleCount;
        m_mipsInsnSnapshot = m_cpu->InstructionCount;
        m_mhzTimestamp = std::chrono::steady_clock::now();
        m_estimatedMHz = 0.0;
        m_estimatedMips = 0.0;

        m_runStartCycleCount = m_cpu->CycleCount;
        m_runStartInsnCount = m_cpu->InstructionCount;
        m_runStartTimestamp = std::chrono::steady_clock::now();
        m_avgMHz = 0.0;
        m_avgMips = 0.0;

        m_emulationThread = std::thread([this]() { EmulationThreadLoop(); });
    }

    void MainViewModel::EmulationThreadLoop()
    {
        bool hasBreakpoints = !m_enabledBreakpoints.empty();
        bool hasRunToCursor = m_runToCursorAddress.has_value();
        uint32_t runToCursorAddr = m_runToCursorAddress.value_or(0);
        auto lastMhzUpdate = std::chrono::steady_clock::now();

        // Select execution function once at loop start to avoid per-instruction branching.
        // ExecuteNextFast() is the interpreter-only path (no JIT code bloat).
        // ExecuteNextFastJit() adds JIT block lookup + sampling.
        using ExecFn = bool (::Em68030::Core::MC68030::*)();
        ExecFn execFn = m_cpu->JitEnabled
            ? &::Em68030::Core::MC68030::ExecuteNextFastJit
            : &::Em68030::Core::MC68030::ExecuteNextFast;

        // Use large batch size; no yield() — this thread owns a full core
        constexpr int BatchSize = 100000;

        try
        {
            while (!m_stopRequested.load(std::memory_order_relaxed))
            {
                if (m_cpu->Halted) { RequestStopOnUI(); return; }
                if (m_cpu->Stopped && !m_cpu->HasExternalDevices()) { RequestStopOnUI(); return; }

                if (!hasBreakpoints && !hasRunToCursor)
                {
                    // ---- Fast path: no breakpoints, no run-to-cursor ----
                    uint32_t loopDetectPC = UINT32_MAX;
                    int loopDetectCount = 0;
                    for (int i = 0; i < BatchSize; i++)
                    {
                        try
                        {
                            if (!(m_cpu.get()->*execFn)())
                            {
                                if (m_cpu->Halted || (!m_cpu->HasExternalDevices() && m_cpu->Stopped))
                                { RequestStopOnUI(); return; }
                                continue;
                            }
                        }
                        catch (const ::Em68030::Core::BusErrorException& ex)
                        {
                            m_cpu->HandleBusError(ex);
                        }
                        if (m_cpu->Halted) { RequestStopOnUI(); return; }
                        // Infinite loop detection (e.g. kernel panic for(;;);)
                        if (m_cpu->PC == loopDetectPC)
                        {
                            if (++loopDetectCount >= 1000)
                            {
                                m_cpu->Halted = true;
                                m_cpu->StopReason = std::format("Infinite loop at ${:08X}", m_cpu->PC);
                                if (m_cpu->DiagnosticOutput)
                                    m_cpu->DiagnosticOutput(std::format("\n[EMU] Infinite loop detected at PC=${:08X}, SR=${:04X} — halting\n", m_cpu->PC, m_cpu->SR));
                                RequestStopOnUI();
                                return;
                            }
                        }
                        else { loopDetectPC = m_cpu->PC; loopDetectCount = 0; }
                    }
                }
                else
                {
                    // ---- Slow path: breakpoint / run-to-cursor checks ----
                    uint32_t loopDetectPC = UINT32_MAX;
                    int loopDetectCount = 0;
                    for (int i = 0; i < BatchSize; i++)
                    {
                        try
                        {
                            if (!(m_cpu.get()->*execFn)())
                            {
                                if (m_cpu->Halted || (!m_cpu->HasExternalDevices() && m_cpu->Stopped))
                                { RequestStopOnUI(); return; }
                                continue;
                            }
                        }
                        catch (const ::Em68030::Core::BusErrorException& ex)
                        {
                            m_cpu->HandleBusError(ex);
                        }

                        if (m_cpu->Halted) { RequestStopOnUI(); return; }
                        if (hasBreakpoints && m_enabledBreakpoints.contains(m_cpu->PC))
                        {
                            m_cpu->StopReason = std::format("Breakpoint at ${:08X}", m_cpu->PC);
                            RequestStopOnUI();
                            return;
                        }
                        if (hasRunToCursor && m_cpu->PC == runToCursorAddr)
                        { RequestStopOnUI(); return; }
                        // Infinite loop detection
                        if (m_cpu->PC == loopDetectPC)
                        {
                            if (++loopDetectCount >= 1000)
                            {
                                m_cpu->Halted = true;
                                m_cpu->StopReason = std::format("Infinite loop at ${:08X}", m_cpu->PC);
                                if (m_cpu->DiagnosticOutput)
                                    m_cpu->DiagnosticOutput(std::format("\n[EMU] Infinite loop detected at PC=${:08X}, SR=${:04X} — halting\n", m_cpu->PC, m_cpu->SR));
                                RequestStopOnUI();
                                return;
                            }
                        }
                        else { loopDetectPC = m_cpu->PC; loopDetectCount = 0; }
                    }
                }

                // Periodic MHz display update (~every 500ms)
                auto now = std::chrono::steady_clock::now();
                double seconds = std::chrono::duration<double>(now - lastMhzUpdate).count();
                if (seconds >= 0.5)
                {
                    int64_t cycles = static_cast<int64_t>(m_cpu->CycleCount) - m_mhzCyclesSnapshot;
                    m_estimatedMHz = static_cast<double>(cycles) / seconds / 1'000'000.0;
                    m_mhzCyclesSnapshot = m_cpu->CycleCount;

                    int64_t insns = static_cast<int64_t>(m_cpu->InstructionCount) - m_mipsInsnSnapshot;
                    m_estimatedMips = static_cast<double>(insns) / seconds / 1'000'000.0;
                    m_mipsInsnSnapshot = m_cpu->InstructionCount;

                    lastMhzUpdate = now;
                    m_mhzTimestamp = now;

                    // Cumulative average since Run started
                    double totalSec = std::chrono::duration<double>(now - m_runStartTimestamp).count();
                    if (totalSec > 0.01) {
                        m_avgMHz = static_cast<double>(m_cpu->CycleCount - m_runStartCycleCount) / totalSec / 1'000'000.0;
                        m_avgMips = static_cast<double>(m_cpu->InstructionCount - m_runStartInsnCount) / totalSec / 1'000'000.0;
                    }

                    if (m_dispatcherQueue)
                    {
                        m_dispatcherQueue.TryEnqueue([this]() {
                            RaisePropertyChanged(L"EstimatedMHz");
                            RaisePropertyChanged(L"CycleCount");
                        });
                    }
                }
            }
        }
        catch (const std::exception& ex)
        {
            // Catch any unhandled exception to prevent thread crash.
            m_cpu->Halted = true;
            m_cpu->StopReason = std::format("Unhandled exception: {}", ex.what());
            RequestStopOnUI();
            return;
        }
        catch (...)
        {
            m_cpu->Halted = true;
            m_cpu->StopReason = "Unhandled unknown exception";
            RequestStopOnUI();
            return;
        }
    }

    void MainViewModel::RequestStopOnUI()
    {
        if (m_dispatcherQueue)
        {
            m_dispatcherQueue.TryEnqueue([this]() { FinalizeStop(); });
        }
    }

    void MainViewModel::FinalizeStop()
    {
        if (m_emulationThread.joinable())
            m_emulationThread.join();

        m_runToCursorAddress = std::nullopt;
        m_isRunning = false;
        RaisePropertyChanged(L"IsRunning");
        RaiseAllCommandsCanExecuteChanged();

        // Final MHz/MIPS calculation
        auto now = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(now - m_mhzTimestamp).count();
        if (seconds > 0.01)
        {
            int64_t cycles = static_cast<int64_t>(m_cpu->CycleCount) - m_mhzCyclesSnapshot;
            m_estimatedMHz = static_cast<double>(cycles) / seconds / 1'000'000.0;
            int64_t insns = static_cast<int64_t>(m_cpu->InstructionCount) - m_mipsInsnSnapshot;
            m_estimatedMips = static_cast<double>(insns) / seconds / 1'000'000.0;
        }
        // Final average calculation
        double totalSec = std::chrono::duration<double>(now - m_runStartTimestamp).count();
        if (totalSec > 0.01) {
            m_avgMHz = static_cast<double>(m_cpu->CycleCount - m_runStartCycleCount) / totalSec / 1'000'000.0;
            m_avgMips = static_cast<double>(m_cpu->InstructionCount - m_runStartInsnCount) / totalSec / 1'000'000.0;
        }
        RefreshAll();
    }

    void MainViewModel::StopEmulation()
    {
        m_stopRequested = true;
        if (m_emulationThread.joinable())
            m_emulationThread.join();

        m_runToCursorAddress = std::nullopt;
        m_isRunning = false;
        RaisePropertyChanged(L"IsRunning");
        RaiseAllCommandsCanExecuteChanged();

        // Final MHz/MIPS calculation
        auto now = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(now - m_mhzTimestamp).count();
        if (seconds > 0.01)
        {
            int64_t cycles = static_cast<int64_t>(m_cpu->CycleCount) - m_mhzCyclesSnapshot;
            m_estimatedMHz = static_cast<double>(cycles) / seconds / 1'000'000.0;
            int64_t insns = static_cast<int64_t>(m_cpu->InstructionCount) - m_mipsInsnSnapshot;
            m_estimatedMips = static_cast<double>(insns) / seconds / 1'000'000.0;
        }
        // Final average calculation
        double totalSec = std::chrono::duration<double>(now - m_runStartTimestamp).count();
        if (totalSec > 0.01) {
            m_avgMHz = static_cast<double>(m_cpu->CycleCount - m_runStartCycleCount) / totalSec / 1'000'000.0;
            m_avgMips = static_cast<double>(m_cpu->InstructionCount - m_runStartInsnCount) / totalSec / 1'000'000.0;
        }
        RefreshAll();
    }

    void MainViewModel::Stop()
    {
        StopEmulation();
    }

    void MainViewModel::DoReset()
    {
        m_systemBooted = false;
        m_cpu->Reset();
        RefreshAll();
    }

    void MainViewModel::DoFullReset()
    {
        if (m_isRunning)
            Stop();

        m_systemBooted = false;

        // Unmount SCSI disks and CD-ROM
        for (auto& disk : m_scsiDisks)
            disk->UnmountImage();
        m_scsiDisks.clear();
        if (m_scsiCdrom)
            m_scsiCdrom->UnmountImage();

        // Re-initialize console and HDD devices with defaults
        m_consoleDevice = std::make_unique<::Em68030::IO::ConsoleDevice>(m_config.ConsoleBaseAddress);
        m_hddDevice = std::make_unique<::Em68030::IO::HddDevice>(m_config.HddBaseAddress);

        // Re-run full hardware setup
        if (m_config.BoardType == "MVME147")
            SetupMvme147();
        else
            SetupGeneric();

        m_disassembler = std::make_unique<::Em68030::Core::Disassembler>(*m_memory);
        SetupTrapHandler();

        m_cpu->Reset();
        m_disasmAddress = m_cpu->PC;
        m_fullProgramDisassembled = false;
        m_programStartAddress = 0;
        m_programEndAddress = 0;

        RefreshAll();
    }

    // ======================================================================
    // Trace
    // ======================================================================

    void MainViewModel::ToggleTrace()
    {
        m_cpu->VerboseTrace = !m_cpu->VerboseTrace;
        if (m_cpu->VerboseTrace)
        {
            // Open trace log file next to the executable
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            auto traceDir = std::filesystem::path(exePath).parent_path();
            auto tracePath = traceDir / "tracelog.txt";
            try
            {
                m_traceWriter = std::make_unique<std::ofstream>(tracePath, std::ios::out | std::ios::trunc);
                std::string msg = std::format("\n[EMU] Verbose trace ON -> {}\n", tracePath.string());
                m_consoleStringOutput(*this, to_hstring_from_std(msg));
            }
            catch (...)
            {
                m_consoleStringOutput(*this, L"\n[EMU] Verbose trace ON (file open failed, console only)\n");
            }
        }
        else
        {
            if (m_traceWriter)
            {
                m_traceWriter->close();
                m_traceWriter.reset();
            }
            m_consoleStringOutput(*this, L"\n[EMU] Verbose trace OFF\n");
        }
    }

    // ======================================================================
    // Breakpoints / PC
    // ======================================================================

    void MainViewModel::SetPCToCursor(uint32_t address)
    {
        m_cpu->PC = address;
        RefreshAll();
    }

    void MainViewModel::ToggleBreakpoint(uint32_t address)
    {
        auto it = m_breakpoints.find(address);
        if (it != m_breakpoints.end())
            m_breakpoints.erase(it);
        else
            m_breakpoints[address] = BreakpointData{ address, true };
        RebuildEnabledSet();
        UpdateDisassembly();
    }

    void MainViewModel::EnableBreakpoint(uint32_t addr, bool enabled)
    {
        auto it = m_breakpoints.find(addr);
        if (it != m_breakpoints.end())
        {
            it->second.enabled = enabled;
            RebuildEnabledSet();
            UpdateDisassembly();
        }
    }

    void MainViewModel::RemoveBreakpoint(uint32_t addr)
    {
        m_breakpoints.erase(addr);
        RebuildEnabledSet();
        UpdateDisassembly();
    }

    void MainViewModel::ClearAllBreakpoints()
    {
        m_breakpoints.clear();
        m_enabledBreakpoints.clear();
        UpdateDisassembly();
    }

    void MainViewModel::ManualDisassembly(uint32_t address, uint32_t sizeBytes)
    {
        m_manualDisasmMode = true;
        SetDisasmFollowPC(false);
        m_fullProgramDisassembled = false;
        m_disasmAddress = address;
        UpdateDisassemblyRange(address, address + sizeBytes);
    }

    void MainViewModel::ClearManualDisasmMode()
    {
        m_manualDisasmMode = false;
    }

    void MainViewModel::RebuildEnabledSet()
    {
        m_enabledBreakpoints.clear();
        for (const auto& [addr, bp] : m_breakpoints)
        {
            if (bp.enabled)
                m_enabledBreakpoints.insert(addr);
        }
    }

    // ======================================================================
    // Config
    // ======================================================================

    void MainViewModel::UnmountAllScsiDisks()
    {
        for (auto& disk : m_scsiDisks)
            disk->UnmountImage();
        if (m_scsiCdrom)
            m_scsiCdrom->UnmountImage();
    }

    void MainViewModel::ApplyConfig(::Em68030::Config::EmulatorConfig const& newConfig)
    {
        // Unregister old devices
        m_memory->UnregisterDevice(m_config.ConsoleBaseAddress, 256);
        m_memory->UnregisterDevice(m_config.HddBaseAddress, 256);

        m_config = newConfig;

        // Re-register devices
        m_consoleDevice->SetBaseAddress(m_config.ConsoleBaseAddress);
        m_hddDevice->SetBaseAddress(m_config.HddBaseAddress);

        if (m_config.ConsoleEnabled)
            m_memory->RegisterDevice(m_config.ConsoleBaseAddress, 256, m_consoleDevice.get());
        if (m_config.HddEnabled)
            m_memory->RegisterDevice(m_config.HddBaseAddress, 256, m_hddDevice.get());

        if (!m_config.HddImagePath.empty() && std::filesystem::exists(m_config.HddImagePath))
            m_hddDevice->MountImage(m_config.HddImagePath);
        else
            m_hddDevice->UnmountImage();

        // Hot-swap SCSI disks (MVME147 mode only)
        if (m_scsiDevice)
        {
            // Detach all old SCSI disk IDs from the bus before destroying disk objects
            for (int id = 0; id < 7; id++)
            {
                if (id != m_scsiCdromId)
                    m_scsiDevice->DetachTarget(id);
            }

            // Close file handles and destroy old ScsiDisk objects
            for (auto& disk : m_scsiDisks)
                disk->UnmountImage();
            m_scsiDisks.clear();

            // Reset SCSI controller bus state to clear any in-flight operations
            m_scsiDevice->ResetBusState();

            // Mount and attach new SCSI disks from updated config
            for (const auto& diskConfig : m_config.Mvme147ScsiDisks)
            {
                if (!diskConfig.Path.empty() && std::filesystem::exists(diskConfig.Path))
                {
                    auto disk = std::make_unique<::Em68030::IO::ScsiDisk>();
                    disk->MountImage(diskConfig.Path);
                    m_scsiDevice->AttachTarget(diskConfig.ScsiId, disk.get());
                    m_scsiDisks.push_back(std::move(disk));
                }
            }

            // Hot-swap SCSI CD-ROM
            if (m_scsiCdrom)
            {
                // If SCSI ID changed, detach from old slot and attach to new slot
                if (m_scsiCdromId != m_config.Mvme147ScsiCdromId)
                {
                    m_scsiDevice->DetachTarget(m_scsiCdromId);
                    m_scsiCdromId = m_config.Mvme147ScsiCdromId;
                    m_scsiDevice->AttachTarget(m_scsiCdromId, m_scsiCdrom.get());
                }

                // Mount or unmount the ISO image
                if (!m_config.Mvme147ScsiCdromPath.empty() &&
                    std::filesystem::exists(m_config.Mvme147ScsiCdromPath))
                    m_scsiCdrom->MountImage(m_config.Mvme147ScsiCdromPath);
                else
                    m_scsiCdrom->UnmountImage();
            }
        }

        // JIT setting
        if (m_cpu)
        {
            m_cpu->JitEnabled = m_config.JitEnabled;
            m_cpu->JitMinBlockLength = m_config.JitMinBlockLength;
            m_cpu->JitCompileThreshold = static_cast<uint8_t>(m_config.JitCompileThreshold);
            if (!m_config.JitEnabled)
                m_cpu->InvalidateJitCache();
        }

        m_config.Save();
        RaisePropertyChanged(L"Config");
    }

    // ======================================================================
    // Navigation
    // ======================================================================

    void MainViewModel::NavigateMemoryDump(uint32_t address)
    {
        m_memoryDumpAddress = address;
        RaisePropertyChanged(L"MemoryDumpAddress");
        UpdateMemoryDump();
    }

    void MainViewModel::NavigateDisassembly(uint32_t address)
    {
        m_disasmAddress = address;
        SetDisasmFollowPC(false);
        m_fullProgramDisassembled = false;
        UpdateDisassemblyAt(m_disasmAddress);
    }

    void MainViewModel::ScrollToAddress(uint32_t address)
    {
        // Check if the address is already visible in the current disassembly
        int targetIndex = -1;
        for (uint32_t i = 0; i < m_disassemblyLines.Size(); i++)
        {
            auto impl = m_disassemblyLines.GetAt(i).as<implementation::DisasmLineViewModel>();
            if (impl->HasAddress() && impl->Address() == address)
            {
                targetIndex = static_cast<int>(i);
                break;
            }
        }

        if (targetIndex < 0)
        {
            // Address not in current view — navigate so the address appears
            SetDisasmFollowPC(false);
            m_fullProgramDisassembled = false;
            uint32_t backBytes = std::min(address, 80u);
            uint32_t startAddr = (address - backBytes) & 0xFFFFFFFE;
            m_disasmAddress = startAddr;
            UpdateDisassemblyAt(startAddr);

            for (uint32_t i = 0; i < m_disassemblyLines.Size(); i++)
            {
                auto impl = m_disassemblyLines.GetAt(i).as<implementation::DisasmLineViewModel>();
                if (impl->HasAddress() && impl->Address() == address)
                {
                    targetIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        if (targetIndex >= 0)
            m_scrollToLineRequested(*this, targetIndex);
    }

    void MainViewModel::NavigateToProgram()
    {
        m_disasmAddress = m_programStartAddress;
        SetDisasmFollowPC(false);
        m_fullProgramDisassembled = false;
        UpdateDisassemblyRange(m_programStartAddress, m_programEndAddress);
    }

    void MainViewModel::ResetDisasmFollowPC()
    {
        SetDisasmFollowPC(true);
        m_fullProgramDisassembled = false;
        UpdateDisassembly();
    }

    // ======================================================================
    // Disassembly
    // ======================================================================

    void MainViewModel::UpdateDisassembly()
    {
        if (m_manualDisasmMode)
        {
            // In manual mode, only update PC highlight; don't rebuild
            UpdatePCHighlight();
            return;
        }

        if (m_disasmFollowPC)
        {
            if (m_fullProgramDisassembled)
            {
                UpdatePCHighlight();
                return;
            }

            if (HasProgramLoaded())
            {
                UpdateDisassemblyRange(m_programStartAddress, m_programEndAddress);
                m_fullProgramDisassembled = true;
                return;
            }

            // No program loaded -- show 40 lines around PC
            uint32_t startAddr = m_cpu->PC;
            if (startAddr > m_programStartAddress)
            {
                uint32_t backBytes = std::min(startAddr - m_programStartAddress, 30u);
                startAddr -= backBytes;
                startAddr &= 0xFFFFFFFE;
            }
            UpdateDisassemblyAt(startAddr);
        }
        else
        {
            UpdatePCHighlight();
        }
    }

    void MainViewModel::UpdatePCHighlight()
    {
        int pcIndex = -1;
        for (uint32_t i = 0; i < m_disassemblyLines.Size(); i++)
        {
            auto line = m_disassemblyLines.GetAt(i);
            auto impl = line.as<implementation::DisasmLineViewModel>();
            bool isCurrent = impl->HasAddress() && impl->Address() == m_cpu->PC;
            impl->IsCurrentPC(isCurrent);
            if (impl->HasAddress())
            {
                auto it = m_breakpoints.find(impl->Address());
                bool hasBp = it != m_breakpoints.end();
                impl->HasBreakpoint(hasBp);
                impl->HasDisabledBreakpoint(hasBp && !it->second.enabled);
            }
            if (isCurrent) pcIndex = static_cast<int>(i);
        }
        if (pcIndex >= 0 && m_disasmFollowPC)
            m_scrollToLineRequested(*this, pcIndex);
    }

    void MainViewModel::UpdateDisassemblyAt(uint32_t startAddress)
    {
        m_disassemblyLines.Clear();

        if (m_showLst && m_hasLstLines)
        {
            int matchIdx = -1;
            for (int i = 0; i < static_cast<int>(m_lstLines.size()); i++)
            {
                if (m_lstLines[i].HasAddress && m_lstLines[i].Address == m_cpu->PC)
                {
                    matchIdx = i;
                    break;
                }
            }

            int startIdx = std::max(0, matchIdx - 5);
            int endIdx = std::min(static_cast<int>(m_lstLines.size()), startIdx + 40);

            for (int i = startIdx; i < endIdx; i++)
            {
                auto& lstLine = m_lstLines[i];
                auto vm = winrt::make<implementation::DisasmLineViewModel>();
                auto impl = vm.as<implementation::DisasmLineViewModel>();
                impl->Address(lstLine.Address);
                impl->HasAddress(lstLine.HasAddress);
                impl->Text(to_hstring_from_std(lstLine.RawText));
                impl->IsCurrentPC(lstLine.HasAddress && lstLine.Address == m_cpu->PC);
                {
                    auto bpIt = lstLine.HasAddress ? m_breakpoints.find(lstLine.Address) : m_breakpoints.end();
                    bool hasBp = bpIt != m_breakpoints.end();
                    impl->HasBreakpoint(hasBp);
                    impl->HasDisabledBreakpoint(hasBp && !bpIt->second.enabled);
                }
                m_disassemblyLines.Append(vm);
            }
        }
        else
        {
            auto lines = m_disassembler->Disassemble(startAddress, 40);
            for (auto& line : lines)
            {
                auto vm = winrt::make<implementation::DisasmLineViewModel>();
                auto impl = vm.as<implementation::DisasmLineViewModel>();
                impl->Address(line.Address);
                impl->HasAddress(true);
                impl->Text(to_hstring_from_std(line.ToString()));
                impl->IsCurrentPC(line.Address == m_cpu->PC);
                {
                    auto bpIt = m_breakpoints.find(line.Address);
                    bool hasBp = bpIt != m_breakpoints.end();
                    impl->HasBreakpoint(hasBp);
                    impl->HasDisabledBreakpoint(hasBp && !bpIt->second.enabled);
                }
                impl->RawBytes(to_hstring_from_std(line.RawBytes));
                impl->Mnemonic(to_hstring_from_std(line.Mnemonic));
                impl->Operands(to_hstring_from_std(line.Operands));
                impl->Length(line.Length);
                m_disassemblyLines.Append(vm);
            }
        }
    }

    void MainViewModel::UpdateDisassemblyRange(uint32_t startAddress, uint32_t endAddress)
    {
        m_disassemblyLines.Clear();

        auto lines = m_disassembler->DisassembleRange(startAddress, endAddress);
        for (auto& line : lines)
        {
            auto vm = winrt::make<implementation::DisasmLineViewModel>();
            auto impl = vm.as<implementation::DisasmLineViewModel>();
            impl->Address(line.Address);
            impl->HasAddress(true);
            impl->Text(to_hstring_from_std(line.ToString()));
            impl->IsCurrentPC(line.Address == m_cpu->PC);
            {
                auto bpIt = m_breakpoints.find(line.Address);
                bool hasBp = bpIt != m_breakpoints.end();
                impl->HasBreakpoint(hasBp);
                impl->HasDisabledBreakpoint(hasBp && !bpIt->second.enabled);
            }
            impl->RawBytes(to_hstring_from_std(line.RawBytes));
            impl->Mnemonic(to_hstring_from_std(line.Mnemonic));
            impl->Operands(to_hstring_from_std(line.Operands));
            impl->Length(line.Length);
            m_disassemblyLines.Append(vm);
        }
    }

    // ======================================================================
    // Memory dump
    // ======================================================================

    void MainViewModel::UpdateMemoryDump()
    {
        uint32_t addr = m_memoryDumpAddress & 0xFFFFFFF0; // Align to 16

        if (m_memoryDumpRows.Size() != 16)
        {
            m_memoryDumpRows.Clear();
            for (int row = 0; row < 16; row++)
            {
                uint32_t lineAddr = addr + static_cast<uint32_t>(row * 16);
                auto dumpRow = winrt::make<implementation::MemoryDumpRow>();
                dumpRow.as<implementation::MemoryDumpRow>()->Init(lineAddr, *m_memory, row);
                m_memoryDumpRows.Append(dumpRow);
            }
        }
        else
        {
            for (int row = 0; row < 16; row++)
            {
                uint32_t lineAddr = addr + static_cast<uint32_t>(row * 16);
                auto existing = m_memoryDumpRows.GetAt(row);
                existing.as<implementation::MemoryDumpRow>()->Update(lineAddr, *m_memory, row);
            }
        }
    }

    // ======================================================================
    // Memory edit mode
    // ======================================================================

    void MainViewModel::EnterMemoryEditMode()
    {
        m_isMemoryEditMode = true;
        RaisePropertyChanged(L"IsMemoryEditMode");
        RaiseAllCommandsCanExecuteChanged();
    }

    void MainViewModel::ApplyMemoryEdits()
    {
        for (uint32_t r = 0; r < m_memoryDumpRows.Size(); r++)
        {
            auto row = m_memoryDumpRows.GetAt(r);
            auto rowImpl = row.as<implementation::MemoryDumpRow>();
            auto cells = rowImpl->Cells();

            for (uint32_t c = 0; c < cells.Size(); c++)
            {
                auto cell = cells.GetAt(c);
                auto cellImpl = cell.as<implementation::MemoryByteCell>();
                if (cellImpl->IsModified())
                {
                    auto val = cellImpl->GetEditedValue();
                    if (val.has_value())
                        m_memory->PokeByte(cellImpl->Address(), val.value());
                }
            }
        }
        m_isMemoryEditMode = false;
        RaisePropertyChanged(L"IsMemoryEditMode");
        RaiseAllCommandsCanExecuteChanged();
        UpdateMemoryDump();
    }

    void MainViewModel::CancelMemoryEdits()
    {
        m_isMemoryEditMode = false;
        RaisePropertyChanged(L"IsMemoryEditMode");
        RaiseAllCommandsCanExecuteChanged();
        UpdateMemoryDump();
    }

    // ======================================================================
    // Register edit mode
    // ======================================================================

    void MainViewModel::EnterRegisterEditMode()
    {
        if (m_isRunning) return;

        // Save snapshot for Cancel
        for (int i = 0; i < 8; i++) m_savedD[i] = m_cpu->D[i];
        for (int i = 0; i < 8; i++) m_savedA[i] = m_cpu->A[i];
        m_savedPC = m_cpu->PC;
        m_savedSR = m_cpu->SR;
        m_savedSSP = m_cpu->SSP;
        m_savedVBR = m_cpu->VBR;
        m_savedCACR = m_cpu->CACR;
        for (int i = 0; i < 8; i++) m_savedFP[i] = m_cpu->GetFpu().FP[i];
        m_savedFPCR = m_cpu->GetFpu().FPCR;
        m_savedFPSR = m_cpu->GetFpu().FPSR;
        m_savedFPIAR = m_cpu->GetFpu().FPIAR;

        m_isRegisterEditMode = true;
        RaisePropertyChanged(L"IsRegisterEditMode");
        RaiseAllCommandsCanExecuteChanged();
    }

    void MainViewModel::ApplyRegisterEdits()
    {
        m_isRegisterEditMode = false;
        RaisePropertyChanged(L"IsRegisterEditMode");
        RaiseAllCommandsCanExecuteChanged();

        RefreshAll();
    }

    void MainViewModel::CancelRegisterEdits()
    {
        // Restore from snapshot
        for (int i = 0; i < 8; i++) m_cpu->D[i] = m_savedD[i];
        for (int i = 0; i < 8; i++) m_cpu->A[i] = m_savedA[i];
        m_cpu->PC = m_savedPC;
        m_cpu->SR = m_savedSR;
        m_cpu->SSP = m_savedSSP;
        m_cpu->VBR = m_savedVBR;
        m_cpu->CACR = m_savedCACR;
        for (int i = 0; i < 8; i++) m_cpu->GetFpu().FP[i] = m_savedFP[i];
        m_cpu->GetFpu().FPCR = m_savedFPCR;
        m_cpu->GetFpu().FPSR = m_savedFPSR;
        m_cpu->GetFpu().FPIAR = m_savedFPIAR;

        m_isRegisterEditMode = false;
        RaisePropertyChanged(L"IsRegisterEditMode");
        RaiseAllCommandsCanExecuteChanged();
        NotifyAllRegisters();
    }

    // ======================================================================
    // RefreshAll
    // ======================================================================

    void MainViewModel::RefreshAll()
    {
        m_disasmAddress = m_cpu->PC;
        UpdateDisassembly();
        UpdateMemoryDump();
        NotifyAllRegisters();
        RaisePropertyChanged(L"IsHalted");
        RaisePropertyChanged(L"IsStopped");
        RaisePropertyChanged(L"StopReason");
        RaisePropertyChanged(L"CycleCount");
        RaisePropertyChanged(L"EstimatedMHz");
    }

    // ======================================================================
    // Notification helpers
    // ======================================================================

    void MainViewModel::NotifyAllRegisters()
    {
        RaisePropertyChanged(L"D0"); RaisePropertyChanged(L"D1");
        RaisePropertyChanged(L"D2"); RaisePropertyChanged(L"D3");
        RaisePropertyChanged(L"D4"); RaisePropertyChanged(L"D5");
        RaisePropertyChanged(L"D6"); RaisePropertyChanged(L"D7");
        RaisePropertyChanged(L"A0"); RaisePropertyChanged(L"A1");
        RaisePropertyChanged(L"A2"); RaisePropertyChanged(L"A3");
        RaisePropertyChanged(L"A4"); RaisePropertyChanged(L"A5");
        RaisePropertyChanged(L"A6"); RaisePropertyChanged(L"A7");
        RaisePropertyChanged(L"PC"); RaisePropertyChanged(L"SR");
        RaisePropertyChanged(L"SSP"); RaisePropertyChanged(L"VBR");
        RaisePropertyChanged(L"CACR");
        RaisePropertyChanged(L"FP0"); RaisePropertyChanged(L"FP1");
        RaisePropertyChanged(L"FP2"); RaisePropertyChanged(L"FP3");
        RaisePropertyChanged(L"FP4"); RaisePropertyChanged(L"FP5");
        RaisePropertyChanged(L"FP6"); RaisePropertyChanged(L"FP7");
        RaisePropertyChanged(L"FPCR"); RaisePropertyChanged(L"FPSR");
        RaisePropertyChanged(L"FPIAR");
        NotifyFlagChanges();
    }

    void MainViewModel::NotifyFlagChanges()
    {
        RaisePropertyChanged(L"FlagX"); RaisePropertyChanged(L"FlagN");
        RaisePropertyChanged(L"FlagZ"); RaisePropertyChanged(L"FlagV");
        RaisePropertyChanged(L"FlagC"); RaisePropertyChanged(L"FlagS");
        RaisePropertyChanged(L"FlagT"); RaisePropertyChanged(L"InterruptMask");
    }

    void MainViewModel::RaiseAllCommandsCanExecuteChanged()
    {
        auto raise = [](Em68030::RelayCommand const& cmd) {
            if (cmd)
                cmd.as<implementation::RelayCommand>()->RaiseCanExecuteChanged();
        };
        raise(m_stepCommand);
        raise(m_runCommand);
        raise(m_stopCommand);
        raise(m_resetCommand);
        raise(m_editMemoryCommand);
        raise(m_applyMemoryCommand);
        raise(m_cancelMemoryCommand);
        raise(m_editRegistersCommand);
        raise(m_applyRegistersCommand);
        raise(m_cancelRegistersCommand);
    }

    // ======================================================================
    // INotifyPropertyChanged
    // ======================================================================

    winrt::event_token MainViewModel::PropertyChanged(
        PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void MainViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void MainViewModel::RaisePropertyChanged(hstring const& propertyName)
    {
        m_propertyChanged(*this, PropertyChangedEventArgs(propertyName));
    }

    // ======================================================================
    // Data register properties D0-D7
    // ======================================================================

    uint32_t MainViewModel::D0() const { return m_cpu->D[0]; }
    void MainViewModel::D0(uint32_t value) { m_cpu->D[0] = value; RaisePropertyChanged(L"D0"); }

    uint32_t MainViewModel::D1() const { return m_cpu->D[1]; }
    void MainViewModel::D1(uint32_t value) { m_cpu->D[1] = value; RaisePropertyChanged(L"D1"); }

    uint32_t MainViewModel::D2() const { return m_cpu->D[2]; }
    void MainViewModel::D2(uint32_t value) { m_cpu->D[2] = value; RaisePropertyChanged(L"D2"); }

    uint32_t MainViewModel::D3() const { return m_cpu->D[3]; }
    void MainViewModel::D3(uint32_t value) { m_cpu->D[3] = value; RaisePropertyChanged(L"D3"); }

    uint32_t MainViewModel::D4() const { return m_cpu->D[4]; }
    void MainViewModel::D4(uint32_t value) { m_cpu->D[4] = value; RaisePropertyChanged(L"D4"); }

    uint32_t MainViewModel::D5() const { return m_cpu->D[5]; }
    void MainViewModel::D5(uint32_t value) { m_cpu->D[5] = value; RaisePropertyChanged(L"D5"); }

    uint32_t MainViewModel::D6() const { return m_cpu->D[6]; }
    void MainViewModel::D6(uint32_t value) { m_cpu->D[6] = value; RaisePropertyChanged(L"D6"); }

    uint32_t MainViewModel::D7() const { return m_cpu->D[7]; }
    void MainViewModel::D7(uint32_t value) { m_cpu->D[7] = value; RaisePropertyChanged(L"D7"); }

    // ======================================================================
    // Address register properties A0-A7
    // ======================================================================

    uint32_t MainViewModel::A0() const { return m_cpu->A[0]; }
    void MainViewModel::A0(uint32_t value) { m_cpu->A[0] = value; RaisePropertyChanged(L"A0"); }

    uint32_t MainViewModel::A1() const { return m_cpu->A[1]; }
    void MainViewModel::A1(uint32_t value) { m_cpu->A[1] = value; RaisePropertyChanged(L"A1"); }

    uint32_t MainViewModel::A2() const { return m_cpu->A[2]; }
    void MainViewModel::A2(uint32_t value) { m_cpu->A[2] = value; RaisePropertyChanged(L"A2"); }

    uint32_t MainViewModel::A3() const { return m_cpu->A[3]; }
    void MainViewModel::A3(uint32_t value) { m_cpu->A[3] = value; RaisePropertyChanged(L"A3"); }

    uint32_t MainViewModel::A4() const { return m_cpu->A[4]; }
    void MainViewModel::A4(uint32_t value) { m_cpu->A[4] = value; RaisePropertyChanged(L"A4"); }

    uint32_t MainViewModel::A5() const { return m_cpu->A[5]; }
    void MainViewModel::A5(uint32_t value) { m_cpu->A[5] = value; RaisePropertyChanged(L"A5"); }

    uint32_t MainViewModel::A6() const { return m_cpu->A[6]; }
    void MainViewModel::A6(uint32_t value) { m_cpu->A[6] = value; RaisePropertyChanged(L"A6"); }

    uint32_t MainViewModel::A7() const { return m_cpu->A[7]; }
    void MainViewModel::A7(uint32_t value) { m_cpu->A[7] = value; RaisePropertyChanged(L"A7"); }

    // ======================================================================
    // Special registers
    // ======================================================================

    uint32_t MainViewModel::PC() const { return m_cpu->PC; }
    void MainViewModel::PC(uint32_t value) {
        m_cpu->PC = value;
        RaisePropertyChanged(L"PC");
        UpdateDisassembly();
    }

    uint32_t MainViewModel::SR() const { return m_cpu->SR; }
    void MainViewModel::SR(uint32_t value) {
        m_cpu->SR = static_cast<uint16_t>(value);
        RaisePropertyChanged(L"SR");
        NotifyFlagChanges();
    }

    uint32_t MainViewModel::SSP() const { return m_cpu->SSP; }
    void MainViewModel::SSP(uint32_t value) { m_cpu->SSP = value; RaisePropertyChanged(L"SSP"); }

    uint32_t MainViewModel::VBR() const { return m_cpu->VBR; }
    void MainViewModel::VBR(uint32_t value) { m_cpu->VBR = value; RaisePropertyChanged(L"VBR"); }

    uint32_t MainViewModel::CACR() const { return m_cpu->CACR; }
    void MainViewModel::CACR(uint32_t value) { m_cpu->CACR = value; RaisePropertyChanged(L"CACR"); }

    // ======================================================================
    // FPU registers
    // ======================================================================

    double MainViewModel::FP0() const { return m_cpu->GetFpu().FP[0]; }
    void MainViewModel::FP0(double value) { m_cpu->GetFpu().FP[0] = value; RaisePropertyChanged(L"FP0"); }

    double MainViewModel::FP1() const { return m_cpu->GetFpu().FP[1]; }
    void MainViewModel::FP1(double value) { m_cpu->GetFpu().FP[1] = value; RaisePropertyChanged(L"FP1"); }

    double MainViewModel::FP2() const { return m_cpu->GetFpu().FP[2]; }
    void MainViewModel::FP2(double value) { m_cpu->GetFpu().FP[2] = value; RaisePropertyChanged(L"FP2"); }

    double MainViewModel::FP3() const { return m_cpu->GetFpu().FP[3]; }
    void MainViewModel::FP3(double value) { m_cpu->GetFpu().FP[3] = value; RaisePropertyChanged(L"FP3"); }

    double MainViewModel::FP4() const { return m_cpu->GetFpu().FP[4]; }
    void MainViewModel::FP4(double value) { m_cpu->GetFpu().FP[4] = value; RaisePropertyChanged(L"FP4"); }

    double MainViewModel::FP5() const { return m_cpu->GetFpu().FP[5]; }
    void MainViewModel::FP5(double value) { m_cpu->GetFpu().FP[5] = value; RaisePropertyChanged(L"FP5"); }

    double MainViewModel::FP6() const { return m_cpu->GetFpu().FP[6]; }
    void MainViewModel::FP6(double value) { m_cpu->GetFpu().FP[6] = value; RaisePropertyChanged(L"FP6"); }

    double MainViewModel::FP7() const { return m_cpu->GetFpu().FP[7]; }
    void MainViewModel::FP7(double value) { m_cpu->GetFpu().FP[7] = value; RaisePropertyChanged(L"FP7"); }

    uint32_t MainViewModel::FPCR() const { return m_cpu->GetFpu().FPCR; }
    void MainViewModel::FPCR(uint32_t value) { m_cpu->GetFpu().FPCR = value; RaisePropertyChanged(L"FPCR"); }

    uint32_t MainViewModel::FPSR() const { return m_cpu->GetFpu().FPSR; }
    void MainViewModel::FPSR(uint32_t value) { m_cpu->GetFpu().FPSR = value; RaisePropertyChanged(L"FPSR"); }

    uint32_t MainViewModel::FPIAR() const { return m_cpu->GetFpu().FPIAR; }
    void MainViewModel::FPIAR(uint32_t value) { m_cpu->GetFpu().FPIAR = value; RaisePropertyChanged(L"FPIAR"); }

    // ======================================================================
    // Flags
    // ======================================================================

    bool MainViewModel::FlagX() const { return m_cpu->GetFlagX(); }
    void MainViewModel::FlagX(bool value) {
        m_cpu->SetFlagX(value);
        RaisePropertyChanged(L"FlagX");
        RaisePropertyChanged(L"SR");
    }

    bool MainViewModel::FlagN() const { return m_cpu->GetFlagN(); }
    void MainViewModel::FlagN(bool value) {
        m_cpu->SetFlagN(value);
        RaisePropertyChanged(L"FlagN");
        RaisePropertyChanged(L"SR");
    }

    bool MainViewModel::FlagZ() const { return m_cpu->GetFlagZ(); }
    void MainViewModel::FlagZ(bool value) {
        m_cpu->SetFlagZ(value);
        RaisePropertyChanged(L"FlagZ");
        RaisePropertyChanged(L"SR");
    }

    bool MainViewModel::FlagV() const { return m_cpu->GetFlagV(); }
    void MainViewModel::FlagV(bool value) {
        m_cpu->SetFlagV(value);
        RaisePropertyChanged(L"FlagV");
        RaisePropertyChanged(L"SR");
    }

    bool MainViewModel::FlagC() const { return m_cpu->GetFlagC(); }
    void MainViewModel::FlagC(bool value) {
        m_cpu->SetFlagC(value);
        RaisePropertyChanged(L"FlagC");
        RaisePropertyChanged(L"SR");
    }

    bool MainViewModel::FlagS() const { return m_cpu->GetSupervisorMode(); }
    void MainViewModel::FlagS(bool value) {
        m_cpu->SetSupervisorMode(value);
        RaisePropertyChanged(L"FlagS");
        RaisePropertyChanged(L"SR");
    }

    bool MainViewModel::FlagT() const { return m_cpu->GetTraceT1(); }
    void MainViewModel::FlagT(bool value) {
        m_cpu->SetTraceT1(value);
        RaisePropertyChanged(L"FlagT");
        RaisePropertyChanged(L"SR");
    }

    int32_t MainViewModel::InterruptMask() const { return m_cpu->GetInterruptMask(); }
    void MainViewModel::InterruptMask(int32_t value) {
        m_cpu->SetInterruptMask(value);
        RaisePropertyChanged(L"InterruptMask");
        RaisePropertyChanged(L"SR");
    }

    // ======================================================================
    // State properties
    // ======================================================================

    bool MainViewModel::IsRunning() const { return m_isRunning; }
    void MainViewModel::IsRunning(bool value) {
        m_isRunning = value;
        RaisePropertyChanged(L"IsRunning");
    }

    bool MainViewModel::IsMemoryEditMode() const { return m_isMemoryEditMode; }
    void MainViewModel::IsMemoryEditMode(bool value) {
        m_isMemoryEditMode = value;
        RaisePropertyChanged(L"IsMemoryEditMode");
    }

    bool MainViewModel::IsRegisterEditMode() const { return m_isRegisterEditMode; }
    void MainViewModel::IsRegisterEditMode(bool value) {
        m_isRegisterEditMode = value;
        RaisePropertyChanged(L"IsRegisterEditMode");
    }

    bool MainViewModel::IsHalted() const { return m_cpu->Halted; }
    bool MainViewModel::IsStopped() const { return m_cpu->Stopped; }

    hstring MainViewModel::StopReason() const
    {
        return to_hstring_from_std(m_cpu->StopReason);
    }

    int64_t MainViewModel::CycleCount() const { return m_cpu->CycleCount; }

    hstring MainViewModel::EstimatedMHz() const
    {
        double mhz = m_showAvgMhz ? m_avgMHz : m_estimatedMHz;
        double mips = m_showAvgMhz ? m_avgMips : m_estimatedMips;
        if (mhz > 0)
        {
            wchar_t buf[80];
            swprintf_s(buf, m_showAvgMhz ? L"Avg %.2f MHz (%.2f MIPS)" : L"%.2f MHz (%.2f MIPS)", mhz, mips);
            return buf;
        }
        return L"";
    }

    void MainViewModel::ToggleMhzDisplayMode()
    {
        m_showAvgMhz = !m_showAvgMhz;
        RaisePropertyChanged(L"EstimatedMHz");
    }

    // ======================================================================
    // File / display properties
    // ======================================================================

    bool MainViewModel::ShowLst() const { return m_showLst; }
    void MainViewModel::ShowLst(bool value) {
        m_showLst = value;
        RaisePropertyChanged(L"ShowLst");
        UpdateDisassembly();
    }

    bool MainViewModel::HasLstFile() const { return m_hasLstLines; }
    bool MainViewModel::HasProgramLoaded() const { return m_programEndAddress > m_programStartAddress; }

    hstring MainViewModel::LoadedFileName() const { return m_loadedFileName; }
    void MainViewModel::LoadedFileName(hstring const& value) {
        m_loadedFileName = value;
        RaisePropertyChanged(L"LoadedFileName");
    }

    uint32_t MainViewModel::MemoryDumpAddress() const { return m_memoryDumpAddress; }
    void MainViewModel::MemoryDumpAddress(uint32_t value) {
        m_memoryDumpAddress = value;
        RaisePropertyChanged(L"MemoryDumpAddress");
        UpdateMemoryDump();
    }

    // ======================================================================
    // Observable collections
    // ======================================================================

    IObservableVector<Em68030::DisasmLineViewModel> MainViewModel::DisassemblyLines() const
    {
        return m_disassemblyLines;
    }

    IObservableVector<Em68030::MemoryDumpRow> MainViewModel::MemoryDumpRows() const
    {
        return m_memoryDumpRows;
    }

} // namespace winrt::Em68030::implementation
