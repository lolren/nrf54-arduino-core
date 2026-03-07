/*
 * Arduino Random Number Functions
 *
 * random(), randomSeed()
 *
 * Licensed under the Apache License 2.0
 */

#include "Arduino.h"

// Pseudo-random number generator state
static uint32_t g_randomSeed = 1;

// Linear Congruential Generator constants
// Using constants from glibc (LCG)
#define RANDOM_MULTIPLIER  1103515245UL
#define RANDOM_INCREMENT   12345UL

void randomSeed(unsigned long seed)
{
    if (seed == 0) {
        seed = 1;
    }
    g_randomSeed = seed;
}

long arduinoRandom(long max)
{
    if (max <= 0) {
        return 0;
    }
    return arduinoRandomRange(0, max);
}

long arduinoRandomRange(long min, long max)
{
    if (min >= max) {
        return min;
    }

    // Generate next random number
    g_randomSeed = (g_randomSeed * RANDOM_MULTIPLIER + RANDOM_INCREMENT);

    // Map to requested range
    uint32_t range = max - min;
    return min + (g_randomSeed % range);
}
