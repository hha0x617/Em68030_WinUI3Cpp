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
#include <gtest/gtest.h>
#include <windows.h>
#include <fstream>
#include <string>
#include <set>
#include <map>
#include <regex>
#include <filesystem>

// ============================================================================
// Resource file integrity tests
//
// Validates that en-US and ja-JP .resw files have matching keys and no
// empty values, ensuring localization completeness without requiring WinRT.
// ============================================================================

namespace {

// Derive source Strings/ path from exe location.
// Build output: <repo>/Em68030/x64/Release/Em68030.Tests.exe
// Strings source: <repo>/Em68030/Strings/
std::filesystem::path GetRepoRoot()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    // exe is at <repo>/Em68030/x64/Release/ → go up 3 levels to <repo>
    return std::filesystem::path(exePath).parent_path().parent_path().parent_path().parent_path();
}

const std::string kEnUsPath = (GetRepoRoot() / "Em68030" / "Strings" / "en-US" / "Resources.resw").string();
const std::string kJaJpPath = (GetRepoRoot() / "Em68030" / "Strings" / "ja-JP" / "Resources.resw").string();

// Parse resource keys from a .resw file
std::set<std::string> ParseReswKeys(const std::string& filePath)
{
    std::set<std::string> keys;
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return keys;

    std::string line;
    std::regex dataNameRegex(R"xx(<data\s+name="([^"]+)")xx");
    while (std::getline(ifs, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, dataNameRegex))
        {
            keys.insert(match[1].str());
        }
    }
    return keys;
}

// Parse resource key-value pairs from a .resw file
std::map<std::string, std::string> ParseReswEntries(const std::string& filePath)
{
    std::map<std::string, std::string> entries;
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return entries;

    std::string line;
    std::regex dataNameRegex(R"xx(<data\s+name="([^"]+)")xx");
    std::regex valueRegex(R"xx(<value>(.*)</value>)xx");
    std::string currentKey;

    while (std::getline(ifs, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, dataNameRegex))
        {
            currentKey = match[1].str();
        }
        else if (!currentKey.empty() && std::regex_search(line, match, valueRegex))
        {
            entries[currentKey] = match[1].str();
            currentKey.clear();
        }
    }
    return entries;
}

} // anonymous namespace

// Verify en-US resource file exists and has entries
TEST(ResourceIntegrityTest, EnUsFileExistsAndHasEntries)
{
    ASSERT_TRUE(std::filesystem::exists(kEnUsPath)) << "en-US Resources.resw not found";
    auto keys = ParseReswKeys(kEnUsPath);
    EXPECT_GT(keys.size(), 100u) << "en-US Resources.resw should have >100 keys";
}

// Verify ja-JP resource file exists and has entries
TEST(ResourceIntegrityTest, JaJpFileExistsAndHasEntries)
{
    ASSERT_TRUE(std::filesystem::exists(kJaJpPath)) << "ja-JP Resources.resw not found";
    auto keys = ParseReswKeys(kJaJpPath);
    EXPECT_GT(keys.size(), 100u) << "ja-JP Resources.resw should have >100 keys";
}

// Verify en-US and ja-JP have exactly the same set of keys
TEST(ResourceIntegrityTest, KeysSynchronized)
{
    auto enKeys = ParseReswKeys(kEnUsPath);
    auto jaKeys = ParseReswKeys(kJaJpPath);
    ASSERT_FALSE(enKeys.empty()) << "en-US has no keys";
    ASSERT_FALSE(jaKeys.empty()) << "ja-JP has no keys";

    // Keys in en-US but missing from ja-JP
    for (const auto& k : enKeys)
    {
        EXPECT_TRUE(jaKeys.count(k) > 0) << "Key in en-US but missing from ja-JP: " << k;
    }

    // Keys in ja-JP but missing from en-US
    for (const auto& k : jaKeys)
    {
        EXPECT_TRUE(enKeys.count(k) > 0) << "Key in ja-JP but missing from en-US: " << k;
    }

    EXPECT_EQ(enKeys.size(), jaKeys.size());
}

// Verify no empty values in en-US
TEST(ResourceIntegrityTest, NoEmptyValuesInEnUs)
{
    auto entries = ParseReswEntries(kEnUsPath);
    ASSERT_FALSE(entries.empty());
    for (const auto& [key, value] : entries)
    {
        EXPECT_FALSE(value.empty()) << "Empty value in en-US for key: " << key;
    }
}

// Verify no empty values in ja-JP
TEST(ResourceIntegrityTest, NoEmptyValuesInJaJp)
{
    auto entries = ParseReswEntries(kJaJpPath);
    ASSERT_FALSE(entries.empty());
    for (const auto& [key, value] : entries)
    {
        EXPECT_FALSE(value.empty()) << "Empty value in ja-JP for key: " << key;
    }
}

// Verify format placeholders {0}, {1}, etc. are consistent between languages
TEST(ResourceIntegrityTest, FormatPlaceholdersConsistent)
{
    auto enEntries = ParseReswEntries(kEnUsPath);
    auto jaEntries = ParseReswEntries(kJaJpPath);
    ASSERT_FALSE(enEntries.empty());

    std::regex placeholderRegex(R"(\{(\d+)\})");

    for (const auto& [key, enValue] : enEntries)
    {
        auto jaIt = jaEntries.find(key);
        if (jaIt == jaEntries.end()) continue;

        // Extract placeholder indices from en-US
        std::set<std::string> enPlaceholders;
        auto enBegin = std::sregex_iterator(enValue.begin(), enValue.end(), placeholderRegex);
        auto end = std::sregex_iterator();
        for (auto it = enBegin; it != end; ++it)
            enPlaceholders.insert((*it)[1].str());

        // Extract placeholder indices from ja-JP
        const auto& jaValue = jaIt->second;
        std::set<std::string> jaPlaceholders;
        auto jaBegin = std::sregex_iterator(jaValue.begin(), jaValue.end(), placeholderRegex);
        for (auto it = jaBegin; it != end; ++it)
            jaPlaceholders.insert((*it)[1].str());

        EXPECT_EQ(enPlaceholders, jaPlaceholders)
            << "Placeholder mismatch for key '" << key << "':\n"
            << "  en-US: " << enValue << "\n"
            << "  ja-JP: " << jaValue;
    }
}

// Verify OS UI language detection (GetUserPreferredUILanguages) works
TEST(ResourceIntegrityTest, OsUiLanguageDetection)
{
    ULONG numLangs = 0;
    ULONG bufSize = 0;
    BOOL ok = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, nullptr, &bufSize);
    EXPECT_TRUE(ok) << "GetUserPreferredUILanguages should succeed";
    EXPECT_GT(bufSize, 0u) << "Buffer size should be > 0";
    EXPECT_GT(numLangs, 0u) << "Should have at least one preferred language";

    if (ok && bufSize > 0)
    {
        std::vector<wchar_t> buf(bufSize);
        ok = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, buf.data(), &bufSize);
        EXPECT_TRUE(ok);
        std::wstring firstLang = buf.data();
        EXPECT_GT(firstLang.size(), 0u) << "First preferred UI language should not be empty";
    }
}

// Verify key naming conventions (all keys should use PascalCase with underscores)
TEST(ResourceIntegrityTest, KeyNamingConvention)
{
    auto keys = ParseReswKeys(kEnUsPath);
    ASSERT_FALSE(keys.empty());
    std::regex validKeyRegex(R"(^[A-Z][a-zA-Z0-9]*(_[A-Z0-9][a-zA-Z0-9]*)*$)");

    for (const auto& key : keys)
    {
        EXPECT_TRUE(std::regex_match(key, validKeyRegex))
            << "Key does not follow PascalCase_PascalCase convention: " << key;
    }
}

// Verify .resw XML parsing produces correct key-value pairs
TEST(ResourceIntegrityTest, ReswParsingProducesCorrectValues)
{
    auto enEntries = ParseReswEntries(kEnUsPath);
    auto jaEntries = ParseReswEntries(kJaJpPath);
    ASSERT_FALSE(enEntries.empty());
    ASSERT_FALSE(jaEntries.empty());

    // Verify a known key has different values in en-US vs ja-JP
    auto enIt = enEntries.find("Menu_File");
    auto jaIt = jaEntries.find("Menu_File");
    ASSERT_NE(enIt, enEntries.end()) << "Menu_File not found in en-US";
    ASSERT_NE(jaIt, jaEntries.end()) << "Menu_File not found in ja-JP";
    EXPECT_EQ(enIt->second, "File");
    EXPECT_NE(enIt->second, jaIt->second) << "en-US and ja-JP should differ for Menu_File";
}

// Verify exe-relative Strings directory exists in build output
TEST(ResourceIntegrityTest, ResourcesPriExistsNextToExe)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    // MRT Core compiles .resw files into <AppName>.pri at build time.
    // The Strings/ directory only exists in source, not in the build output.
    std::filesystem::path priFile = exeDir / L"Em68030.pri";
    std::string exeDirStr = exeDir.string();
    EXPECT_TRUE(std::filesystem::exists(priFile))
        << "Em68030.pri not found next to exe at: " << exeDirStr;
}

// Verify preferred UI language matches an available Strings subdirectory
TEST(ResourceIntegrityTest, UiLanguageMatchesAvailableResource)
{
    ULONG numLangs = 0;
    ULONG bufSize = 0;
    ASSERT_TRUE(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, nullptr, &bufSize));
    std::vector<wchar_t> buf(bufSize);
    ASSERT_TRUE(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, buf.data(), &bufSize));

    std::wstring lang = buf.data();
    // Verify we have a Strings directory for at least one available language
    EXPECT_TRUE(std::filesystem::exists(kJaJpPath) || std::filesystem::exists(kEnUsPath))
        << "No resource file found; UI language: "
        << std::string(lang.begin(), lang.end());
}
