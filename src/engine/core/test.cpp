#include "engine/core/test.hpp"

#include "engine/core/log.hpp"

struct TestSuite
{
    const char* name;
    void (*run)();
};

constexpr TestSuite TEST_SUITES[] = {
    { "arena", arena_tests },
};

static int test_failures = 0;
static const char* test_current_suite = "";

void test_fail(const char* file, int line, const char* expression)
{
    test_failures += 1;
    log_write(LogLevel::LEVEL_ERROR, file, line, "[%s] check failed: %s", test_current_suite,
              expression);
}

int test_run_all()
{
    test_failures = 0;
    for (const TestSuite& suite : TEST_SUITES)
    {
        test_current_suite = suite.name;
        int failures_before = test_failures;
        suite.run();
        int failed = test_failures - failures_before;
        log_output("%s: %s", suite.name, failed == 0 ? "ok" : "FAILED");
    }

    int suite_count = (int)(sizeof(TEST_SUITES) / sizeof(TEST_SUITES[0]));
    log_output("tests: %d suites, %d failed checks", suite_count, test_failures);
    return test_failures;
}
