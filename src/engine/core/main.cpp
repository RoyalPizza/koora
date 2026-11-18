// Entry point: sets up logging, then runs the tests or the engine.

#include "engine/core/log.hpp"
#include "engine/core/test.hpp"
#include "engine/platform/platform.hpp"

#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    std::string exe_folder = platform_get_exe_folder();
    std::string log_path = exe_folder + "logs\\koora.log";
    log_init(log_path.c_str());
    LOG_INFO("koora starting, exe folder '%s'", exe_folder.c_str());

    bool run_tests = false;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--test") == 0)
        {
            run_tests = true;
        }
    }

    int exit_code = 0;
    if (run_tests)
    {
        exit_code = test_run_all() == 0 ? 0 : 1;
    }

    log_shutdown();
    return exit_code;
}
