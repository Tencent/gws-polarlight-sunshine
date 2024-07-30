#include "AuroraLib.h"
#include "metrics.h"

#define AURORALIB "AuroraLib.dll"

static MetricsManager __MetricsManager;
MetricsManager& MetricsMgr() {
  return __MetricsManager;
}

static AuroraManager __AuroraManager;
AuroraManager& AuroraMgr() {
  return __AuroraManager;
}

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
    auto val = MetricsMgr().query_metrics(it.setIndex, it.counterIndex);
    it.value = val;
  }
}

 bool
AuroraManager::Start() {
#ifdef _WIN32
  HMODULE hModule = LoadLibraryEx(AURORALIB, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (hModule == nullptr) 
  {
    return false;
  }
  m_createInstanceFunc =
    (CREATEDISPLAYINSTANCE) GetProcAddress(hModule, "CreateDisplayInstance");
  m_createInstanceWaitFunc =
    (CREATEDISPLAYINSTANCEWAIT) GetProcAddress(hModule, "CreateDisplayWaited");  
  m_closeInstanceFunc =
    (CLOSEDISPLAYINSTANCE) GetProcAddress(hModule, "CloseDisplayInstance");
  m_startPerformanceCounter =
    (STARTPERFORMANCECOUNTER) GetProcAddress(hModule, "StartPerformanceCounter");
  m_stopPerformanceCounter =
    (STOPPERFORMANCECOUNTER) GetProcAddress(hModule, "StopPerformanceCounter");
  m_enumDisplayOutputs = 
    (ENUMDISPLAYOUTPUTS) GetProcAddress(hModule, "EnumDisplayOutputs");
  m_changeDisplaySettings  =
    (AURORACHANGEDISPLAYSETTINGS) GetProcAddress(hModule, "AuroraChangeDisplaySettings");
  m_saveDisplayConfig =
      (AURORASAVEDISPLAYCONFIGURATION)GetProcAddress(hModule, "AuroraSaveDisplayConfiguration");
  m_loadDisplayConfig =
      (AURORALOADDISPLAYCONFIGURATION)GetProcAddress(hModule, "AuroraLoadDisplayConfiguration");
  m_setPrimaryDisplay =
      (AURORASETPRIMARYDISPLAY)GetProcAddress(hModule, "AuroraSetPrimaryDisplay");
  m_getPrimaryDisplay =
      (AURORAGETPRIMARYDISPLAY)GetProcAddress(hModule, "AuroraGetPrimaryDisplay");
  m_switchDisplays = (AURORASWITCHDISPLAYS)GetProcAddress(hModule, "AuroraSwitchDisplays");
  m_closeAllDisplays = (AURORACLOSEDISPLAYS)GetProcAddress(hModule, "AuroraCloseAllDisplays");

  m_startScanThread = (STARTSCANTHREAD)GetProcAddress(hModule, "StartScanThread");
  m_stopScanThread = (STOPSCANTHREAD)GetProcAddress(hModule, "StopScanThread");
  m_setLogWriteFunc = (SETLOGWRITEFUNC)GetProcAddress(hModule, "AuroraSetLogWriteCallBack");
     std::cout << "Start() 2" << std::endl;

  if (m_switchDisplays == nullptr 
      || m_closeAllDisplays == nullptr 
      || m_startScanThread == nullptr
      || m_stopScanThread == nullptr     
      || m_enumDisplayOutputs == nullptr
      || m_createInstanceFunc == nullptr
      || m_createInstanceWaitFunc == nullptr)
  {
    FreeLibrary(hModule);
    return false;
  }
  m_hModule = hModule;
  m_startPerformanceCounter((void*)DataCollectProc);
#endif
  return true;
}
void
AuroraManager::Stop() {
  for (auto *display : m_displayList) {
    if (m_closeInstanceFunc) {
      m_closeInstanceFunc(display);
    }
  }
  m_displayList.clear();
#ifdef _WIN32
  if (m_hModule) {
    FreeLibrary(m_hModule);
    m_hModule = nullptr;
  }
#endif
}


int AuroraManager::SetPrivacy(int virtualDisplayCount, char* outBuf, int bufLen, bool toVirtual)
{
    std::cout << "int AuroraManager::SetPrivacy() 1" << std::endl;
    if (outBuf && m_switchDisplays)
    {
        std::cout << "int AuroraManager::SetPrivacy() 2" << std::endl;
        int retVal = (int)m_switchDisplays(virtualDisplayCount, toVirtual, outBuf, bufLen);
        return retVal;
    }
    std::cout << "int AuroraManager::SetPrivacy() 3" << std::endl;
    return -100;
}
