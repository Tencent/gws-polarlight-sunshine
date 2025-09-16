#include "perf_counter.h"
#include "metrics.h"

#define AURORALIB "AuroraLib.dll"

struct WatchPCounterInfo {
  int setIndex;
  int counterIndex;
  unsigned long long value;
};

static void
DataCollectProc(void *collectList, int itemCount) {
  auto *pList = (WatchPCounterInfo *) collectList;
  for (int i = 0; i < itemCount; i++) {
    auto &it = pList[i];
    auto val = MetricsManager::I().query_metrics(it.setIndex, it.counterIndex);
    it.value = val;
  }
}

bool
PerfCounterManager::Start() {
#ifdef _WIN32
  HMODULE hModule = LoadLibraryEx(AURORALIB, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (hModule == nullptr) {
    return false;
  }
  m_startPerformanceCounter =
    (STARTPERFORMANCECOUNTER) GetProcAddress(hModule, "StartPerformanceCounter");
  m_stopPerformanceCounter =
    (STOPPERFORMANCECOUNTER) GetProcAddress(hModule, "StopPerformanceCounter");

  if (m_startPerformanceCounter == nullptr || m_stopPerformanceCounter == nullptr ) {
    FreeLibrary(hModule);
    return false;
  }
  m_hModule = hModule;
  m_startPerformanceCounter((void *) DataCollectProc);
#endif
  return true;
}

void
PerfCounterManager::Stop() {
#ifdef _WIN32
  if (m_hModule) {
    if (m_stopPerformanceCounter) {
      m_stopPerformanceCounter();
  }
    FreeLibrary(m_hModule);
    m_hModule = nullptr;
  }
#endif
}
