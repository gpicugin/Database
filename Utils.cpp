#include "StdAfx.h"
#include "Utils.h"


// convert rate: bits/sec to packets/sec
double Utils::bpsToPps(double bps, uint32_t packetSize)
{
    return (bps / (8 * packetSize));
}

// convert rate: packets/sec to bits/sec
double Utils::ppsToBps(double pps, uint32_t packetSize)
{
    return (pps * (8 * packetSize));
}


// initialize random number generator
void Random::initialize()
{
    srand(time(NULL));
}

// get random unit float value
double Random::getUnitFloat()
{
    return static_cast<double>(rand()) / INT_MAX;
}

// get random float value from the range
double Random::getFloatRange(double a, double b)
{
    return (a + getUnitFloat() * (b - a));
}

// get random uint32 value from the range
uint32_t Random::getIntRange(uint32_t a, uint32_t b)
{
    uint32_t d = b - a + 1;
    return (a + rand() % d);
}
