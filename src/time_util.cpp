#include "time_util.h"
#include <chrono>

static std::chrono::steady_clock::time_point g_start;

void time_start()
{
    g_start = std::chrono::steady_clock::now();
}

int64_t time_get_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - g_start).count();
}

int64_t time_get_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_start).count();
}

float time_get_sec()
{
    return std::chrono::duration<float>(
        std::chrono::steady_clock::now() - g_start).count();
}
