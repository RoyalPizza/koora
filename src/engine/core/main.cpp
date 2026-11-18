// Entry point: sets up logging, then runs the engine.

#include "engine/core/log.hpp"
#include "engine/platform/platform.hpp"

#include <string>

int main()
{
    std::string exe_folder = platform_get_exe_folder();
    std::string log_path = exe_folder + "logs\\koora.log";
    log_init(log_path.c_str());
    LOG_INFO("koora starting, exe folder '%s'", exe_folder.c_str());

    log_shutdown();
    return 0;
}
