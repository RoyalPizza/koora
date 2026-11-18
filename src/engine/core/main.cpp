// Entry point: sets up logging and memory, then runs the tests or the main loop.

#include "engine/core/arena.hpp"
#include "engine/core/log.hpp"
#include "engine/core/test.hpp"
#include "engine/platform/platform.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

constexpr size_t MEGABYTE = 1024 * 1024;
constexpr size_t PERMANENT_ARENA_SIZE = 64 * MEGABYTE;
constexpr size_t LEVEL_ARENA_SIZE = 64 * MEGABYTE;
constexpr size_t FRAME_ARENA_SIZE = 16 * MEGABYTE;

constexpr const char* WINDOW_TITLE = "koora";
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;

constexpr double FIXED_STEP = 1.0 / 60.0;
// After a long stall (debugger, window drag) drop the backlog instead of running many steps.
constexpr int MAX_STEPS_PER_FRAME = 5;

struct Options
{
    bool run_tests = false;
    int frame_limit = 0; // 0 = run until closed
};

struct Memory
{
    void* block = nullptr;
    Arena permanent;
    Arena level;
    Arena frame;
};

static bool parse_options(int argc, char** argv, Options& options);
static bool memory_init(Memory& memory);
static void memory_shutdown(Memory& memory);
static int run_game(const Options& options, Memory& memory);

int main(int argc, char** argv)
{
    std::string exe_folder = platform_get_exe_folder();
    std::string log_path = exe_folder + "logs\\koora.log";
    log_init(log_path.c_str());
    LOG_INFO("koora starting, exe folder '%s'", exe_folder.c_str());

    Options options;
    if (!parse_options(argc, argv, options))
    {
        log_shutdown();
        return 2;
    }

    int exit_code = 0;
    if (options.run_tests)
    {
        exit_code = test_run_all() == 0 ? 0 : 1;
        log_shutdown();
        return exit_code;
    }

    Memory memory;
    if (!memory_init(memory))
    {
        log_shutdown();
        return 1;
    }

    exit_code = run_game(options, memory);

    memory_shutdown(memory);
    LOG_INFO("clean shutdown, exit code %d", exit_code);
    log_shutdown();
    return exit_code;
}

static bool parse_options(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; i += 1)
    {
        const char* arg = argv[i];
        bool has_value = i + 1 < argc;

        if (strcmp(arg, "--test") == 0)
        {
            options.run_tests = true;
        }
        else if (strcmp(arg, "--frames") == 0 && has_value)
        {
            i += 1;
            options.frame_limit = atoi(argv[i]);
            if (options.frame_limit <= 0)
            {
                LOG_ERROR("--frames needs a positive number, got '%s'", argv[i]);
                return false;
            }
        }
        else
        {
            LOG_ERROR("unknown or incomplete argument '%s'", arg);
            return false;
        }
    }
    return true;
}

static bool memory_init(Memory& memory)
{
    size_t total = PERMANENT_ARENA_SIZE + LEVEL_ARENA_SIZE + FRAME_ARENA_SIZE;
    memory.block = platform_alloc(total);
    if (!memory.block)
    {
        LOG_ERROR("could not allocate %zu MB of memory", total / MEGABYTE);
        return false;
    }

    uint8_t* next = (uint8_t*)memory.block;
    arena_init(memory.permanent, "permanent", next, PERMANENT_ARENA_SIZE);
    next += PERMANENT_ARENA_SIZE;
    arena_init(memory.level, "level", next, LEVEL_ARENA_SIZE);
    next += LEVEL_ARENA_SIZE;
    arena_init(memory.frame, "frame", next, FRAME_ARENA_SIZE);

    LOG_INFO("memory: %zu MB total, arenas permanent %zu MB, level %zu MB, frame %zu MB",
             total / MEGABYTE, PERMANENT_ARENA_SIZE / MEGABYTE, LEVEL_ARENA_SIZE / MEGABYTE,
             FRAME_ARENA_SIZE / MEGABYTE);
    return true;
}

static void memory_shutdown(Memory& memory)
{
    platform_free(memory.block);
    memory.block = nullptr;
}

static int run_game(const Options& options, Memory& memory)
{
    Window* window = platform_create_window(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!window)
    {
        return 1;
    }

    double previous_time = platform_get_time();
    double accumulator = 0.0;
    int frame_count = 0;
    int update_count = 0;

    while (platform_poll_events(window))
    {
        double now = platform_get_time();
        accumulator += now - previous_time;
        previous_time = now;

        int steps = 0;
        while (accumulator >= FIXED_STEP && steps < MAX_STEPS_PER_FRAME)
        {
            update_count += 1;
            accumulator -= FIXED_STEP;
            steps += 1;
        }
        if (steps == MAX_STEPS_PER_FRAME && accumulator >= FIXED_STEP)
        {
            accumulator = 0.0;
        }

        arena_reset(memory.frame);
        frame_count += 1;
        if (options.frame_limit > 0 && frame_count >= options.frame_limit)
        {
            break;
        }
    }

    LOG_INFO("main loop ended after %d frames, %d updates", frame_count, update_count);
    platform_destroy_window(window);
    return 0;
}
