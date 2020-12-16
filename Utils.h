
#ifndef UTILS_H
#define UTILS_H

#include "Platform.h"

/**
   Utils
*/
namespace Utils
{
    // convert rate: bits/sec to packets/sec
    double bpsToPps(double bps, uint32_t packetSize);
    // convert rate: packets/sec to bits/sec
    double ppsToBps(double pps, uint32_t packetSize);

    // get the relative distance netween 2 values
    template <typename T>
    T      getDistance(T a, T b);
};

/**
   Random
   Generate random values
*/
namespace Random
{
    // initialize random number generator
    void     initialize();
    // get random unit float value
    double   getUnitFloat();
    // get random float value from the range
    double   getFloatRange(double a, double b);
    // get random uint32 value from the range
    uint32_t getIntRange(uint32_t a, uint32_t b);
};


/// implementation
template <typename T>
T Utils::getDistance(T a, T b)
{
    return (a > b) ? (a - b) : (b - a);
}

#endif // UTILS_H
