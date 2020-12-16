
#ifndef DEFS_H
#define DEFS_H

#include "Platform.h"

/// constants
static const uint32_t maxSamples = 10;

/**
    DataSource
    Source for the data
    There is up to 2 physical inputs per logical one (primary and secondary)
*/
enum DataSource
{
    DS_IN_HP1 = 0,
    DS_IN_LP1,
    DS_IN_HP2,
    DS_IN_LP2,
    DS_OUT_HP,
    DS_OUT_LP,
    DS_COUNT,

    DS_IN_BASE = DS_IN_HP1,
    DS_IN_TOTAL = 4,
    DS_OUT_BASE = DS_OUT_HP,
    DS_OUT_TOTAL = DS_COUNT - DS_OUT_BASE
};

/**
    InputData
    Information at the input stage
*/
struct InputData
{
    float delayFactor;
    uint32_t mediaLossRate;  // number of lost or out-of-order packets
    uint32_t rate;           // bps
    float pcrArray[maxSamples];
    uint8_t samples;
};

/**
    OutputData
    Information at the output stage
*/
struct OutputData
{
    float delayFactor;
    uint32_t rate;           // bps
    float pcrArray[maxSamples];
    uint8_t samples;
};

/**
    DataSample
    Information for all inputs/outputs
*/
struct DataSample
{
    InputData in[DS_IN_TOTAL];
    OutputData out[DS_OUT_TOTAL];
};

#endif //DEFS_H
