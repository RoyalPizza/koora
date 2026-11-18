#include "engine/core/log.hpp"

#include "engine/platform/platform.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

constexpr int KEEP_OLD_LOGS = 5;
constexpr int MAX_PATH_LEN = 1024;
constexpr int MAX_LINE_LEN = 2048;
constexpr int BUFFER_SIZE = 16 * 1024;

constexpr const char* LEVEL_NAMES[] = { "DEBUG", "INFO", "WARN", "ERROR" };

// Lines wait here until a flush; the file is opened only to append, never held open.
static char log_path[MAX_PATH_LEN];
static bool log_has_file = false;
static double log_start_time = 0.0;
static char log_buffer[BUFFER_SIZE];
static int log_buffer_used = 0;

static void rotate_logs(const char* path);
static void make_numbered_path(const char* path, int number, char* out, int out_len);
static void flush_buffer();
static const char* file_name_only(const char* path);

bool log_init(const char* path)
{
    log_start_time = platform_get_time();
    log_buffer_used = 0;
    log_has_file = false;
    snprintf(log_path, MAX_PATH_LEN, "%s", path);

    char folder[MAX_PATH_LEN];
    snprintf(folder, MAX_PATH_LEN, "%s", path);
    char* last_separator = strrchr(folder, '\\');
    char* last_slash = strrchr(folder, '/');
    if (last_slash > last_separator)
    {
        last_separator = last_slash;
    }
    if (last_separator)
    {
        *last_separator = 0;
        platform_create_folder(folder);
    }

    rotate_logs(path);

    FILE* file = platform_file_open(path, "wb");
    if (!file)
    {
        LOG_ERROR("could not create log file '%s'", path);
        return false;
    }
    fclose(file);

    log_has_file = true;
    return true;
}

void log_shutdown()
{
    LOG_INFO("log closed");
    flush_buffer();
    log_has_file = false;
}

void log_write(LogLevel level, const char* file, int line, const char* format, ...)
{
    char message[MAX_LINE_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(message, MAX_LINE_LEN, format, args);
    va_end(args);

    char text[MAX_LINE_LEN];
    double seconds = platform_get_time() - log_start_time;
    int len = snprintf(text, MAX_LINE_LEN, "[%.3f] [%s] %s:%d %s\n", seconds,
                       LEVEL_NAMES[(int)level], file_name_only(file), line, message);
    if (len < 0)
    {
        return;
    }
    if (len >= MAX_LINE_LEN)
    {
        len = MAX_LINE_LEN - 1;
        text[len - 1] = '\n';
    }

    fputs(text, stderr);
    platform_debug_output(text);

    if (log_buffer_used + len > BUFFER_SIZE)
    {
        flush_buffer();
    }
    memcpy(log_buffer + log_buffer_used, text, (size_t)len);
    log_buffer_used += len;

    if (level == LogLevel::LEVEL_WARN || level == LogLevel::LEVEL_ERROR)
    {
        flush_buffer();
    }
}

static void rotate_logs(const char* path)
{
    char from[MAX_PATH_LEN];
    char to[MAX_PATH_LEN];

    make_numbered_path(path, KEEP_OLD_LOGS, to, MAX_PATH_LEN);
    if (platform_file_exists(to))
    {
        platform_file_delete(to);
    }

    for (int i = KEEP_OLD_LOGS - 1; i >= 0; i -= 1)
    {
        make_numbered_path(path, i, from, MAX_PATH_LEN);
        make_numbered_path(path, i + 1, to, MAX_PATH_LEN);
        if (platform_file_exists(from))
        {
            platform_file_rename(from, to);
        }
    }
}

// Number 0 is the path itself; n gives "name.n.log".
static void make_numbered_path(const char* path, int number, char* out, int out_len)
{
    if (number == 0)
    {
        snprintf(out, (size_t)out_len, "%s", path);
        return;
    }

    int stem_len = (int)strlen(path);
    const char* extension = strrchr(path, '.');
    if (extension && strcmp(extension, ".log") == 0)
    {
        stem_len = (int)(extension - path);
    }
    snprintf(out, (size_t)out_len, "%.*s.%d.log", stem_len, path, number);
}

static void flush_buffer()
{
    if (log_buffer_used == 0 || !log_has_file)
    {
        return;
    }

    FILE* file = platform_file_open(log_path, "ab");
    if (!file)
    {
        return;
    }
    fwrite(log_buffer, 1, (size_t)log_buffer_used, file);
    fclose(file);
    log_buffer_used = 0;
}

static const char* file_name_only(const char* path)
{
    const char* name = path;
    for (const char* c = path; *c != 0; c += 1)
    {
        if (*c == '\\' || *c == '/')
        {
            name = c + 1;
        }
    }
    return name;
}
