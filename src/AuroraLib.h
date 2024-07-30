#pragma once

#include <vector>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#include <iostream>

class AuroraManager
{
  typedef void *(*CREATEDISPLAYINSTANCE)(int id);
  typedef void *(*CREATEDISPLAYINSTANCEWAIT)(int id);
  typedef void (*CLOSEDISPLAYINSTANCE)(void *pInstance);

  typedef bool (*STARTPERFORMANCECOUNTER)(void *callbackFunc);
  typedef void (*STOPPERFORMANCECOUNTER)();

  typedef int(*ENUMDISPLAYOUTPUTS)(char *outputBuf, int bufSize);
  typedef long (*AURORACHANGEDISPLAYSETTINGS)(char* deviceName, char* settings);

  //typedef long (*AURORAQUERYDISPLAYCONFIG)(char* deviceName, char* settings);
  //typedef long (*AURORASETDISPLAYCONFIG)(char* deviceName, char* settings);
  typedef long (*AURORASAVEDISPLAYCONFIGURATION)(char* fileName);
  typedef long (*AURORALOADDISPLAYCONFIGURATION)(char* fileName);
  typedef long (*AURORAGETPRIMARYDISPLAY)(char* deviceName);
  typedef long (*AURORASETPRIMARYDISPLAY)(char* deviceName);
  typedef int(*ENUMDISPLAYNAMES)(std::vector<std::string>& deviceNames);
  typedef int (*AURORALOADPRIVACY)(int virtualDisplayCount, std::string& physical_display_data_file, char* inName1, char* inName2, char* outName1, char* outName2);
  typedef int (*AURORASWITCHDISPLAYS)(int numDisp, bool toVirtual, char* bufDisplanyNames, int& bufLen);
  typedef void (*AURORACLOSEDISPLAYS)();
  typedef bool (*STARTSCANTHREAD)();
  typedef bool (*STOPSCANTHREAD)();
  typedef void (*SETLOGWRITEFUNC)(void* callbackFunc);

#ifdef _WIN32
  HMODULE m_hModule = nullptr;
#endif
  CREATEDISPLAYINSTANCE m_createInstanceFunc = nullptr;
  CREATEDISPLAYINSTANCEWAIT m_createInstanceWaitFunc = nullptr;
  CLOSEDISPLAYINSTANCE m_closeInstanceFunc = nullptr;
  STARTPERFORMANCECOUNTER m_startPerformanceCounter = nullptr;
  STOPPERFORMANCECOUNTER m_stopPerformanceCounter = nullptr;
  ENUMDISPLAYOUTPUTS m_enumDisplayOutputs = nullptr; 
  AURORACHANGEDISPLAYSETTINGS m_changeDisplaySettings = nullptr;

  //AURORAQUERYDISPLAYCONFIG m_queryDisplayConfig = nullptr;
  //AURORASETDISPLAYCONFIG m_setDisplayConfig = nullptr;
  AURORASAVEDISPLAYCONFIGURATION m_saveDisplayConfig = nullptr;
  AURORALOADDISPLAYCONFIGURATION m_loadDisplayConfig = nullptr;
  AURORASETPRIMARYDISPLAY m_setPrimaryDisplay = nullptr;
  AURORAGETPRIMARYDISPLAY m_getPrimaryDisplay = nullptr;
  AURORALOADPRIVACY m_PrivacyDisaplay = nullptr;
  AURORASWITCHDISPLAYS m_switchDisplays = nullptr;
  AURORACLOSEDISPLAYS m_closeAllDisplays = nullptr;
  STARTSCANTHREAD m_startScanThread = nullptr;
  STOPSCANTHREAD m_stopScanThread = nullptr;
  SETLOGWRITEFUNC m_setLogWriteFunc= nullptr;

  std::vector<void *> m_displayList;
  int m_listDisplayId = 10;

public:
  int GetVirtualDisplayNum() { return (int)m_displayList.size();}
  bool
  CreateDisplay(){
    bool bOK = false;
    if (m_createInstanceFunc) {
      void *pDisplayContext = m_createInstanceFunc(m_listDisplayId++);
        if(pDisplayContext!=nullptr) {
          bOK =true;
          m_displayList.push_back(pDisplayContext);
        }
    }
    return bOK;
  }
  bool
  CreateDisplayWait(){
    bool bOK = false;
    if (m_createInstanceWaitFunc) {
      void *pDisplayContext = m_createInstanceWaitFunc(m_listDisplayId++);
        if(pDisplayContext!=nullptr) {
          bOK =true;
          m_displayList.push_back(pDisplayContext);
        }
    }
    return bOK;
  }
  bool RemoveDisplay() {
    if(m_displayList.size()>0){
      void* context = *(m_displayList.end()-1);
      if(m_closeInstanceFunc) {
        m_closeInstanceFunc(context);
        m_displayList.erase(m_displayList.end()-1);
      }
    }
    return true;
  }
  int EnumDisplayOutputs(std::string& strOutputs,bool NumberOnly = false){
      if (m_enumDisplayOutputs) {
        int buf_size = 0;
        char* pBuf =nullptr;
        if(!NumberOnly) {
          buf_size = 10 * 1024;
          strOutputs.resize(buf_size);
          pBuf = strOutputs.data();
        }
        return m_enumDisplayOutputs(pBuf,buf_size);
    }
    else
    {
      return -1;
    }
  }

  int EnumDisplayNames(std::vector<std::string>& displayNames){

    if (m_enumDisplayOutputs) {
        int buf_size = 10 * 1024;
        char* pBuf = nullptr;
        std::string strOutputs;
        strOutputs.resize(buf_size);
        //char* pBuf = strOutputs.data();
        pBuf = strOutputs.data();
        int numDisplays = m_enumDisplayOutputs(pBuf,buf_size);
        //std::string line[] = strOutputs.string_split
        //std::string token = s.substr(0, s.find(delimiter));
        size_t pos = 0;
        size_t pos1 = 0;
        std::string line;
        std::string displayName;
        while ((pos = strOutputs.find("\n")) != std::string::npos) {
            line = strOutputs.substr(0, pos);
            strOutputs.erase(0, pos + 1);
            if ((pos1 = line.find(" -- "))!= std::string::npos) {
              displayName = line.substr(0, pos1);
              displayNames.push_back(displayName);
            }
        }  
        return numDisplays;      
    }
    else
    {
      return -1;
    }
  }

  long disableDisplay(char *deviceName) {
    if(m_changeDisplaySettings) {
	  return m_changeDisplaySettings(deviceName, nullptr);
	}
	else
    {
      return -1;
    }		
  }
  
  int enableDisplay(char* deviceName, char* settings) 
  {
    if(m_changeDisplaySettings) 
    {
	    return m_changeDisplaySettings(deviceName, settings);
	  }
	  else
    {
      return -1;
    }		
  }

  int loadDisplayConfig(char *deviceName) 
  {
    m_closeAllDisplays();
    if(m_loadDisplayConfig) 
    {
	    return m_loadDisplayConfig(deviceName);
	  }
	  else
    {
      return -1;
    }		
  }
  
  int saveDisplayConfig(char *deviceName) 
  {

    if(m_saveDisplayConfig) 
    {
	    return m_saveDisplayConfig(deviceName);
	  }
	  else
    {
      return -1;
    }		
  }

  int getPrimaryDisplay(char *deviceName) {
    if(m_getPrimaryDisplay) {
	  return m_getPrimaryDisplay(deviceName);
	}
	else
    {
      return -1;
    }		
  }

  int setPrimaryDisplay(char *deviceName) {
    if(m_setPrimaryDisplay) {
	  return m_setPrimaryDisplay(deviceName);
	}
	else
    {
      return -1;
    }		
  }

  int SetPrivacy(int virtualDisplayCount, char* outBuf, int bufLen, bool toVirtual = true);
 
  bool Start();
  void Stop();

  typedef void (*LOG_WRITE)(const char* szLog);
  void SetLogWriteFunPtr(void* pfun)
  {
      if (m_setLogWriteFunc)
      {
          m_setLogWriteFunc(pfun);
      }
  }
  bool StartScanThread()
  {
      if (m_startScanThread)
      {
          m_startScanThread();
          std::cout << "startScanThread()" << std::endl;
          return true;
      }
      return false;
  }

  bool StopScanThread()
  {
      if (m_stopScanThread)
      {
          m_stopScanThread();
          return true;
      }
      return false;
  }

void OutClean() 
{
      for (auto* display : m_displayList) {
          if (m_closeInstanceFunc) {
              m_closeInstanceFunc(display);
          }
      }
      m_displayList.clear();
  }
};

extern AuroraManager& AuroraMgr();