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

// Prevent Windows macros from conflicting with std::min/std::max
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Windows headers
#include <windows.h>

// Undefine other problematic Windows macros
#ifdef GetObject
#undef GetObject
#endif
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// Undefine GetObject, etc. macros that conflict with WinRT
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

// WinRT headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Core.h>
#include <wil/cppwinrt_helpers.h>

// Implementation headers required by generated XamlTypeInfo.g.cpp
// (winrt::make<T>() requires complete implementation types)
#include "MainWindow.xaml.h"
#include "Views/ConsoleWindow.xaml.h"
#include "Views/FramebufferWindow.xaml.h"
#include "Views/BreakpointsWindow.xaml.h"
#include "Views/SettingsWindow.xaml.h"
#include "Views/InputDialog.xaml.h"
#include "Views/AboutDialog.xaml.h"

// C++ standard library
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <span>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <cassert>
#include <bit>
