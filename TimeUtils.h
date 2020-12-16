
#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include "Platform.h"
#include <vector>
#include <memory>
#include <linux/rtc.h>


/// typedefs
typedef struct timespec Timestamp;

/**
    TimerFlags
*/
enum TimerFlags
{
    TIMER_SINGLESHOT = 0x01,
    TIMER_RESTART    = 0x02
};

/**
    ITimer: base abstract class for timers
*/
class ITimer
{
    public:
        /// start/stop
        virtual void    start(double expireTime) = 0;
        virtual void    stop() = 0;
        /// check status
        virtual bool    isRunning() const = 0;
        virtual double  getTimeToExpire() = 0;
};

/**
    Timer: simple timer for events processing
*/
class Timer: public ITimer
{
    public:
        static const double expireTimeNever;

    private:
        double          m_expireTime;   // seconds
        double          m_currTime;
        Timestamp       m_startTime;
        bool            m_running;

    public:
        /// constructor/destructor
        Timer();
        virtual ~Timer();

        /// start/stop
        virtual void    start(double expireTime = expireTimeNever);
        virtual void    stop();
        void            restart();

        /// check status
        virtual bool    isRunning() const;
        virtual double  getTimeToExpire();
        bool            isExpired();
        bool            isTimeExpired(double timeSec = 0);
        double          getTimeElapsed();

    private:
        double          getCurrentTime() const;
};

/**
    PeriodicTimer: periodic processing (fixed period)
*/
class PeriodicTimer: public ITimer
{
    private:
        Timer           m_timer;
        double          m_periodSec;    // seconds
        double          m_extraTime;    // seconds

    public:
        /// constructor/destructor
        PeriodicTimer();
        virtual ~PeriodicTimer();

        /// start/stop
        virtual void    start(double expireTime);
        virtual void    stop();

        /// check status
        virtual bool    isRunning() const;
        virtual double  getTimeToExpire();
        bool            isTriggered(double* elapsedTime = NULL);
};

/// Timer callback function
typedef void (*TimerCallback)(uint32_t timerId, void* param);

/**
    TimeManager: manages the list of timers
*/
class TimeManager
{
    private:
        struct TimerInfo
        {
            uint32_t        id;
            uint32_t        flags;
            double          period;
            PeriodicTimer   timer;
            TimerCallback   cb;
            void*           param;
        };

        typedef std::shared_ptr<TimerInfo>  TimerInfoPtr;
        typedef std::vector<TimerInfoPtr>   TimerList;
        TimerList       m_timers;
        bool            m_running;

    public:
        /// constructor/destructor
        TimeManager();
        ~TimeManager();

        /// start/stop timers
        void            start();
        void            stop();

        /// add/remove timers
        bool            addTimer(uint32_t id, double period, TimerCallback cb, void* userParam, uint32_t flags = TIMER_RESTART);
        bool            removeTimer(uint32_t id);
        void            removeAll();

        /// get time to next event
        double          getTimeToNextEvent();
        uint32_t        getTimeToNextEventMs();

        /// process events
        void            processEvents();

    private:
        /// find timer by ID
        TimerInfoPtr    findTimer(uint32_t id) const;
};


#endif // TIME_UTILS_H
