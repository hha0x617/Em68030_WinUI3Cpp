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
#include "TapNetworkHandler.h"

#include <winioctl.h>
#include <format>

// TAP-Windows IOCTL to set media status (link up/down)
#define TAP_WIN_IOCTL_SET_MEDIA_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Network adapter class GUID in the registry
static const char* const ADAPTER_CLASS_KEY =
    "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";
static const char* const NETWORK_CONNECTIONS_KEY =
    "SYSTEM\\CurrentControlSet\\Control\\Network\\{4d36e972-e325-11ce-bfc1-08002be10318}";

// TAP-Windows component IDs
static const char* const TAP_COMPONENT_ID = "tap0901";
static const char* const TAP_COMPONENT_ID_ALT = "root\\tap0901";

namespace Em68030::IO {

TapNetworkHandler::TapNetworkHandler(const std::string& adapterGuid)
{
    if (adapterGuid.empty()) return;

    // Open the TAP device
    std::string devicePath = "\\\\.\\Global\\" + adapterGuid + ".tap";
    m_tapHandle = CreateFileA(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_tapHandle == INVALID_HANDLE_VALUE)
    {
        if (DiagnosticOutput)
            DiagnosticOutput(std::format("[TAP] Failed to open device: {} (error {})\n",
                devicePath, GetLastError()));
        return;
    }

    // Create events for overlapped I/O
    m_readEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    m_writeEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    // Set link up
    SetMediaStatus(true);

    // Start background read thread
    m_readThread = std::thread(&TapNetworkHandler::ReadLoop, this);
}

TapNetworkHandler::~TapNetworkHandler()
{
    m_disposed = true;

    // Signal the read event to unblock the read thread
    if (m_readEvent)
        SetEvent(m_readEvent);

    if (m_readThread.joinable())
        m_readThread.join();

    if (m_tapHandle != INVALID_HANDLE_VALUE)
    {
        SetMediaStatus(false);
        CloseHandle(m_tapHandle);
    }
    if (m_readEvent) CloseHandle(m_readEvent);
    if (m_writeEvent) CloseHandle(m_writeEvent);
}

void TapNetworkHandler::SetMediaStatus(bool up)
{
    if (m_tapHandle == INVALID_HANDLE_VALUE) return;

    ULONG status = up ? 1 : 0;
    DWORD bytesReturned = 0;
    DeviceIoControl(m_tapHandle, TAP_WIN_IOCTL_SET_MEDIA_STATUS,
                    &status, sizeof(status), &status, sizeof(status),
                    &bytesReturned, nullptr);
}

void TapNetworkHandler::ReadLoop()
{
    std::vector<uint8_t> buffer(2048);

    while (!m_disposed)
    {
        OVERLAPPED ov{};
        ov.hEvent = m_readEvent;
        ResetEvent(m_readEvent);

        DWORD bytesRead = 0;
        BOOL result = ReadFile(m_tapHandle, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &bytesRead, &ov);

        if (!result)
        {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                // Wait for completion or disposal
                DWORD waitResult = WaitForSingleObject(m_readEvent, 500);
                if (m_disposed) break;
                if (waitResult != WAIT_OBJECT_0) continue;

                if (!GetOverlappedResult(m_tapHandle, &ov, &bytesRead, FALSE))
                    continue;
            }
            else
            {
                if (m_disposed) break;
                // Transient error, retry after short wait
                Sleep(10);
                continue;
            }
        }

        if (bytesRead > 0 && bytesRead >= 14) // minimum Ethernet frame
        {
            std::lock_guard lock(m_rxMutex);
            m_rxQueue.emplace(buffer.begin(), buffer.begin() + bytesRead);
        }
    }
}

void TapNetworkHandler::ProcessPacket(const uint8_t* frame, int length)
{
    if (m_tapHandle == INVALID_HANDLE_VALUE || length <= 0) return;

    OVERLAPPED ov{};
    ov.hEvent = m_writeEvent;
    ResetEvent(m_writeEvent);

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(m_tapHandle, frame, static_cast<DWORD>(length),
                            &bytesWritten, &ov);
    if (!result && GetLastError() == ERROR_IO_PENDING)
    {
        WaitForSingleObject(m_writeEvent, 1000);
        GetOverlappedResult(m_tapHandle, &ov, &bytesWritten, FALSE);
    }
}

bool TapNetworkHandler::HasPendingPacket() const
{
    std::lock_guard lock(m_rxMutex);
    return !m_rxQueue.empty();
}

std::vector<uint8_t> TapNetworkHandler::DequeuePacket()
{
    std::lock_guard lock(m_rxMutex);
    if (m_rxQueue.empty()) return {};
    auto pkt = std::move(m_rxQueue.front());
    m_rxQueue.pop();
    return pkt;
}

void TapNetworkHandler::SetGuestMac(const std::array<uint8_t, 6>& mac)
{
    m_guestMac = mac;
}

void TapNetworkHandler::Reset()
{
    std::lock_guard lock(m_rxMutex);
    std::queue<std::vector<uint8_t>>().swap(m_rxQueue);
}

// ========================================================================
// Static: Enumerate TAP-Windows adapters
// ========================================================================

std::vector<TapAdapterInfo> TapNetworkHandler::EnumerateAdapters()
{
    std::vector<TapAdapterInfo> result;

    HKEY classKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ADAPTER_CLASS_KEY, 0, KEY_READ, &classKey) != ERROR_SUCCESS)
        return result;

    for (DWORD i = 0; ; i++)
    {
        char subKeyName[256];
        DWORD subKeyLen = sizeof(subKeyName);
        if (RegEnumKeyExA(classKey, i, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        HKEY adapterKey;
        if (RegOpenKeyExA(classKey, subKeyName, 0, KEY_READ, &adapterKey) != ERROR_SUCCESS)
            continue;

        // Read ComponentId
        char componentId[256] = {};
        DWORD componentIdLen = sizeof(componentId);
        DWORD type = 0;
        bool isTap = false;
        if (RegQueryValueExA(adapterKey, "ComponentId", nullptr, &type,
                             reinterpret_cast<BYTE*>(componentId), &componentIdLen) == ERROR_SUCCESS)
        {
            std::string cid(componentId);
            if (cid == TAP_COMPONENT_ID || cid == TAP_COMPONENT_ID_ALT)
                isTap = true;
        }

        if (!isTap)
        {
            RegCloseKey(adapterKey);
            continue;
        }

        TapAdapterInfo info;

        // Read NetCfgInstanceId (GUID)
        char guid[256] = {};
        DWORD guidLen = sizeof(guid);
        if (RegQueryValueExA(adapterKey, "NetCfgInstanceId", nullptr, &type,
                             reinterpret_cast<BYTE*>(guid), &guidLen) == ERROR_SUCCESS)
        {
            info.Guid = guid;
        }

        // Read DriverDesc
        char desc[256] = {};
        DWORD descLen = sizeof(desc);
        if (RegQueryValueExA(adapterKey, "DriverDesc", nullptr, &type,
                             reinterpret_cast<BYTE*>(desc), &descLen) == ERROR_SUCCESS)
        {
            info.Description = desc;
        }

        RegCloseKey(adapterKey);

        // Read friendly name from Network Connections
        if (!info.Guid.empty())
        {
            std::string connPath = std::string(NETWORK_CONNECTIONS_KEY) +
                                   "\\" + info.Guid + "\\Connection";
            HKEY connKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, connPath.c_str(), 0, KEY_READ, &connKey) == ERROR_SUCCESS)
            {
                char name[256] = {};
                DWORD nameLen = sizeof(name);
                if (RegQueryValueExA(connKey, "Name", nullptr, &type,
                                     reinterpret_cast<BYTE*>(name), &nameLen) == ERROR_SUCCESS)
                {
                    info.Name = name;
                }
                RegCloseKey(connKey);
            }
        }

        if (!info.Guid.empty())
            result.push_back(std::move(info));
    }

    RegCloseKey(classKey);
    return result;
}

} // namespace Em68030::IO
