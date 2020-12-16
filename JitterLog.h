
#ifndef JITTER_LOG_H
#define JITTER_LOG_H

#include "JitterDefs.h"
#include "RingBuffer.h"
#include <atomic>
#include <map>
#include <memory>


/**
    LogThreadStatus
*/
enum LogThreadStatus
{
    LTS_IDLE = 0,
    LTS_EXECUTING,
    LTS_FAILED,
    LTS_SUCCESS,
    LTS_TERMINATED
};

/**
    JitterLogSample
    Jitter information for active input(s) and output
*/
struct JitterLogSample
{
    uint32_t activeInput; // 1=primary, 2=secondary
    InputJitterData hp1;
    InputJitterData hp2;
#ifdef HIER_MODE_SUPPORTED
    InputJitterData lp1;
    InputJitterData lp2;
#endif
    OutputJitterData output;
};

/**
    JsonDataInfo
    Link to the JSON data source
*/
struct JsonDataSource
{
    std::string fileName;
    uint32_t refCount;
    int32_t expireTime; // time before removing the data source if no extra action is taken to keep it (seconds)
};

/**
    JsonLogOptions
    JSON logging options
*/
struct JsonLogOptions
{
    std::string logNameRealTime;
    uint32_t maxSamples;
    bool gzipCompression; // compress JSON data with gzip
};

/// typedefs
typedef RingBuffer<JitterLogSample> LogData;

/**
    LogThreadData
    Copy of the log data for the separate thread
    performing the save operation
*/
struct LogThreadData
{
    std::string fileName;
    time_t startTime;
    LogData data;
    std::atomic<LogThreadStatus> status;

    // constructor
    LogThreadData();
};

/// typedefs
typedef std::shared_ptr<LogThreadData> LogThreadDataPtr;

/**
    JitterLog
    Store jitter input/output data with the resolution of 1 second
*/
class JitterLog
{
    public:
        static const uint32_t maxSampleCount = 14 * 86400; // 2 weeks of data, measured each second

    private:
        typedef std::map<time_t, JsonDataSource> JsonFileMap;

        LogData         m_data;
        time_t          m_startTime;
        JsonFileMap     m_jsonFileMap;
        JsonLogOptions  m_jsonOpts;

        // shared pointer is used to correctly handle the case
        // when thread is still running and using the object
        // but log instance has been destroyed already
        LogThreadDataPtr m_threadDataPtr;

    public:
        /// constructor/destructor
        JitterLog();
        ~JitterLog();

        /// add entry to the log
        bool addEntry(const JitterLogSample& sample);
        /// clear
        void clear();

        /// static JSON data source control
        void setJsonLogOptions(const JsonLogOptions& opts);
        bool jsonSourceAdd(time_t startTime, const char* fileName);
        bool jsonSourcePing(time_t startTime);
        bool jsonSourceRemove(time_t startTime);
        bool jsonSourceRemoveAll();

        /// randomize log data for testing purposes
        void generateRandomData(uint32_t size);

        /// save log data to the file
        bool saveFile(const char* fileName);
        /// get log save operation status
        LogThreadStatus getSaveOpStatus() const;

    private:
        /// serialize part of the log data in JSON format
        bool createJsonData(const char* fileName, time_t startTime, int32_t expireTime, uint32_t offset, uint32_t size) const;
        /// serialize real time log data in JSON format
        bool createJsonDataRealTime(const char* fileName, uint32_t size) const;
        /// serialize static log data in JSON format
        bool createJsonDataStatic(const char* fileName, time_t startTime, time_t endTime, int32_t expireTime) const;

        /// save all JSON data
        bool saveJsonData() const;
        /// remove unused JSON data
        void removeUnusedJsonData(double timeDelta);
        /// remove JSON file(s) associated with the data source
        void removeJsonFiles(const JsonDataSource& source);

        /// create log header (CSV)
        static uint32_t createLogHeaderCSV(char* buffer, uint32_t bufferSize);
        /// create log entry (CSV)
        static uint32_t createLogEntryCSV(char* buffer, uint32_t bufferSize, time_t entryTime, const JitterLogSample& sample);

    public:
        /// save log data in CSV format
        static bool saveLogCSV(FILE* f, time_t startTime, const LogData& data);
};

#endif // JITTER_LOG_H
