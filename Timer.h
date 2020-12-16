
#ifndef TIMER_H
#define TIMER_H

#include "Platform.h"

/**
    Timer
    High precision system timer
*/
class Timer
{
    private:
        double  m_period;            // seconds per count
        uint64_t  m_lastTimeStamp;     // last time stamp
        uint64_t  m_startTimeStamp;    // start time stamp
        bool    m_highResolution;    // is timer high-resolution
        bool    m_running;           // is timer running

    public:
        explicit Timer(bool autoStart = false);
        ~Timer();

        /// operations
        void    start();
        double  stop();
        void    reset();
        double  getElapsed();
        double  getRunningTime() const;

        /// status
        bool    isRunning() const;
        bool    isHighPrecision() const;

    private:
        /// helpers
        uint64_t  getTimeStamp() const;
        double  getDistance(uint64_t start, uint64_t stop) const;
};

#endif // TIMER_H
