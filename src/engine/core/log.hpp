#pragma once

// Session log. One file per run; the previous 5 are kept as name.1.log ... name.5.log.
// Every line also goes to stderr and the debugger output.

// Members are prefixed because <windows.h> defines ERROR as a macro.
enum class LogLevel
{
    LEVEL_DEBUG,
    LEVEL_INFO,
    LEVEL_WARN,
    LEVEL_ERROR,
};

// Rotates old logs and opens a new one at path (UTF-8, ends in ".log"). Creates the folder.
// Returns false if the file can't be opened; logging then goes to stderr only.
bool log_init(const char* path);

// Writes the closing line and closes the file.
void log_shutdown();

// Formats one line: "[seconds] [LEVEL] file.cpp:line message". Warnings and errors flush.
void log_write(LogLevel level, const char* file, int line, const char* format, ...);

// Program results for scripts to read: the plain message to stdout, plus a line in the log
// file. Not shown in the debugger output.
void log_output(const char* format, ...);

#ifdef NDEBUG
#define LOG_DEBUG(...) ((void)0)
#else
#define LOG_DEBUG(...) log_write(LogLevel::LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#endif

#define LOG_INFO(...) log_write(LogLevel::LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) log_write(LogLevel::LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LogLevel::LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
