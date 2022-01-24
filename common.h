#pragma once

#include <cstdio>

// Setup debug printing.
#ifdef DEBUG_PRINT
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) 
#endif
