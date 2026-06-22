/*
    Copyright (c) 2011 The LevelDB Authors. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    * Neither the name of Google Inc. nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * @file testutil.h
 * @brief Lightweight test framework with assertion macros and test registration.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <sstream>

namespace test {

/**
 * @brief Runs all tests registered via the TEST() macro.
 * @return 0 if all tests pass, 1 if any test failed.
 */
int RunAllTests();

/** @brief Global failure counter incremented by the Tester destructor on assertion failure. */
extern int g_test_failures;

/**
 * @brief Helper class that accumulates assertion results and reports failures on destruction.
 */
class Tester {
public:
    /**
     * @brief Constructor. Records the source file and line number for failure reporting.
     * @param f Source file name (__FILE__).
     * @param l Source line number (__LINE__).
     */
    Tester(const char *f, int l)
        : _ok(true)
        , _file(f)
        , _line(l) {}

    /** @brief Destructor. Prints failure details to stderr if any assertion failed. */
    ~Tester() {
        if (!_ok) {
            fprintf(stderr, "%s:%d:%s\n", _file, _line, _ss.str().c_str());
            ++g_test_failures;
        }
    }

    /**
     * @brief Asserts that a boolean condition is true.
     * @param b The condition to test.
     * @param msg A string representation of the condition for failure output.
     * @return Reference to this Tester for chaining.
     */
    Tester &Is(bool b, const char *msg) {
        if (!b) {
            _ss << "Assertion failure " << msg;
            _ok = false;
        }
        return *this;
    }

    /** @brief Macro that generates a binary comparison assertion method. */
#define BINARY_OPERATION(name, op)                                                                                     \
    template <typename X, typename Y>                                                                                  \
    Tester &name(const X &x, const Y &y) {                                                                             \
        if (!(x op y)) {                                                                                               \
            _ss << " failed: " << x << (" " #op " ") << y;                                                             \
            _ok = false;                                                                                               \
        }                                                                                                              \
        return *this;                                                                                                  \
    }

    BINARY_OPERATION(IsEq, ==)   /**< @brief Asserts x == y. */
    BINARY_OPERATION(IsNe, !=)   /**< @brief Asserts x != y. */
    BINARY_OPERATION(IsGe, >=)   /**< @brief Asserts x >= y. */
    BINARY_OPERATION(IsGt, >)    /**< @brief Asserts x > y. */
    BINARY_OPERATION(IsLe, <=)   /**< @brief Asserts x <= y. */
    BINARY_OPERATION(IsLt, <)    /**< @brief Asserts x < y. */

#undef BINARY_OPERATION

    /**
     * @brief Stream operator for appending diagnostic values to failure messages.
     * @param value The value to append (only used when the assertion has failed).
     * @return Reference to this Tester for chaining.
     */
    template <typename V>
    Tester &operator<<(const V &value) {
        if (!_ok) {
            _ss << " " << value;
        }
        return *this;
    }

private:
    bool _ok;
    const char *_file;
    int _line;
    std::stringstream _ss;
};

/** @brief Asserts that condition c is true. */
#define ASSERT_TRUE(c) ::test::Tester(__FILE__, __LINE__).Is((c), #c)
/** @brief Asserts that a == b. */
#define ASSERT_EQ(a, b) ::test::Tester(__FILE__, __LINE__).IsEq((a), (b))
/** @brief Asserts that a != b. */
#define ASSERT_NE(a, b) ::test::Tester(__FILE__, __LINE__).IsNe((a), (b))
/** @brief Asserts that a >= b. */
#define ASSERT_GE(a, b) ::test::Tester(__FILE__, __LINE__).IsGe((a), (b))
/** @brief Asserts that a > b. */
#define ASSERT_GT(a, b) ::test::Tester(__FILE__, __LINE__).IsGt((a), (b))
/** @brief Asserts that a <= b. */
#define ASSERT_LE(a, b) ::test::Tester(__FILE__, __LINE__).IsLe((a), (b))
/** @brief Asserts that a < b. */
#define ASSERT_LT(a, b) ::test::Tester(__FILE__, __LINE__).IsLt((a), (b))

/** @brief Concatenates two preprocessor tokens (indirection macro). */
#define TCONCAT(a, b) TCONCAT1(a, b)
/** @brief Inner token concatenation helper. */
#define TCONCAT1(a, b) a##b

/**
 * @brief Defines and registers a test function within a test base class.
 * @param base The base class name for the test fixture (use the class name or a plain name).
 * @param name The test name (must be a valid C++ identifier).
 */
#define TEST(base, name)                                                                                               \
    class TCONCAT(_Test_, name)                                                                                        \
        : public base {                                                                                                \
    public:                                                                                                            \
        void _Run();                                                                                                   \
        static void _RunIt() {                                                                                         \
            TCONCAT(_Test_, name) t;                                                                                   \
            t._Run();                                                                                                  \
        }                                                                                                              \
    };                                                                                                                 \
    bool TCONCAT(_Test_ignored_, name) = ::test::RegisterTest(#base, #name, &TCONCAT(_Test_, name)::_RunIt);           \
    void TCONCAT(_Test_, name)::_Run()

/**
 * @brief Registers a test function with the test framework.
 * @param base The base class / test suite name.
 * @param name The individual test name.
 * @param func Pointer to the test function.
 * @return Always returns true (used for static initialization).
 */
bool RegisterTest(const char *base, const char *name, void (*func)());

}  // namespace test
