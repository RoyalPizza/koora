// Entry point: sets up logging and memory, then runs the tests or the main loop.

#include "engine/assets/assets.hpp"
#include "engine/core/arena.hpp"
#include "engine/core/log.hpp"
#include "engine/core/test.hpp"
#include "engine/platform/platform.hpp"
#include "engine/renderer/renderer.hpp"
#include "game/game.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

constexpr size_t MEGABYTE = 1024 * 1024;
constexpr size_t PERMANENT_ARENA_SIZE = 192 * MEGABYTE;
constexpr size_t LEVEL_ARENA_SIZE = 64 * MEGABYTE;
constexpr size_t FRAME_ARENA_SIZE = 16 * MEGABYTE;

constexpr const char* WINDOW_TITLE = "koora";
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;

constexpr float CLEAR_COLOR[3] = { 0.10f, 0.12f, 0.18f };

constexpr double FIXED_STEP = 1.0 / 60.0;
// After a long stall (debugger, window drag) drop the backlog instead of running many steps.
constexpr int MAX_STEPS_PER_FRAME = 5;

struct Options
{
    bool run_tests = false;
    int frame_limit = 0; // 0 = run until closed
    const char* screenshot_path = nullptr; // saved on the last frame of a --frames run
    int window_width = WINDOW_WIDTH;
    int window_height = WINDOW_HEIGHT;
};

// Where the exe finds things. Dev builds run from <root>/bin/ and read loose files.
struct Paths
{
    std::string exe_folder;
    std::string root;
    std::string game_folder;
    std::string shader_folder;
};

struct Memory
{
    void* block = nullptr;
    Arena permanent;
    Arena level;
    Arena frame;
};

static Paths make_paths();
static bool parse_options(int argc, char** argv, Options& options);
static bool parse_size(const char* text, int* width, int* height);
static bool memory_init(Memory& memory);
static void memory_shutdown(Memory& memory);
static int run_game(const Options& options, Memory& memory);

int main(int argc, char** argv)
{
    Paths paths = make_paths();
    std::string log_path = paths.exe_folder + "logs\\koora.log";
    log_init(log_path.c_str());
    LOG_INFO("koora starting, root '%s'", paths.root.c_str());

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
    if (!memory_init(memory) ||
        !assets_init(memory.permanent, paths.game_folder.c_str(), paths.shader_folder.c_str()))
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

static Paths make_paths()
{
    Paths paths;
    paths.exe_folder = platform_get_exe_folder();

    // The root is the parent of the exe's folder: strip the trailing separator, then the name.
    std::string folder = paths.exe_folder;
    if (!folder.empty())
    {
        folder.pop_back();
    }
    size_t separator = folder.find_last_of("\\/");
    paths.root = separator == std::string::npos ? folder : folder.substr(0, separator + 1);

    paths.game_folder = paths.root + "src\\game\\";
    paths.shader_folder = paths.root + "src\\engine\\renderer\\shaders\\";
    return paths;
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
        else if (strcmp(arg, "--window") == 0 && has_value)
        {
            i += 1;
            if (!parse_size(argv[i], &options.window_width, &options.window_height))
            {
                LOG_ERROR("--window needs WIDTHxHEIGHT, got '%s'", argv[i]);
                return false;
            }
        }
        else if (strcmp(arg, "--screenshot") == 0 && has_value)
        {
            i += 1;
            options.screenshot_path = argv[i];
        }
        else
        {
            LOG_ERROR("unknown or incomplete argument '%s'", arg);
            return false;
        }
    }
    if (options.screenshot_path && options.frame_limit == 0)
    {
        LOG_ERROR("--screenshot needs --frames");
        return false;
    }
    return true;
}

// "WIDTHxHEIGHT", both positive.
static bool parse_size(const char* text, int* width, int* height)
{
    char* end = nullptr;
    long parsed_width = strtol(text, &end, 10);
    if (*end != 'x')
    {
        return false;
    }
    long parsed_height = strtol(end + 1, &end, 10);
    if (*end != 0 || parsed_width <= 0 || parsed_height <= 0)
    {
        return false;
    }
    *width = (int)parsed_width;
    *height = (int)parsed_height;
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
    Window* window = platform_create_window(WINDOW_TITLE, options.window_width,
                                            options.window_height);
    if (!window)
    {
        return 1;
    }
    if (!renderer_init(window, memory.permanent))
    {
        platform_destroy_window(window);
        return 1;
    }
    if (!game_init(memory.permanent, memory.level))
    {
        LOG_ERROR("game failed to start");
        renderer_shutdown();
        platform_destroy_window(window);
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
            game_update((float)FIXED_STEP);
            update_count += 1;
            accumulator -= FIXED_STEP;
            steps += 1;
        }
        if (steps == MAX_STEPS_PER_FRAME && accumulator >= FIXED_STEP)
        {
            accumulator = 0.0;
        }

        bool last_frame = options.frame_limit > 0 && frame_count + 1 >= options.frame_limit;
        renderer_begin_frame(CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2]);
        game_render(memory.frame);
        if (last_frame && options.screenshot_path)
        {
            renderer_save_screenshot(options.screenshot_path, memory.frame);
        }
        renderer_end_frame();

        arena_reset(memory.frame);
        frame_count += 1;
        if (last_frame)
        {
            break;
        }
    }

    LOG_INFO("main loop ended after %d frames, %d updates", frame_count, update_count);
    renderer_shutdown();
    platform_destroy_window(window);
    return 0;
}
