
#include "JitterLog.h"
#include "DejitterUtils.h"
#include <thread>


// formatting macros
#define JL_HEAD_FMT_IN  ",%10s,%10s,%10s"
#define JL_HEAD_FMT_IN2 JL_HEAD_FMT_IN JL_HEAD_FMT_IN
#define JL_HEAD_FMT_OUT ",%10s,%10s"
#define JL_DATA_FMT_IN  ",%10u,%10.6f,%10u"
#define JL_DATA_FMT_IN2 JL_DATA_FMT_IN JL_DATA_FMT_IN
#define JL_DATA_FMT_OUT ",%10u,%10.6f"

#ifdef HIER_MODE_SUPPORTED
  #define JL_FMT_HEAD   "%20s,%15s" JL_HEAD_FMT_IN2 JL_HEAD_FMT_IN2 JL_HEAD_FMT_OUT "\n"
  #define JL_FMT_DATA   "%20s,%15d" JL_DATA_FMT_IN2 JL_DATA_FMT_IN2 JL_DATA_FMT_OUT "\n"
#else
  #define JL_FMT_HEAD   "%20s,%15s" JL_HEAD_FMT_IN2 JL_HEAD_FMT_OUT "\n"
  #define JL_FMT_DATA   "%20s,%15d" JL_DATA_FMT_IN2 JL_DATA_FMT_OUT "\n"
#endif

/// static constants
static const double defaultExpireTimeout = 30.0; // seconds


/**
    LogDataPart
    Wrapper class to access part of the log data
*/
struct LogDataPart
{
    const LogData& data;
    uint32_t offset;
    uint32_t size;
};

/**
    JSON array serialization components
*/
template <typename SamplingFunc, typename SerializeFunc>
struct JsonArraySerializer
{
    const char* arrayName;
    SamplingFunc samplingCb;
    SerializeFunc serializeCb;

    // serialize data
    bool serialize(const LogDataPart& logPart, std::string& result) const;
};


/// typedefs
typedef uint32_t (*SampleDataInt)(const JitterLogSample& sample);
typedef float (*SampleDataFloat)(const JitterLogSample& sample);

typedef uint32_t (*SerializeInt)(uint32_t value, char* buffer, uint32_t bufferSize);
typedef uint32_t (*SerializeFloat)(float value, char* buffer, uint32_t bufferSize);

typedef JsonArraySerializer<SampleDataInt, SerializeInt> JsonArraySerializerInt;
typedef JsonArraySerializer<SampleDataFloat, SerializeFloat> JsonArraySerializerFloat;



/// static functions
static uint32_t jsonSerializeU32(uint32_t value, char* buffer, uint32_t bufferSize)
{
    int result = snprintf(buffer, bufferSize, "%u", value);
    return (result > 0) ? static_cast<uint32_t>(result) : 0;
}
static uint32_t jsonSerializeFloat(float value, char* buffer, uint32_t bufferSize)
{
    int result = snprintf(buffer, bufferSize, "%.6f", value);
    return (result > 0) ? static_cast<uint32_t>(result) : 0;
}


// log data sampling functions
static uint32_t sampleRateHP1(const JitterLogSample& sample)   { return sample.hp1.rate; }
static float    sampleDFHP1(const JitterLogSample& sample)     { return sample.hp1.delayFactor; }
static uint32_t sampleMLRHP1(const JitterLogSample& sample)    { return sample.hp1.mediaLossRate; }
static uint32_t sampleRateHP2(const JitterLogSample& sample)   { return sample.hp2.rate; }
static float    sampleDFHP2(const JitterLogSample& sample)     { return sample.hp2.delayFactor; }
static uint32_t sampleMLRHP2(const JitterLogSample& sample)    { return sample.hp2.mediaLossRate; }
#ifdef HIER_MODE_SUPPORTED
static uint32_t sampleRateLP1(const JitterLogSample& sample)   { return sample.lp1.rate; }
static float    sampleDFLP1(const JitterLogSample& sample)     { return sample.lp1.delayFactor; }
static uint32_t sampleMLRLP1(const JitterLogSample& sample)    { return sample.lp1.mediaLossRate; }
static uint32_t sampleRateLP2(const JitterLogSample& sample)   { return sample.lp2.rate; }
static float    sampleDFLP2(const JitterLogSample& sample)     { return sample.lp2.delayFactor; }
static uint32_t sampleMLRLP2(const JitterLogSample& sample)    { return sample.lp2.mediaLossRate; }
#endif
static uint32_t sampleRateOut(const JitterLogSample& sample)   { return sample.output.rate; }
static float    sampleDFOut(const JitterLogSample& sample)     { return sample.output.delayFactor; }
static uint32_t sampleCurrInput(const JitterLogSample& sample) { return sample.activeInput; }


template <typename SamplingFunc, typename SerializeFunc>
bool JsonArraySerializer<SamplingFunc, SerializeFunc>::serialize(const LogDataPart& logPart, std::string& result) const
{
    char buffer[32];

    result += "\"";
    result += arrayName;
    result += "\": [";

    for(uint32_t i = 0; i < logPart.size; ++i)
    {
        if (i > 0)
            result += ',';

        const JitterLogSample& sample = logPart.data[logPart.offset + i];

        uint32_t numBytes = (*serializeCb)((*samplingCb)(sample), buffer, sizeof(buffer));
        if (numBytes == 0)
            return false;

        result.append(buffer, numBytes);
    }

    result += "]";
    return true;
}


/// Separate thread is required to perform time consuming log save operation

/// clone log data storage
static void cloneLogData(const LogData& src, LogData& dest)
{
    dest.reset();
    for(uint32_t i = 0; i < src.getSize(); ++i)
        dest.append(src[i]);
}

/// save function
static void taskSaveLog(LogThreadDataPtr ltd)
{
    FILE* f = fopen(ltd->fileName.c_str(), "wt");
    bool success;

    if (f)
    {
        ltd->status.store(LTS_EXECUTING);
        success = JitterLog::saveLogCSV(f, ltd->startTime, ltd->data);
        fclose(f);
    }
    else
    {
        success = false;
    }

    ltd->status.store(success ? LTS_SUCCESS : LTS_FAILED);
}


/// constructor
LogThreadData::LogThreadData()
    : startTime(0)
    , data(JitterLog::maxSampleCount)
{
    status.store(LTS_IDLE);
}


/// constructor/destructor
JitterLog::JitterLog()
    : m_data(maxSampleCount)
    , m_startTime(0)
    , m_threadDataPtr(new LogThreadData())
{
    m_jsonOpts.maxSamples = 0;
}

JitterLog::~JitterLog()
{
}

/// add entry to the log
bool JitterLog::addEntry(const JitterLogSample& sample)
{
    time_t currTime = time(NULL);
    uint32_t numNewSamples = 0;

    if (m_data.isEmpty())
    {
        numNewSamples = 1;
        m_startTime = currTime;
    }
    else
    {
        double diff = difftime(currTime, m_startTime);
        diff -= m_data.getSize();

        if (diff > 1.0)
        {
            // we missed 1 second somehow, need to add 2 samples at once
            numNewSamples = 2;
        }
        else if (diff > 0.5)
        {
            // add 1 sample
            numNewSamples = 1;
        }
        else
        {
            // we're trying to add sample that should be already stored, ignore
            numNewSamples = 0;
        }
    }

    if (numNewSamples > 0)
    {
        // adjust start time
        uint32_t totalSamples = m_data.getSize() + numNewSamples;
        if (totalSamples > maxSampleCount)
            m_startTime += totalSamples - maxSampleCount;

        for(uint32_t i = 0; i < numNewSamples; ++i)
            m_data.append(sample);

        // remove unused JSON data by checking expiration timeouts
        removeUnusedJsonData(numNewSamples);

        // save JSON data immediately
        saveJsonData();
        return true;
    }

    return false;
}

/// clear
void JitterLog::clear()
{
    m_data.reset();
    m_startTime = 0;

    // clear JSON data as well
    saveJsonData();
}

/// static JSON data source control
/// process add request
bool JitterLog::jsonSourceAdd(time_t startTime, const char* fileName)
{
    // find existing element
    JsonFileMap::iterator it = m_jsonFileMap.find(startTime);
    if (it == m_jsonFileMap.end())
    {
        // new element
        JsonDataSource src;
        src.fileName = fileName;
        src.refCount = 1;
        src.expireTime = defaultExpireTimeout;

        m_jsonFileMap[startTime] = src;

    }
    else
    {
        // existing element
        // increase reference counter
        it->second.refCount += 1;
    }

    return true;
}

/// static JSON data source control
/// process ping request
bool JitterLog::jsonSourcePing(time_t startTime)
{
    // find existing element
    JsonFileMap::iterator it = m_jsonFileMap.find(startTime);
    if (it == m_jsonFileMap.end())
    {
        // no item found
        return false;
    }

    // set maximum expiration timeout to prevent removing the data
    it->second.expireTime = defaultExpireTimeout;
    return true;
}

/// static JSON data source control
/// set JSON log options
void JitterLog::setJsonLogOptions(const JsonLogOptions& opts)
{
    m_jsonOpts = opts;
}

/// static JSON data source control
/// process remove request
bool JitterLog::jsonSourceRemove(time_t startTime)
{
    // find existing element
    JsonFileMap::iterator it = m_jsonFileMap.find(startTime);
    if (it == m_jsonFileMap.end())
        return false;

    JsonDataSource& src = it->second;
    if (--src.refCount == 0)
    {
        // remove created files
        removeJsonFiles(src);
        // we have no more consumers that use this data, destroy
        m_jsonFileMap.erase(it);
    }
    return true;
}

/// static JSON data source control
/// process remove all request
bool JitterLog::jsonSourceRemoveAll()
{
    // remove all created files from the file system
    JsonFileMap::const_iterator it;
    for(it = m_jsonFileMap.cbegin(); it != m_jsonFileMap.cend(); ++it)
    {
        const JsonDataSource& src = it->second;
        // remove created files
        removeJsonFiles(src);
    }

    m_jsonFileMap.clear();
    return true;
}

/// randomize log data for testing purposes
void JitterLog::generateRandomData(uint32_t size)
{
    Random::initialize();

    JitterLogSample sample = {0};

    uint32_t nominalRate = Random::getIntRange(4e6, 40e6);
    uint32_t rateDeviation = Random::getIntRange(2, 8) * nominalRate / 100;

    for(uint32_t i = 0; i < size; ++i)
    {
        // input
        InputJitterData& in = sample.hp1;
        in.rate = Random::getIntRange(nominalRate - rateDeviation, nominalRate + rateDeviation);
        in.delayFactor = Random::getFloatRange(0.000050, 0.006000);
        in.mediaLossRate = Random::getIntRange(0, 3);

        // output
        OutputJitterData& out = sample.output;
        out.rate = nominalRate;
        out.delayFactor = Random::getFloatRange(0.000010, 0.000050);

        m_data.append(sample);
    }

    m_startTime = time(NULL) - size;
}

/// save log data to the file
bool JitterLog::saveFile(const char* fileName)
{
    // check if log save task is already running
    if (m_threadDataPtr->status.load() == LTS_EXECUTING)
        return false;

    // clone log data for the store thread
    m_threadDataPtr->fileName = fileName;
    m_threadDataPtr->startTime = m_startTime;
    cloneLogData(m_data, m_threadDataPtr->data);

    std::thread logThread(taskSaveLog, m_threadDataPtr);
    logThread.detach();

    return true;
}

/// get log save operation status
LogThreadStatus JitterLog::getSaveOpStatus() const
{
    if (!m_threadDataPtr)
        return LTS_TERMINATED;

    return m_threadDataPtr->status.load();
}

/// serialize part of the log data in JSON format
bool JitterLog::createJsonData(const char* fileName, time_t startTime, int32_t expireTime, uint32_t offset, uint32_t size) const
{
    FILE* f = fopen(fileName, "wt");
    if (!f)
        return false;

    uint32_t maxSize = m_data.getSize();
    if (offset + size > maxSize)
        size = (offset > maxSize) ? 0 : (maxSize - offset);

    fprintf(f, "{ \"entryCount\": %u, \"startTime\": %lu, \"expireTime\": %d", size, startTime, expireTime);

    // string storage to serialize arrays
    std::string storage;
    storage.reserve(32768); // 32K should be large enough to hold serialized array data

    // serialize arrays
    LogDataPart logPart = { m_data, offset, size };

    JsonArraySerializerInt serializerListI[] = 
    {
        { "active",  sampleCurrInput, jsonSerializeU32   }, // Active input
        { "hp1Rate", sampleRateHP1,   jsonSerializeU32   }, // HP1 rate
        { "hp2Rate", sampleRateHP2,   jsonSerializeU32   }, // HP2 rate
        { "hp1MLR",  sampleMLRHP1,    jsonSerializeU32   }, // HP1 media loss rate
        { "hp2MLR",  sampleMLRHP2,    jsonSerializeU32   }, // HP2 media loss rate
#ifdef HIER_MODE_SUPPORTED
        { "lp1Rate", sampleRateLP1,   jsonSerializeU32   }, // LP1 rate
        { "lp2Rate", sampleRateLP2,   jsonSerializeU32   }, // LP2 rate
        { "lp1MLR",  sampleMLRLP1,    jsonSerializeU32   }, // LP1 media loss rate
        { "lp2MLR",  sampleMLRLP2,    jsonSerializeU32   }, // LP2 media loss rate
#endif
        { "outRate", sampleRateOut,   jsonSerializeU32   }  // Output rate
    };

    JsonArraySerializerFloat serializerListF[] = 
    {
        { "hp1DF",   sampleDFHP1,     jsonSerializeFloat }, // HP1 delay factor
        { "hp2DF",   sampleDFHP2,     jsonSerializeFloat }, // HP2 delay factor
#ifdef HIER_MODE_SUPPORTED
        { "lp1DF",   sampleDFLP1,     jsonSerializeFloat }, // LP1 delay factor
        { "lp2DF",   sampleDFLP2,     jsonSerializeFloat }, // LP2 delay factor
#endif
        { "outDF",   sampleDFOut,     jsonSerializeFloat }  // Output delay factor
    };

    uint32_t sizeI = arraySize(serializerListI);
    for(uint32_t i = 0; i < sizeI; ++i)
    {
        storage.assign(", ", 2);
        if (serializerListI[i].serialize(logPart, storage))
            fwrite(storage.c_str(), storage.size(), 1, f);
    }

    uint32_t sizeF = arraySize(serializerListF);
    for(uint32_t i = 0; i < sizeF; ++i)
    {
        storage.assign(", ", 2);
        if (serializerListF[i].serialize(logPart, storage))
            fwrite(storage.c_str(), storage.size(), 1, f);
    }

    fwrite(" }", 2, 1, f);

    fclose(f);

    // compress data to increase transfer speed
    if (m_jsonOpts.gzipCompression)
    {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "gzip -c %s > %s.gz", fileName, fileName);
        system(cmd);
    }

    return true;
}

/// serialize real time log data in JSON format
bool JitterLog::createJsonDataRealTime(const char* fileName, uint32_t size) const
{
    uint32_t maxSize = m_data.getSize();
    uint32_t offset = (size > maxSize) ? 0 : (maxSize - size);

    return createJsonData(fileName, m_startTime + offset, -1, offset, maxSize - offset);
}

/// serialize static log data in JSON format
bool JitterLog::createJsonDataStatic(const char* fileName, time_t startTime, time_t endTime, int32_t expireTime) const
{
    uint32_t maxSize = m_data.getSize();

    time_t S1 = m_startTime;           // time for the first available sample
    time_t E1 = m_startTime + maxSize; // time for the last available sample
    time_t S2 = startTime;             // time for the first required sample
    time_t E2 = endTime;               // time for the last required sample

    // time range intersection
    time_t S = max(S1, S2);
    time_t E = min(E1, E2);
    double dt = difftime(E, S);

    if (dt <= 0)
    {
        // no data available
        return createJsonData(fileName, startTime, expireTime, 0, 0);
    }
    else
    {
        uint32_t offset = static_cast<uint32_t>(difftime(S, S1));
        uint32_t size = static_cast<uint32_t>(dt + 0.5); // round up

        return createJsonData(fileName, S, expireTime, offset, size);
    }
}

/// save all JSON data
bool JitterLog::saveJsonData() const
{
    const std::string& logNameRT = m_jsonOpts.logNameRealTime;
    uint32_t maxSamples = m_jsonOpts.maxSamples;

    if ((maxSamples == 0) || logNameRT.empty())
        return false;

    // realtime data
    if (!createJsonDataRealTime(logNameRT.c_str(), maxSamples))
        return false;

    // static data
    JsonFileMap::const_iterator it;
    for(it = m_jsonFileMap.cbegin(); it != m_jsonFileMap.cend(); ++it)
    {
        time_t startTime = it->first;
        const JsonDataSource& src = it->second;

        if (!createJsonDataStatic(src.fileName.c_str(), startTime, startTime + maxSamples, src.expireTime))
            return false;
    }

    return true;
}

/// remove unused JSON data
void JitterLog::removeUnusedJsonData(double timeDelta)
{
    // static data
    JsonFileMap::iterator it;
    for(it = m_jsonFileMap.begin(); it != m_jsonFileMap.end(); ++it)
    {
        JsonDataSource& src = it->second;

        src.expireTime -= timeDelta;
        if (src.expireTime <= 0)
        {
            // remove created file
            removeJsonFiles(src);
            it = m_jsonFileMap.erase(it);
        }
    }
}

/// remove JSON file(s) associated with the data source
void JitterLog::removeJsonFiles(const JsonDataSource& source)
{
    // remove created files
    std::string name(source.fileName);
    // original file
    remove(name.c_str());
    // compressed file (if exists)
    name += ".gz";
    remove(name.c_str());
}

/// create log header (CSV)
uint32_t JitterLog::createLogHeaderCSV(char* buffer, uint32_t bufferSize)
{
    if (!buffer || !bufferSize)
        return 0;

    int result = snprintf(buffer, bufferSize, JL_FMT_HEAD,
        "Time",
        "Active Input",
#ifdef HIER_MODE_SUPPORTED
        "HP Rate 1","HP DF 1","HP MLR 1",
        "LP Rate 1","LP DF 1","LP MLR 1",
        "HP Rate 2","HP DF 2","HP MLR 2",
        "LP Rate 2","LP DF 2","LP MLR 2",
#else
        "Rate 1","DF 1","MLR 1",
        "Rate 2","DF 2","MLR 2",
#endif
        "Out Rate","Out DF"
    );

    return (result > 0) ? static_cast<uint32_t>(result) : 0;
}

/// create log entry (CSV)
uint32_t JitterLog::createLogEntryCSV(char* buffer, uint32_t bufferSize, time_t entryTime, const JitterLogSample& sample)
{
    if (!buffer || !bufferSize)
        return 0;

    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%F %X", localtime(&entryTime));

    const OutputJitterData& out = sample.output;
    const InputJitterData& hp1 = sample.hp1;
    const InputJitterData& hp2 = sample.hp2;
#ifdef HIER_MODE_SUPPORTED
    const InputJitterData& lp1 = sample.lp1;
    const InputJitterData& lp2 = sample.lp2;
#endif

    int result = snprintf(buffer, bufferSize, JL_FMT_DATA,
        timeStr,
        sample.activeInput,
#ifdef HIER_MODE_SUPPORTED
        hp1.rate, hp1.delayFactor, hp1.mediaLossRate,
        lp1.rate, lp1.delayFactor, lp1.mediaLossRate,
        hp2.rate, hp2.delayFactor, hp2.mediaLossRate,
        lp2.rate, lp2.delayFactor, lp2.mediaLossRate,
#else
        hp1.rate, hp1.delayFactor, hp1.mediaLossRate,
        hp2.rate, hp2.delayFactor, hp2.mediaLossRate,
#endif
        out.rate, out.delayFactor
    );

    return (result > 0) ? static_cast<uint32_t>(result) : 0;
}

/// save log data in CSV format
bool JitterLog::saveLogCSV(FILE* f, time_t startTime, const LogData& data)
{
    char buffer[512];
    uint32_t numBytes;

    // create and write header
    numBytes = JitterLog::createLogHeaderCSV(buffer, sizeof(buffer));
    if (numBytes == 0)
        return false;

    fwrite(buffer, numBytes, 1, f);

    // create and write entries
    uint32_t size = data.getSize();
    for(uint32_t i = 0; i < size; ++i)
    {
        const JitterLogSample& sample = data[i];
        time_t entryTime = startTime + i;

        numBytes = JitterLog::createLogEntryCSV(buffer, sizeof(buffer), entryTime, sample);
        if (numBytes == 0)
            return false;

        fwrite(buffer, numBytes, 1, f);
    }

    return true;
}
