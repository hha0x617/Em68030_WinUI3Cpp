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

#include "FileLoader.h"
#include "../Core/Memory.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <limits>

namespace Em68030::IO {

uint32_t FileLoader::LoadBinary(Core::Memory& memory, const std::string& filePath, uint32_t loadAddress)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + filePath);

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    memory.LoadData(loadAddress, data);
    return static_cast<uint32_t>(data.size());
}

SRecordLoadResult FileLoader::LoadSRecord(Core::Memory& memory, const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + filePath);

    uint32_t minAddr = std::numeric_limits<uint32_t>::max();
    uint32_t maxAddr = 0;
    uint32_t entryPoint = 0;
    bool hasEntryPoint = false;

    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        if (line.empty() || line[0] != 'S')
            continue;

        char type = line[1];

        // Parse byte count from characters 2-3
        int byteCount = 0;
        {
            std::string hex = line.substr(2, 2);
            byteCount = static_cast<int>(std::stoul(hex, nullptr, 16));
        }

        // Parse record data bytes
        std::vector<uint8_t> recordData(byteCount);
        for (int i = 0; i < byteCount; i++) {
            std::string hex = line.substr(4 + i * 2, 2);
            recordData[i] = static_cast<uint8_t>(std::stoul(hex, nullptr, 16));
        }

        uint32_t address;
        int dataOffset;

        switch (type) {
            case '0': // Header
                continue;

            case '1': // Data with 16-bit address
                address = static_cast<uint32_t>((recordData[0] << 8) | recordData[1]);
                dataOffset = 2;
                break;

            case '2': // Data with 24-bit address
                address = static_cast<uint32_t>((recordData[0] << 16) | (recordData[1] << 8) | recordData[2]);
                dataOffset = 3;
                break;

            case '3': // Data with 32-bit address
                address = static_cast<uint32_t>((recordData[0] << 24) | (recordData[1] << 16) |
                                                 (recordData[2] << 8) | recordData[3]);
                dataOffset = 4;
                break;

            case '7': // End record with 32-bit start address
                entryPoint = static_cast<uint32_t>((recordData[0] << 24) | (recordData[1] << 16) |
                                                    (recordData[2] << 8) | recordData[3]);
                hasEntryPoint = true;
                continue;

            case '8': // End record with 24-bit start address
                entryPoint = static_cast<uint32_t>((recordData[0] << 16) | (recordData[1] << 8) | recordData[2]);
                hasEntryPoint = true;
                continue;

            case '9': // End record with 16-bit start address
                entryPoint = static_cast<uint32_t>((recordData[0] << 8) | recordData[1]);
                hasEntryPoint = true;
                continue;

            case '5': // Record count
            case '6':
                continue;

            default:
                continue;
        }

        int dataLength = byteCount - dataOffset - 1; // -1 for checksum
        for (int i = 0; i < dataLength; i++) {
            memory.PokeByte(address + static_cast<uint32_t>(i), recordData[dataOffset + i]);
        }

        if (address < minAddr) minAddr = address;
        uint32_t end = address + static_cast<uint32_t>(dataLength);
        if (end > maxAddr) maxAddr = end;
    }

    if (minAddr == std::numeric_limits<uint32_t>::max()) minAddr = 0;

    return SRecordLoadResult{ minAddr, maxAddr, entryPoint, hasEntryPoint };
}

ElfLoadResult FileLoader::LoadElf(Core::Memory& memory, const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + filePath);

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    // Validate ELF magic: 0x7F 'E' 'L' 'F'
    if (data.size() < 52 ||
        data[0] != 0x7F || data[1] != 'E' ||
        data[2] != 'L' || data[3] != 'F')
        throw std::runtime_error("Not a valid ELF file.");

    // EI_CLASS: must be ELFCLASS32 (1)
    if (data[4] != 1)
        throw std::runtime_error("Only 32-bit ELF files are supported.");

    // EI_DATA: must be ELFDATA2MSB (2) for big-endian
    uint8_t eiData = data[5];
    bool bigEndian = eiData == 2;
    if (!bigEndian && eiData != 1)
        throw std::runtime_error("Unknown ELF data encoding.");

    // Parse ELF header
    uint16_t e_type = ReadU16(data.data(), 16, bigEndian);
    uint16_t e_machine = ReadU16(data.data(), 18, bigEndian);
    uint32_t e_entry = ReadU32(data.data(), 24, bigEndian);
    uint32_t e_phoff = ReadU32(data.data(), 28, bigEndian);
    uint16_t e_phentsize = ReadU16(data.data(), 42, bigEndian);
    uint16_t e_phnum = ReadU16(data.data(), 44, bigEndian);

    // Validate: ET_EXEC(2) or ET_DYN(3)
    if (e_type != 2 && e_type != 3)
        throw std::runtime_error("ELF type " + std::to_string(e_type) + " is not executable.");

    // Load PT_LOAD segments
    uint32_t minAddr = std::numeric_limits<uint32_t>::max();
    uint32_t maxAddr = 0;
    int segmentsLoaded = 0;

    for (int i = 0; i < e_phnum; i++) {
        uint32_t phOffset = e_phoff + static_cast<uint32_t>(i * e_phentsize);
        if (phOffset + e_phentsize > data.size())
            break;

        uint32_t p_type = ReadU32(data.data(), phOffset, bigEndian);
        if (p_type != 1) continue; // PT_LOAD = 1

        uint32_t p_offset = ReadU32(data.data(), phOffset + 4, bigEndian);
        uint32_t p_vaddr = ReadU32(data.data(), phOffset + 8, bigEndian);
        uint32_t p_filesz = ReadU32(data.data(), phOffset + 16, bigEndian);
        uint32_t p_memsz = ReadU32(data.data(), phOffset + 20, bigEndian);

        // Load file data into memory
        if (p_filesz > 0 && p_offset + p_filesz <= data.size()) {
            for (uint32_t j = 0; j < p_filesz; j++)
                memory.PokeByte(p_vaddr + j, data[p_offset + j]);
        }

        // Zero-fill BSS (memsz > filesz)
        for (uint32_t j = p_filesz; j < p_memsz; j++)
            memory.PokeByte(p_vaddr + j, 0);

        if (p_vaddr < minAddr) minAddr = p_vaddr;
        uint32_t end = p_vaddr + p_memsz;
        if (end > maxAddr) maxAddr = end;
        segmentsLoaded++;
    }

    if (segmentsLoaded == 0)
        throw std::runtime_error("No loadable segments found in ELF file.");

    if (minAddr == std::numeric_limits<uint32_t>::max()) minAddr = 0;

    return ElfLoadResult{ e_entry, minAddr, maxAddr, e_machine, segmentsLoaded };
}

bool FileLoader::IsElfFile(const std::string& filePath)
{
    try {
        std::ifstream fs(filePath, std::ios::binary);
        if (!fs.is_open()) return false;
        uint8_t magic[4];
        fs.read(reinterpret_cast<char*>(magic), 4);
        if (fs.gcount() < 4) return false;
        return magic[0] == 0x7F && magic[1] == 'E' &&
               magic[2] == 'L' && magic[3] == 'F';
    } catch (...) {
        return false;
    }
}

std::string FileLoader::FindLstFile(const std::string& filePath)
{
    namespace fs = std::filesystem;
    fs::path p(filePath);
    fs::path dir = p.parent_path();
    std::string baseName = p.stem().string();

    fs::path lstPath = dir / (baseName + ".lst");
    if (fs::exists(lstPath)) return lstPath.string();

    lstPath = dir / (baseName + ".LST");
    if (fs::exists(lstPath)) return lstPath.string();

    return ""; // empty string = not found
}

std::vector<LstLine> FileLoader::LoadLstFile(const std::string& lstPath)
{
    std::vector<LstLine> lines;
    std::ifstream file(lstPath);
    if (!file.is_open()) return lines;

    std::string rawLine;
    while (std::getline(file, rawLine)) {
        lines.push_back(ParseLstLine(rawLine));
    }
    return lines;
}

LstLine FileLoader::ParseLstLine(const std::string& line)
{
    LstLine result;
    result.RawText = line;

    if (line.length() < 8) return result;

    // Try to parse address from first column
    std::string addrPart = line.substr(0, std::min(static_cast<size_t>(8), line.length()));
    // Trim whitespace
    size_t start = addrPart.find_first_not_of(' ');
    if (start != std::string::npos)
        addrPart = addrPart.substr(start);
    size_t end = addrPart.find_last_not_of(' ');
    if (end != std::string::npos)
        addrPart = addrPart.substr(0, end + 1);

    if (!addrPart.empty()) {
        try {
            size_t pos;
            uint32_t addr = static_cast<uint32_t>(std::stoul(addrPart, &pos, 16));
            if (pos == addrPart.size()) {
                result.Address = addr;
                result.HasAddress = true;
            }
        } catch (...) {
            // Not a valid hex number
        }
    }

    return result;
}

uint16_t FileLoader::ReadU16(const uint8_t* data, uint32_t offset, bool bigEndian)
{
    if (bigEndian)
        return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
    return static_cast<uint16_t>((data[offset + 1] << 8) | data[offset]);
}

uint32_t FileLoader::ReadU32(const uint8_t* data, uint32_t offset, bool bigEndian)
{
    if (bigEndian)
        return static_cast<uint32_t>((data[offset] << 24) | (data[offset + 1] << 16) |
                                      (data[offset + 2] << 8) | data[offset + 3]);
    return static_cast<uint32_t>((data[offset + 3] << 24) | (data[offset + 2] << 16) |
                                  (data[offset + 1] << 8) | data[offset]);
}

} // namespace Em68030::IO
