// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdio>

#include <fmt/format.h>

// For asserts we'd like to keep all the junk executed when an assert happens away from the
// important code in the function. One way of doing this is to put all the relevant code inside a
// lambda and force the compiler to not inline it. Unfortunately, MSVC seems to have no syntax to
// specify __declspec on lambda functions, so what we do instead is define a noinline wrapper
// template that calls the lambda. This seems to generate an extra instruction at the call-site
// compared to the ideal implementation (which wouldn't support ASSERT_MSG parameters), but is good
// enough for our purposes.
template <typename Fn>
#if defined(_MSC_VER)
__declspec(noinline, noreturn)
#elif defined(__GNUC__)
[[noreturn, gnu::noinline, gnu::cold]]
#endif
static void assert_noinline_call(const Fn& fn) {
    fn();
    throw "";
}

#define ASSERT(_a_) \
    do if (!(_a_)) { assert_noinline_call([] { \
        fmt::print(stderr, "Assertion Failed!: {}\n", #_a_); \
    }); } while (false)

#define ASSERT_MSG(_a_, ...) \
    do if (!(_a_)) { assert_noinline_call([&] { \
        fmt::print(stderr, "Assertion Failed!: {}\n", #_a_); \
        fmt::print(stderr, "Message: " __VA_ARGS__); \
        fmt::print(stderr, "\n"); \
    }); } while (false)

#define UNREACHABLE() ASSERT_MSG(false, "Unreachable code!")
#define UNREACHABLE_MSG(...) ASSERT_MSG(false, __VA_ARGS__)

#ifdef NDEBUG
#define DEBUG_ASSERT(_a_)
#define DEBUG_ASSERT_MSG(_a_, ...)
#else // debug
#define DEBUG_ASSERT(_a_) ASSERT(_a_)
#define DEBUG_ASSERT_MSG(_a_, ...) ASSERT_MSG(_a_, __VA_ARGS__)
#endif

#define UNIMPLEMENTED() DEBUG_ASSERT_MSG(false, "Unimplemented code!")
#define UNIMPLEMENTED_MSG(_a_, ...) ASSERT_MSG(false, _a_, __VA_ARGS__)
