/* Copyright (c) 2024 Masaaki Hamada
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#if defined(_WIN32)
#pragma warning(disable : 4189)
#pragma warning(disable : 4702)
#pragma warning(disable : 4324)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <shellapi.h>
#else
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include <limits>
#include <type_traits>

#include <algorithm>
#include <any>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <variant>

#include <array>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <format>
#include <string>
#include <string_view>

#include <atomic>
#include <future>
#include <mutex>
#include <thread>
