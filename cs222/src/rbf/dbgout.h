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

    class Dbgout
    {
    public:
        Dbgout();
        ~Dbgout();

        inline void logNone() { setLogLevel(LOG_NONE); }
        inline void logSevere() { setLogLevel(LOG_SEVERE); }
        inline void logError() { setLogLevel(LOG_ERROR); }
        inline void logWarning() { setLogLevel(LOG_WARNING); }
        inline void logInfo() { setLogLevel(LOG_INFO); }
        inline void logDebug() { setLogLevel(LOG_DEBUG); }
        inline void logExtremeDebug() { setLogLevel(LOG_EXTREMEDEBUG); }

        inline void setLogLevel(LogLevel logLevel) { _logLevel = logLevel; }
        inline void setVerbosity(LogLevel verbosity) { _verbosity = verbosity; }

        inline LogLevel getLogLevel() const { return _logLevel; }
        inline LogLevel getVerbosity() const { return _verbosity; }

        inline Dbgout& operator<< (LogLevel logLevel) { setLogLevel(logLevel); return *this; }

        template<typename T>
        inline Dbgout& operator<< (const T& t)
        {
#if DEBUG_ENABLED
            if (_logLevel <= _verbosity)
            {
                _out << t;
            }
#endif
            return *this;

        }

        inline void log(const char* fmt, ...)
        {
#if DEBUG_ENABLED
            if (_logLevel <= _verbosity)
            {
                va_list args;
                va_start(args, fmt);
                vfprintf(stderr, fmt, args);
                va_end(args);
            }
#endif
        }

    private:
        std::ostream& _out;
        LogLevel _logLevel;
        LogLevel _verbosity;
    };
}

#endif // DEBUGOUTPUT_H
