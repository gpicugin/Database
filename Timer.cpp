
#include "Timer.h"
//#include "PlatformInc.h"

/// system specific includes and pragmas
#ifdef OS_WINDOWS
    //#include <mmsystem.h>     
    #include <windows.h>    
    #include <profileapi.h>
    //#include <timeapi.h>
    /*#pragma comment(lib, "winmm.lib")*/
#elif defined OS_LINUX
    #include <sys/time.h>
    #include <time.h>
#endif


/// timer resolution
static const uint64_t msecPerSecond = 1000;       // milliseconds
static const uint64_t nsecPerSecond = 1000000000; // nanoseconds


/**
    Timer
*/
Timer::Timer(bool autoStart)
    : m_period( 0 )
    , m_lastTimeStamp( 0 )
    , m_startTimeStamp( 0 )
    , m_highResolution( true )
    , m_running( false )
{
    uint64_t frequency;

#ifdef OS_WINDOWS
    m_highResolution = QueryPerformanceFrequency((LARGE_INTEGER*)&frequency) ? true : false;
    // if high precision timer isn't supported
    // use multimedia timer with resolution = 1 msec
    if (!m_highResolution)
        frequency = msecPerSecond;
   /* if (true)
    {
        frequency = 1;
        m_highResolution = false;
    }*/
#else
    // resolution = 1 nanosecond
    frequency = nsecPerSecond;
#endif

    m_period = 1.0 / frequency;
    m_lastTimeStamp = getTimeStamp();

    if (autoStart)
        start();
}
Timer::~Timer()
{
}

/// operations
void Timer::start()
{
    m_running = true;
    m_startTimeStamp = getTimeStamp();
    m_lastTimeStamp = m_startTimeStamp;
}
double Timer::stop()
{
    if (!m_running)
        return 0.0;

    m_running = false;
    return getDistance( m_startTimeStamp, getTimeStamp() );
}
void Timer::reset()
{
    m_lastTimeStamp = getTimeStamp();
    if (m_running)
        m_startTimeStamp = m_lastTimeStamp;
}

double Timer::getElapsed()
{
    if (!m_running)
        return 0.0;

    uint64_t currTime = getTimeStamp();
    double result = getDistance(m_lastTimeStamp, currTime);

    m_lastTimeStamp = currTime;
    return result;
}

double Timer::getRunningTime() const
{
    if (!m_running)
        return 0.0;

    return getDistance( m_startTimeStamp, getTimeStamp() );
}

/// status
bool Timer::isRunning() const
{
    return m_running;
}
bool Timer::isHighPrecision() const
{
    return m_highResolution;
}

/// helpers
uint64_t Timer::getTimeStamp() const
{
    uint64_t value = 0;

#ifdef OS_WINDOWS
    QueryPerformanceCounter((LARGE_INTEGER*)&value);
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    value = ts.tv_sec * nsecPerSecond + ts.tv_nsec;
#endif

    return value;
}
double Timer::getDistance(uint64_t start, uint64_t stop) const
{
    uint64_t delta = stop - start;
    return (m_period * delta);
}
