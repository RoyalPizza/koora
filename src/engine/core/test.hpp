#pragma once

// Built-in tests, run with `koora.exe --test`. Tests are plain functions that use TEST_CHECK,
// which stays on in release builds (unlike assert).

// Records a failed check at file:line and logs it. Used through TEST_CHECK.
void test_fail(const char* file, int line, const char* expression);

// Runs every suite listed in test.cpp. Returns the number of failed checks.
int test_run_all();

#define TEST_CHECK(condition)                                                                 \
    do                                                                                        \
    {                                                                                         \
        if (!(condition))                                                                     \
        {                                                                                     \
            test_fail(__FILE__, __LINE__, #condition);                                        \
        }                                                                                     \
    } while (0)

// Test suites, one per module, each in that module's <module>_tests.cpp. Add new ones to the
// table in test.cpp.
void arena_tests();
