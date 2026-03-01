#pragma once

// Lightweight precompiled header for tests — no WinRT dependencies.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// C standard
#include <cstdint>
#include <cstring>
#include <cmath>
#include <climits>
#include <cassert>

// C++ standard library (matches headers used by Core/IO code)
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
