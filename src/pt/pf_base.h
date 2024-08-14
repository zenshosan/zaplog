/* Copyright (c) 2024 Masaaki Hamada
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "pf_common_headers.h"

#ifdef NDEBUG
#define PF_BUILD_RELEASE
#else
#define PF_BUILD_DEBUG
#endif

#if defined(_WIN32)
#define PF_COMPILER_MSVC
#elif defined(__GNUC__)
#define PF_COMPILER_GCC
#elif defined(__clang__)
#define PF_COMPILER_CLANG
#endif

#if defined(__GNUC__)
#define PF_ALWAYS_INLINE inline __attribute__((__always_inline__))
#elif defined(__clang__)
#define PF_ALWAYS_INLINE inline __attribute__((__always_inline__))
#elif defined(_WIN32)
#define PF_ALWAYS_INLINE __forceinline
#else
#define PF_ALWAYS_INLINE inline
#endif

#if defined(__GNUC__)
#define PT_UNREACHABLE() __builtin_unreachable()
#define PT_ASSERT(x) assert(x)
#define PT_DEBUGBREAK()
#elif defined(__clang__)
#define PT_UNREACHABLE() __builtin_unreachable()
#define PT_ASSERT(x) assert(x)
#define PT_DEBUGBREAK()
#elif defined(_WIN32)
#include <intrin.h>
#define PT_UNREACHABLE() __assume(0)
#define PT_ASSERT(x) _ASSERTE(x)
#define PT_DEBUGBREAK() __debugbreak()
#else
#define PT_UNREACHABLE() abort();
// #define PT_UNREACHABLE()   std::unreachable()
#define PT_ASSERT(x) assert(x)
#endif

#define PF_CASE_TO_TSTRING(p, x)                                                                   \
    case x:                                                                                        \
        p = _T(#x);                                                                                \
        break;
#define PF_DEFAULT_TO_TSTRING(p, x)                                                                \
    default:                                                                                       \
        p = x;                                                                                     \
        break;
#define PF_CASE_TO_STRING(p, x)                                                                    \
    case x:                                                                                        \
        p = #x;                                                                                    \
        break;
#define PF_DEFAULT_TO_STRING(p, x)                                                                 \
    default:                                                                                       \
        p = x;                                                                                     \
        break;

#define PF_CONCAT_NAME(prefix, line) prefix##_pf_unique_var_##line
#define PF_MAKE_VARNAME(prefix, line) PF_CONCAT_NAME(prefix, line)
#define PF_VAR(prefix) PF_MAKE_VARNAME(prefix, __LINE__)

// see DEFINE_ENUM_FLAG_OPERATORS in winnt.h"
#define PF_DEFINE_BIT_OPERATORS(Type)                                                              \
    inline Type operator&(Type a, Type b)                                                          \
    {                                                                                              \
        using IntType = std::underlying_type<Type>::type;                                          \
        return static_cast<Type>(static_cast<IntType>(a) & static_cast<IntType>(b));               \
    }                                                                                              \
    inline Type operator|(Type a, Type b)                                                          \
    {                                                                                              \
        using IntType = std::underlying_type<Type>::type;                                          \
        return static_cast<Type>(static_cast<IntType>(a) | static_cast<IntType>(b));               \
    }                                                                                              \
    inline Type& operator|=(Type& a, Type b)                                                       \
    {                                                                                              \
        a = a | b;                                                                                 \
        return a;                                                                                  \
    };                                                                                             \
    inline Type& operator&=(Type& a, Type b)                                                       \
    {                                                                                              \
        a = a & b;                                                                                 \
        return a;                                                                                  \
    };                                                                                             \
    inline Type operator~(Type a)                                                                  \
    {                                                                                              \
        using IntType = std::underlying_type<Type>::type;                                          \
        return static_cast<Type>(~static_cast<IntType>(a));                                        \
    }                                                                                              \
    inline bool is_set(Type val, Type flag)                                                        \
    {                                                                                              \
        return (val & flag) != static_cast<Type>(0);                                               \
    }                                                                                              \
    inline void flip_bit(Type& val, Type flag)                                                     \
    {                                                                                              \
        val = is_set(val, flag) ? (val & (~flag)) : (val | flag);                                  \
    }

namespace pf {
namespace fw_ {

class NonCopyable
{
  protected:
    constexpr NonCopyable(void) = default;
    ~NonCopyable(void) = default;
    constexpr NonCopyable(NonCopyable&&) = default;
    constexpr NonCopyable& operator=(NonCopyable&&) = default;

  private:
    // note that the following functions are private
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

/*
 * go like defer for c++
 *
 * usege:
 * auto a = openSomeghing();
 * pf_make_defer { closeSomething(a); };  // semicolon needed
 */
template<class Functor>
class Defer
{
  private:
    Functor m_func;

  public:
    explicit Defer(Functor&& func) noexcept
      : m_func{ std::forward<Functor>(func) }
    {
    }
    ~Defer(void) noexcept { m_func(); }
    Defer(const Defer&) = delete;
    Defer& operator=(Defer&& rhs) = delete;
    Defer& operator=(const Defer&) = delete;
};

class DeferMakeHelper final
{
  public:
    template<typename Functor>
    Defer<Functor> operator+(Functor&& func)
    {
        return Defer{ std::forward<Functor>(func) };
    }
};

} // namespace fw_ {

#if defined(_WIN32)
#define SWITCH_TO_THREAD() SwitchToThread()
#else
#define WCHAR wchar_t
#define SWITCH_TO_THREAD() std::this_thread::yield()
#endif

using NonCopyable = fw_::NonCopyable;
using DeferMakeHelper = fw_::DeferMakeHelper;
using PathString = std::variant<std::basic_string<char>, std::basic_string<WCHAR>>;

} // namespace pf

#define pf_make_defer                                                                              \
    auto PF_MAKE_VARNAME(defer_, __LINE__) = pf::DeferMakeHelper{} + [&](void) noexcept -> void
