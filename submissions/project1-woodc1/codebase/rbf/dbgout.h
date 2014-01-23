#ifndef DEBUGOUTPUT_H
#define DEBUGOUTPUT_H

#include <cstdarg>
#include <cstdio>
#include <iostream>

#define DEBUG_ENABLED 0
#define DEFAULT_VERBOSITY LOG_EXTREMEDEBUG

namespace dbg
{
    enum LogLevel
    {
        LOG_NONE = 0,
        LOG_SEVERE = 1,
        LOG_ERROR = 2,
        LOG_WARNING = 4,
        LOG_INFO = 8,
        LOG_DEBUG = 16,

        LOG_EXTREMEDEBUG = 128
    };

    class Dbgout;
    extern Dbgout out;

    // if DEBUG_ENABLED == 0, pretty much everything in this class should be a NOP
    class Dbgout
    {
    public:
        Dbgout();
        ~Dbgout();

        // Sets the log level for all upcoming calls to the logger
        inline void logNone(){ setLogLevel(LOG_NONE); }
        inline void logSevere() { setLogLevel(LOG_SEVERE); }
        inline void logError() { setLogLevel(LOG_ERROR); }
        inline void logWarning() { setLogLevel(LOG_WARNING); }
        inline void logInfo() { setLogLevel(LOG_INFO); }
        inline void logDebug() { setLogLevel(LOG_DEBUG); }
        inline void logExtremeDebug() { setLogLevel(LOG_EXTREMEDEBUG); }

#if DEBUG_ENABLED
        inline void setLogLevel(LogLevel logLevel)
        {
            _logLevel = logLevel;
        }

        // Sets the level of log calls we will care about (verbosity of LOG_ERROR means only LOG_ERROR and LOG_SEVERE will display)
        inline void setVerbosity(LogLevel verbosity)
        {
            _verbosity = verbosity;
        }

        inline LogLevel getLogLevel() const { return _logLevel; }
        inline LogLevel getVerbosity() const { return _verbosity; }

        inline Dbgout& operator<< (LogLevel logLevel)
        {
            setLogLevel(logLevel);
            return *this;
        }

        template<typename T>
        inline Dbgout& operator<< (const T& t)
        {
            if (_logLevel <= _verbosity)
            {
                _out << t;
            }
            return *this;

        }

        inline void log(const char* fmt, ...)
        {
            if (_logLevel <= _verbosity)
            {
                va_list args;
                va_start(args, fmt);
                vfprintf(stderr, fmt, args);
                va_end(args);
            }

        }
#else
        inline void setLogLevel(LogLevel) {}
        inline void setVerbosity(LogLevel) {}
        inline Dbgout& operator<< (LogLevel) { return *this; }
        template<typename T> inline Dbgout& operator<< (const T&) { return *this; }
        inline void log(const char*, ...) { }
#endif

    private:
#if DEBUG_ENABLED
        std::ostream& _out;
        LogLevel _logLevel;
        LogLevel _verbosity;
#endif
    };
}

#endif // DEBUGOUTPUT_H
