#pragma once

#include <string>
#include <vector>
#ifdef _WIN32
  #include <windows.h>
#endif

class PerfCounterManager
{
  typedef bool (*STARTPERFORMANCECOUNTER)(void *callbackFunc);
  typedef void (*STOPPERFORMANCECOUNTER)();
#ifdef _WIN32
  HMODULE m_hModule = nullptr;
#endif
  STARTPERFORMANCECOUNTER m_startPerformanceCounter = nullptr;
  STOPPERFORMANCECOUNTER m_stopPerformanceCounter = nullptr;

  void
  Stop();
  public:
  PerfCounterManager()
  {

  };
  ~PerfCounterManager()
  {
    Stop();
  }
  bool
  Start();
};