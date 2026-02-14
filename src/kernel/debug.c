#include "debug.h"
#include <stdio.h>
#include <config/config.h>
#include <util/str_to_int.h>

static const char* const g_LogSeverityColors[] =
{
    [LVL_DEBUG]        = "\033[2;37m",
    [LVL_INFO]         = "\033[37m",
    [LVL_WARN]         = "\033[1;33m",
    [LVL_ERROR]        = "\033[1;31m",
    [LVL_CRITICAL]     = "\033[1;37;41m",
    [LVL_OK]           = "\x1b[1;32m"
};

static const int* const g_LogVLevels[] =
{
    [LVL_DEBUG]        = 2,
    [LVL_INFO]         = 3,
    [LVL_WARN]         = 1,
    [LVL_ERROR]        = -1,
    [LVL_CRITICAL]     = -1,
    [LVL_OK]           = 2
};

static const char* const g_ColorReset = "\033[0m";

static int checkVer(DebugLevel level)
{
    int vLevel = str_to_int(config_get("verbosity", "3"));
    if (g_LogVLevels[level] == -1) {
        return 0;
    }
    if (g_LogVLevels[level] <= vLevel) {
        return 0;
    }

    return 1;
}

void logf(const char* module, DebugLevel level, const char* fmt, ...)
{
    int shouldQuit = checkVer(level);
    if (shouldQuit == 1) {
        return;
    }

    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    
    if (level < MIN_LOG_LEVEL)
    {
        va_end(args);
        va_end(args_copy);
        return;
    }
    
    fputs(g_LogSeverityColors[level], VFS_FD_DEBUG);
    fprintf(VFS_FD_DEBUG, "[%s] ", module);
    vfprintf(VFS_FD_DEBUG, fmt, args);
    fputs(g_ColorReset, VFS_FD_DEBUG);
    fputc('\n', VFS_FD_DEBUG);

    va_end(args);
    va_end(args_copy);
}
