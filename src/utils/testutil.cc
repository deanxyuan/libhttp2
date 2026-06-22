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
 * @file testutil.cc
 * @brief Implementation of the lightweight test framework.
 */

#include "src/utils/testutil.h"
#include <vector>

namespace test {

/** @brief Global counter for assertion failures across all tests. */
int g_test_failures = 0;

namespace {
/**
 * @brief Internal struct holding a registered test's metadata and function pointer.
 */
struct Test {
    const char *base;  /**< Test suite / base class name. */
    const char *name;  /**< Individual test name. */
    void (*func)();    /**< Pointer to the test function. */
};
/** @brief Global vector of registered tests (lazily allocated). */
std::vector<Test> *tests;
}  // namespace

/**
 * @brief Registers a test function with the test framework.
 */
bool RegisterTest(const char *base, const char *name, void (*func)()) {
    if (tests == nullptr) {
        tests = new std::vector<Test>;
    }
    Test t;
    t.base = base;
    t.name = name;
    t.func = func;
    tests->push_back(t);
    return true;
}

/** @brief Executes all registered tests and prints a summary to stderr. */
int RunAllTests() {
    int num = 0;
    if (tests != nullptr) {
        for (size_t i = 0; i < tests->size(); i++) {
            const Test &t = (*tests)[i];
            fprintf(stderr, "==== Test %s.%s\n", t.base, t.name);
            (*t.func)();
            ++num;
        }
    }
    if (g_test_failures > 0) {
        fprintf(stderr, "==== FAILED %d assertions across %d tests\n", g_test_failures, num);
    } else {
        fprintf(stderr, "==== PASSED %d tests\n", num);
    }
    return g_test_failures > 0 ? 1 : 0;
}

}  // namespace test
