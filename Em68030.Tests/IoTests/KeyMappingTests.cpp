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
#include <set>

#include "IO/KeyMapping.h"

namespace Em68030::Tests {

using IO::WindowsVkToLinuxKey;

// Tests use raw hex VK codes to avoid conflicts with Windows SDK macros.
// Comments note the VK_* name for readability.

// ============================================================================
// Escape and function keys
// ============================================================================

TEST(KeyMappingTest, Escape_MapsToKeyEsc) {
    EXPECT_EQ(1, WindowsVkToLinuxKey(0x1B));   // VK_ESCAPE -> KEY_ESC
}

TEST(KeyMappingTest, F1_MapsToKeyF1) {
    EXPECT_EQ(59, WindowsVkToLinuxKey(0x70));  // VK_F1 -> KEY_F1
}

TEST(KeyMappingTest, F10_MapsToKeyF10) {
    EXPECT_EQ(68, WindowsVkToLinuxKey(0x79));  // VK_F10 -> KEY_F10
}

TEST(KeyMappingTest, F11_MapsToKeyF11) {
    EXPECT_EQ(87, WindowsVkToLinuxKey(0x7A));  // VK_F11 -> KEY_F11
}

TEST(KeyMappingTest, F12_MapsToKeyF12) {
    EXPECT_EQ(88, WindowsVkToLinuxKey(0x7B));  // VK_F12 -> KEY_F12
}

TEST(KeyMappingTest, AllFunctionKeys_AreMapped) {
    // F1(0x70) through F12(0x7B)
    for (int vk = 0x70; vk <= 0x7B; vk++) {
        EXPECT_NE(0, WindowsVkToLinuxKey(vk))
            << "VK 0x" << std::hex << vk << " not mapped";
    }
}

// ============================================================================
// Number row
// ============================================================================

TEST(KeyMappingTest, Key1_MapsToKey1) {
    EXPECT_EQ(2, WindowsVkToLinuxKey(0x31));   // '1' -> KEY_1
}

TEST(KeyMappingTest, Key0_MapsToKey0) {
    EXPECT_EQ(11, WindowsVkToLinuxKey(0x30));  // '0' -> KEY_0
}

TEST(KeyMappingTest, AllDigits_AreMapped) {
    for (int vk = 0x30; vk <= 0x39; vk++) {
        EXPECT_NE(0, WindowsVkToLinuxKey(vk))
            << "VK 0x" << std::hex << vk << " not mapped";
    }
}

// ============================================================================
// Letter keys
// ============================================================================

TEST(KeyMappingTest, A_MapsToKeyA) {
    EXPECT_EQ(30, WindowsVkToLinuxKey(0x41));  // 'A' -> KEY_A
}

TEST(KeyMappingTest, Z_MapsToKeyZ) {
    EXPECT_EQ(44, WindowsVkToLinuxKey(0x5A));  // 'Z' -> KEY_Z
}

TEST(KeyMappingTest, AllLetters_AreMapped) {
    for (int vk = 0x41; vk <= 0x5A; vk++) {
        EXPECT_NE(0, WindowsVkToLinuxKey(vk))
            << "VK 0x" << std::hex << vk << " not mapped";
    }
}

TEST(KeyMappingTest, AllLetters_AreUnique) {
    std::set<uint16_t> codes;
    for (int vk = 0x41; vk <= 0x5A; vk++) {
        auto code = WindowsVkToLinuxKey(vk);
        EXPECT_TRUE(codes.insert(code).second)
            << "Duplicate Linux key code " << code << " for VK 0x" << std::hex << vk;
    }
}

// ============================================================================
// Special keys
// ============================================================================

TEST(KeyMappingTest, Backspace_MapsToKeyBackspace) {
    EXPECT_EQ(14, WindowsVkToLinuxKey(0x08));  // VK_BACK -> KEY_BACKSPACE
}

TEST(KeyMappingTest, Tab_MapsToKeyTab) {
    EXPECT_EQ(15, WindowsVkToLinuxKey(0x09));  // VK_TAB -> KEY_TAB
}

TEST(KeyMappingTest, Enter_MapsToKeyEnter) {
    EXPECT_EQ(28, WindowsVkToLinuxKey(0x0D));  // VK_RETURN -> KEY_ENTER
}

TEST(KeyMappingTest, Space_MapsToKeySpace) {
    EXPECT_EQ(57, WindowsVkToLinuxKey(0x20));  // VK_SPACE -> KEY_SPACE
}

// ============================================================================
// Modifier keys
// ============================================================================

TEST(KeyMappingTest, Shift_MapsToLeftShift) {
    EXPECT_EQ(42, WindowsVkToLinuxKey(0x10));  // VK_SHIFT -> KEY_LEFTSHIFT
}

TEST(KeyMappingTest, LShift_MapsToLeftShift) {
    EXPECT_EQ(42, WindowsVkToLinuxKey(0xA0));  // VK_LSHIFT -> KEY_LEFTSHIFT
}

TEST(KeyMappingTest, RShift_MapsToRightShift) {
    EXPECT_EQ(54, WindowsVkToLinuxKey(0xA1));  // VK_RSHIFT -> KEY_RIGHTSHIFT
}

TEST(KeyMappingTest, Control_MapsToLeftCtrl) {
    EXPECT_EQ(29, WindowsVkToLinuxKey(0x11));  // VK_CONTROL -> KEY_LEFTCTRL
}

TEST(KeyMappingTest, LControl_MapsToLeftCtrl) {
    EXPECT_EQ(29, WindowsVkToLinuxKey(0xA2));  // VK_LCONTROL -> KEY_LEFTCTRL
}

TEST(KeyMappingTest, RControl_MapsToRightCtrl) {
    EXPECT_EQ(97, WindowsVkToLinuxKey(0xA3));  // VK_RCONTROL -> KEY_RIGHTCTRL
}

TEST(KeyMappingTest, Alt_MapsToLeftAlt) {
    EXPECT_EQ(56, WindowsVkToLinuxKey(0x12));  // VK_MENU -> KEY_LEFTALT
}

TEST(KeyMappingTest, LAlt_MapsToLeftAlt) {
    EXPECT_EQ(56, WindowsVkToLinuxKey(0xA4));  // VK_LMENU -> KEY_LEFTALT
}

TEST(KeyMappingTest, RAlt_MapsToRightAlt) {
    EXPECT_EQ(100, WindowsVkToLinuxKey(0xA5)); // VK_RMENU -> KEY_RIGHTALT
}

TEST(KeyMappingTest, CapsLock_MapsToKeyCapslock) {
    EXPECT_EQ(58, WindowsVkToLinuxKey(0x14));  // VK_CAPITAL -> KEY_CAPSLOCK
}

// ============================================================================
// Punctuation
// ============================================================================

TEST(KeyMappingTest, Minus_MapsToKeyMinus) {
    EXPECT_EQ(12, WindowsVkToLinuxKey(0xBD));  // VK_OEM_MINUS -> KEY_MINUS
}

TEST(KeyMappingTest, Equal_MapsToKeyEqual) {
    EXPECT_EQ(13, WindowsVkToLinuxKey(0xBB));  // VK_OEM_PLUS -> KEY_EQUAL
}

TEST(KeyMappingTest, LeftBrace_MapsToKeyLeftBrace) {
    EXPECT_EQ(26, WindowsVkToLinuxKey(0xDB));  // VK_OEM_4 -> KEY_LEFTBRACE
}

TEST(KeyMappingTest, RightBrace_MapsToKeyRightBrace) {
    EXPECT_EQ(27, WindowsVkToLinuxKey(0xDD));  // VK_OEM_6 -> KEY_RIGHTBRACE
}

TEST(KeyMappingTest, Backslash_MapsToKeyBackslash) {
    EXPECT_EQ(43, WindowsVkToLinuxKey(0xDC));  // VK_OEM_5 -> KEY_BACKSLASH
}

TEST(KeyMappingTest, Semicolon_MapsToKeySemicolon) {
    EXPECT_EQ(39, WindowsVkToLinuxKey(0xBA));  // VK_OEM_1 -> KEY_SEMICOLON
}

TEST(KeyMappingTest, Apostrophe_MapsToKeyApostrophe) {
    EXPECT_EQ(40, WindowsVkToLinuxKey(0xDE));  // VK_OEM_7 -> KEY_APOSTROPHE
}

TEST(KeyMappingTest, Grave_MapsToKeyGrave) {
    EXPECT_EQ(41, WindowsVkToLinuxKey(0xC0));  // VK_OEM_3 -> KEY_GRAVE
}

TEST(KeyMappingTest, Comma_MapsToKeyComma) {
    EXPECT_EQ(51, WindowsVkToLinuxKey(0xBC));  // VK_OEM_COMMA -> KEY_COMMA
}

TEST(KeyMappingTest, Period_MapsToKeyDot) {
    EXPECT_EQ(52, WindowsVkToLinuxKey(0xBE));  // VK_OEM_PERIOD -> KEY_DOT
}

TEST(KeyMappingTest, Slash_MapsToKeySlash) {
    EXPECT_EQ(53, WindowsVkToLinuxKey(0xBF));  // VK_OEM_2 -> KEY_SLASH
}

// ============================================================================
// Navigation
// ============================================================================

TEST(KeyMappingTest, Insert_MapsToKeyInsert) {
    EXPECT_EQ(110, WindowsVkToLinuxKey(0x2D)); // VK_INSERT -> KEY_INSERT
}

TEST(KeyMappingTest, Delete_MapsToKeyDelete) {
    EXPECT_EQ(111, WindowsVkToLinuxKey(0x2E)); // VK_DELETE -> KEY_DELETE
}

TEST(KeyMappingTest, Home_MapsToKeyHome) {
    EXPECT_EQ(102, WindowsVkToLinuxKey(0x24)); // VK_HOME -> KEY_HOME
}

TEST(KeyMappingTest, End_MapsToKeyEnd) {
    EXPECT_EQ(107, WindowsVkToLinuxKey(0x23)); // VK_END -> KEY_END
}

TEST(KeyMappingTest, PageUp_MapsToKeyPageUp) {
    EXPECT_EQ(104, WindowsVkToLinuxKey(0x21)); // VK_PRIOR -> KEY_PAGEUP
}

TEST(KeyMappingTest, PageDown_MapsToKeyPageDown) {
    EXPECT_EQ(109, WindowsVkToLinuxKey(0x22)); // VK_NEXT -> KEY_PAGEDOWN
}

TEST(KeyMappingTest, ArrowUp_MapsToKeyUp) {
    EXPECT_EQ(103, WindowsVkToLinuxKey(0x26)); // VK_UP -> KEY_UP
}

TEST(KeyMappingTest, ArrowDown_MapsToKeyDown) {
    EXPECT_EQ(108, WindowsVkToLinuxKey(0x28)); // VK_DOWN -> KEY_DOWN
}

TEST(KeyMappingTest, ArrowLeft_MapsToKeyLeft) {
    EXPECT_EQ(105, WindowsVkToLinuxKey(0x25)); // VK_LEFT -> KEY_LEFT
}

TEST(KeyMappingTest, ArrowRight_MapsToKeyRight) {
    EXPECT_EQ(106, WindowsVkToLinuxKey(0x27)); // VK_RIGHT -> KEY_RIGHT
}

// ============================================================================
// Numpad
// ============================================================================

TEST(KeyMappingTest, Numpad0_MapsToKeyKP0) {
    EXPECT_EQ(82, WindowsVkToLinuxKey(0x60));  // VK_NUMPAD0 -> KEY_KP0
}

TEST(KeyMappingTest, Numpad9_MapsToKeyKP9) {
    EXPECT_EQ(73, WindowsVkToLinuxKey(0x69));  // VK_NUMPAD9 -> KEY_KP9
}

TEST(KeyMappingTest, NumpadMultiply_MapsToKeyKPAsterisk) {
    EXPECT_EQ(55, WindowsVkToLinuxKey(0x6A));  // VK_MULTIPLY -> KEY_KPASTERISK
}

TEST(KeyMappingTest, NumpadPlus_MapsToKeyKPPlus) {
    EXPECT_EQ(78, WindowsVkToLinuxKey(0x6B));  // VK_ADD -> KEY_KPPLUS
}

TEST(KeyMappingTest, NumpadMinus_MapsToKeyKPMinus) {
    EXPECT_EQ(74, WindowsVkToLinuxKey(0x6D));  // VK_SUBTRACT -> KEY_KPMINUS
}

TEST(KeyMappingTest, NumpadDot_MapsToKeyKPDot) {
    EXPECT_EQ(83, WindowsVkToLinuxKey(0x6E));  // VK_DECIMAL -> KEY_KPDOT
}

TEST(KeyMappingTest, NumpadSlash_MapsToKeyKPSlash) {
    EXPECT_EQ(98, WindowsVkToLinuxKey(0x6F));  // VK_DIVIDE -> KEY_KPSLASH
}

// ============================================================================
// Misc
// ============================================================================

TEST(KeyMappingTest, NumLock_MapsToKeyNumlock) {
    EXPECT_EQ(69, WindowsVkToLinuxKey(0x90));  // VK_NUMLOCK -> KEY_NUMLOCK
}

TEST(KeyMappingTest, ScrollLock_MapsToKeyScrollLock) {
    EXPECT_EQ(70, WindowsVkToLinuxKey(0x91));  // VK_SCROLL -> KEY_SCROLLLOCK
}

TEST(KeyMappingTest, Pause_MapsToKeyPause) {
    EXPECT_EQ(119, WindowsVkToLinuxKey(0x13)); // VK_PAUSE -> KEY_PAUSE
}

TEST(KeyMappingTest, PrintScreen_MapsToKeySysRq) {
    EXPECT_EQ(99, WindowsVkToLinuxKey(0x2C));  // VK_SNAPSHOT -> KEY_SYSRQ
}

// ============================================================================
// Unmapped keys
// ============================================================================

TEST(KeyMappingTest, UnmappedKey_ReturnsZero) {
    EXPECT_EQ(0, WindowsVkToLinuxKey(0x00));
    EXPECT_EQ(0, WindowsVkToLinuxKey(0xFF));
    EXPECT_EQ(0, WindowsVkToLinuxKey(0x5B));   // VK_LWIN
}

// ============================================================================
// CharToScancode tests
// ============================================================================

TEST(CharToScancodeTest, LowercaseA_MapsToKeyA_NoShift) {
    auto [code, shift] = IO::CharToScancode('a');
    EXPECT_EQ(30, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, UppercaseA_MapsToKeyA_WithShift) {
    auto [code, shift] = IO::CharToScancode('A');
    EXPECT_EQ(30, code);
    EXPECT_TRUE(shift);
}

TEST(CharToScancodeTest, LowercaseZ_MapsToKeyZ) {
    auto [code, shift] = IO::CharToScancode('z');
    EXPECT_EQ(44, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, Digit0_MapsToKey0) {
    auto [code, shift] = IO::CharToScancode('0');
    EXPECT_EQ(11, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, Digit9_MapsToKey9) {
    auto [code, shift] = IO::CharToScancode('9');
    EXPECT_EQ(10, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, Space_MapsToKeySpace) {
    auto [code, shift] = IO::CharToScancode(' ');
    EXPECT_EQ(57, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, Enter_MapsToKeyEnter) {
    auto [code, shift] = IO::CharToScancode('\n');
    EXPECT_EQ(28, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, Tab_MapsToKeyTab) {
    auto [code, shift] = IO::CharToScancode('\t');
    EXPECT_EQ(15, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, Slash_NoShift) {
    auto [code, shift] = IO::CharToScancode('/');
    EXPECT_EQ(53, code);
    EXPECT_FALSE(shift);
}

TEST(CharToScancodeTest, QuestionMark_WithShift) {
    auto [code, shift] = IO::CharToScancode('?');
    EXPECT_EQ(53, code);
    EXPECT_TRUE(shift);
}

TEST(CharToScancodeTest, ExclamationMark_Shift1) {
    auto [code, shift] = IO::CharToScancode('!');
    EXPECT_EQ(2, code);  // KEY_1
    EXPECT_TRUE(shift);
}

TEST(CharToScancodeTest, AtSign_Shift2) {
    auto [code, shift] = IO::CharToScancode('@');
    EXPECT_EQ(3, code);  // KEY_2
    EXPECT_TRUE(shift);
}

TEST(CharToScancodeTest, Tilde_ShiftGrave) {
    auto [code, shift] = IO::CharToScancode('~');
    EXPECT_EQ(41, code);
    EXPECT_TRUE(shift);
}

TEST(CharToScancodeTest, Pipe_ShiftBackslash) {
    auto [code, shift] = IO::CharToScancode('|');
    EXPECT_EQ(43, code);
    EXPECT_TRUE(shift);
}

TEST(CharToScancodeTest, UnmappedChar_ReturnsZero) {
    auto [code, shift] = IO::CharToScancode('\x01');
    EXPECT_EQ(0, code);
}

TEST(CharToScancodeTest, AllLowercase_AreMapped) {
    for (char ch = 'a'; ch <= 'z'; ch++) {
        auto [code, shift] = IO::CharToScancode(ch);
        EXPECT_NE(0, code) << "'" << ch << "' not mapped";
        EXPECT_FALSE(shift);
    }
}

TEST(CharToScancodeTest, AllDigits_AreMapped) {
    for (char ch = '0'; ch <= '9'; ch++) {
        auto [code, shift] = IO::CharToScancode(ch);
        EXPECT_NE(0, code) << "'" << ch << "' not mapped";
        EXPECT_FALSE(shift);
    }
}

} // namespace Em68030::Tests
