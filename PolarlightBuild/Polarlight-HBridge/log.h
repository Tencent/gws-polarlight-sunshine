#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "Locker.h"

std::string getCurrentDateString();

namespace Polarlight {
    class Log {
        Locker m_lock;

#define LOG_OUT(v)                \
    if (m_toFile) {               \
        CheckLogFileDate();       \
        m_file_log << v;          \
    }                             \
    if (m_toStdOut) {             \
        std::cout << v;           \
    }

#define LOG_FLUSH()               \
    if (m_toFile) m_file_log.flush();

    public:
        Log();
        ~Log();

        bool Init();
        template <typename T>
        inline Log& operator<<(const T& v) {
            if (m_level <= m_dumpLevel) {
                LOG_OUT(v);
                LOG_FLUSH();
            }
            return *this;
        }
        inline void operator<<(Locker* l) {
            if (m_level <= m_dumpLevel) {
                LOG_OUT('\n');
            }
            l->Unlock();
        }

        inline void SetLogFileName(std::string& strFileName) {
            m_logFileName = strFileName;
        }
        Log& SetCurInfo(const char* fileName, const int line, const int level);
        inline Locker* LineEnd() {
            return &m_lock;
        }
        inline Locker* End() {
            return &m_lock;
        }
        inline void LineBegin() {
            m_lock.Lock();
        }

        inline void SetDumpLevel(int l) {
            m_dumpLevel = l;
        }
        inline void SetLevel(int l) {
            m_level = l;
        }

    private:
        void CheckLogFileDate();
        bool m_toFile = true;
        bool m_toStdOut = true;
        std::string m_logFileName;
        std::ofstream m_file_log;
        std::string m_currentDate = getCurrentDateString();

        int m_level = 0;
        int m_dumpLevel = 999999; // All levels will be dumped out
    };

    extern Log log;

#define SetLogLevel(l) Polarlight::log.SetDumpLevel(l)
#define LOGV(level) Polarlight::log.SetCurInfo(__FILE__, __LINE__, level)
#define LOG LOGV(0)
#define LOG1 LOGV(1)
#define LOG2 LOGV(2)
#define LOG3 LOGV(3)
#define LOG4 LOGV(4)
#define LOG5 LOGV(5)
#define LOG6 LOGV(6)
#define LOG7 LOGV(7)
#define LOG8 LOGV(8)
#define LOG9 LOGV(9)
#define LINE_END Polarlight::log.LineEnd()
#define LOG_END Polarlight::log.End()

} // namespace Polarlight
