#pragma once
#include <cstdint>

// Call once at program start before any threads are spawned.
void    time_start();

int64_t time_get_us();   // microseconds since time_start()
int64_t time_get_ms();   // milliseconds since time_start()
float   time_get_sec();  // seconds since time_start()
