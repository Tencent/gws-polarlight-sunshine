#include "log.h"
#include <chrono>
#include <ctime>
#include <iomanip>

unsigned long GetPID()
{
    unsigned long processId = 0;
#if (WIN32)
    processId = GetCurrentProcessId();
#else
    processId = getpid();
#endif
    return processId;
}
unsigned long GetThreadID()
{
    unsigned long tid = 0;
#if (WIN32)
    tid = ::GetCurrentThreadId();
#else
#include <sys/types.h>
#include <unistd.h>
    tid = gettid();
#endif
    return tid;
}

std::string getCurrentDateString() {
    std::string sDate;
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    if (now_tm) {
        char szDate[16] = {};
        snprintf(szDate, sizeof(szDate), "%04d%02d%02d", now_tm->tm_year + 1900, now_tm->tm_mon + 1, now_tm->tm_mday);
        sDate = std::string(szDate);
    }
    return sDate;
}

std::string getCurrentTimeString()
{
    auto current_time = std::chrono::system_clock::now();
    auto duration = current_time.time_since_epoch();
    long long current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    // Convert milliseconds to time_t (seconds)
    std::time_t time_in_seconds = current_time_ms / 1000;

    // Convert time_t to tm struct using localtime_s
    std::tm time_info;
    localtime_s(&time_info, &time_in_seconds);

    // Format the time as MMDDHHMMSSmmm
    std::stringstream ss;
    ss << std::put_time(&time_info, "%m%d%H%M%S.");
    ss << std::setw(3) << std::setfill('0') << (current_time_ms % 1000);

    return ss.str();
}

Polarlight::Log Polarlight::log;

Polarlight::Log::Log() {}

Polarlight::Log::~Log() {
    m_file_log.close();
}

bool Polarlight::Log::Init() {
    m_file_log.open(m_logFileName, std::ios_base::out | std::ios_base::app);
    return true;
}

void Polarlight::Log::CheckLogFileDate() {
    if (getCurrentDateString() != m_currentDate && m_file_log.is_open()) {
        m_file_log.close();
        m_currentDate = getCurrentDateString();
        std::string archiveLogName = m_logFileName.substr(0, m_logFileName.size() - 4) + "_" + m_currentDate + ".log";
        std::rename(m_logFileName.c_str(), archiveLogName.c_str());
        m_file_log.open(m_logFileName, std::ios_base::out | std::ios_base::app);
    }
}

Polarlight::Log& Polarlight::Log::SetCurInfo(const char* fileName, const int line, const int level) {
    m_lock.Lock();
    m_level = level;
    if (m_level <= m_dumpLevel) {
        std::string strFileName(fileName);
        auto pos = strFileName.rfind("\\");
        if (pos != std::string::npos) {
            strFileName = strFileName.substr(pos + 1);
        }
        unsigned long pid = GetPID();
        unsigned long tid = GetThreadID();
        auto curTime = getCurrentTimeString();
        const int buf_Len = 1000;
        char szFilter[buf_Len];
        sprintf_s(szFilter, buf_Len, "[%d-%d-%s,%s:%d] ", pid, tid, curTime.c_str(), strFileName.c_str(), line);
        LOG_OUT(szFilter);
    }
    return *this;
}
