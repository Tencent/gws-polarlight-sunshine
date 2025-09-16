#include "xhost.h"
#include "xpackage.h"
#include "Pbridge.h"

#include "xload.h"
#include "xpackage.h"
#include "xlang.h"

#include <iostream>

#if (WIN32)
#include <windows.h>
#define PB_EXPORT __declspec(dllexport) 
#else
#define PB_EXPORT
#endif

X::Value g_HostObject; //use for call from sunshine to host to set sunshine status

class SunshineAPI
{
    SunshineCallTable* mHostApis = nullptr;
public:
    BEGIN_PACKAGE(SunshineAPI)
        APISET().AddEvent("OnClientConnect");
        APISET().AddEvent("OnClientDisconnect");
        APISET().AddEvent("OnDummyDisplay");
        APISET().AddFunc<2>("SetWordBooks", &SunshineAPI::SetWordbooks);
        APISET().AddFunc<1>("QueryWordBooks", &SunshineAPI::QueryWordbooks);
        APISET().AddFunc<1>("SetDisplayList", &SunshineAPI::SetDisplayList);
        APISET().AddFunc<3>("OnClientAction", &SunshineAPI::OnClientAction);
        APISET().AddFunc<1>("StopVideoStreaming", &SunshineAPI::StopVideoStreaming);
        APISET().AddFunc<1>("ValidateDisplay", &SunshineAPI::ValidateDisplay);
        APISET().AddFunc<1>("OnQueryRuntimeInfo", &SunshineAPI::OnQueryRuntimeInfo);
        APISET().AddFunc<1>("OnQueryMetrics", &SunshineAPI::OnQueryMetrics);
        APISET().AddFunc<1>("SetHostObject", &SunshineAPI::SetHostObject);
    END_PACKAGE
    void SetHostApis(SunshineCallTable* hostApis)
    {
        mHostApis = hostApis;
    }
    SunshineAPI()
    {
    }
    virtual ~SunshineAPI()
    {
    }
    void SetHostObject(X::Value hostObj)
    {
        g_HostObject = hostObj;
    }
    bool StopVideoStreaming(int streamId)
    {
        bool bOK = false;
        if (mHostApis)
        {
            bOK = mHostApis->StopVideoStreaming(streamId);
        }
        return bOK;
    }
    bool SetDisplayList(std::string displayList)
    {
        bool bOK = false;
        if (mHostApis)
        {
            bOK = mHostApis->SetDisplayList(displayList.c_str());
        }
        return bOK;
    }
    bool SetWordbooks(std::string key, std::string value)
    {
        bool bOK = false;
        if (mHostApis)
        {
            bOK = mHostApis->SetWordbooks(key.c_str(), (char*)value.c_str());
        }
        return bOK;
    }
    std::string QueryWordbooks(std::string key)
    {
        char buf[8192];
        int lenMax = 8192;
        int len = 0;
        std::string val;
        if (mHostApis)
        {
            len = mHostApis->QueryWordbooks(key.c_str(), buf, lenMax);
        }
        return std::string(buf, len);
    }
    int OnClientAction(std::string name, std::string jsonArgs, std::string aesKey) {
    if (mHostApis)
        {
            return mHostApis->OnClientAction(name.c_str(), jsonArgs.c_str(), aesKey.c_str());
        }
        return 500;
    }
    bool ValidateDisplay(std::string displayName)
    {
        bool bOK = false;
        if (mHostApis)
        {
            bOK = mHostApis->ValidateDisplay(displayName.c_str());
        }
        return bOK;
    }
    int OnQueryRuntimeInfo(std::string name) {
        if (mHostApis)
        {
            return mHostApis->OnQueryRuntimeInfo(name.c_str());
        }
        return 0;
    }

    double OnQueryMetrics(std::string name) {
        if (mHostApis)
        {
            return mHostApis->OnQueryMetrics(name.c_str());
        }
        return 0;
    }
};

X::XLoad g_xload;
X::Config g_config;
SunshineAPI g_api;

#define LRPC_PORT 31415

static bool GetCurLibInfo(void* EntryFuncName, std::string& strFullPath,
    std::string& strFolderPath, std::string& strLibName)
{
#if (WIN32)
    HMODULE  hModule = NULL;
    GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        (LPCTSTR)EntryFuncName,
        &hModule);
    char path[MAX_PATH];
    GetModuleFileName(hModule, path, MAX_PATH);
    std::string strPath(path);
    strFullPath = strPath;
    auto pos = strPath.rfind("\\");
    if (pos != std::string::npos)
    {
        strFolderPath = strPath.substr(0, pos);
        strLibName = strPath.substr(pos + 1);
    }
#endif
    //remove ext
    pos = strLibName.rfind(".");
    if (pos != std::string::npos)
    {
        strLibName = strLibName.substr(0, pos);
    }
    return true;
}

extern "C"  PB_EXPORT int Load(void* hostCallTable,const char* hostPath)
{
    SunshineCallTable* pTab = (SunshineCallTable*)hostCallTable;
    //fill out client APIs
    pTab->OnClientStatusChanged =(f_OnClientStatusChanged)([](
        const char* clientId, int status)
    {
            std::string strClientId(clientId);
            X::ARGS args(1);
            args.push_back(strClientId);
            X::KWARGS kwargs;
            SunshineStatus s = (SunshineStatus)status;
            switch (s)
            {
            case ClientStatus_Connect:
                g_api.Fire(0, args, kwargs); // OnClientConnect
                break;
            case ClientStatus_Disconnect:
                g_api.Fire(1, args, kwargs); // OnClientDisconnect
                break;
            default:
                break;
            }
    });
    pTab->OnSessionStatusChanged = (f_OnSessionStatusChanged)([](
        int status, const char* statusInfo)
        {
            std::string strInfo(statusInfo);
            if (g_HostObject.IsObject())
            {
                g_HostObject["OnSunshineStatusChanged"](status, strInfo);
            }
        });
    pTab->OnDummyDisplay = (f_OnDummyDisplay)([](
        int displayId, bool isDummy, const char* displayName)
        {
            std::string strDisplayName(displayName);
            X::ARGS args(3);
            args.push_back(displayId);
            args.push_back(isDummy);
            args.push_back(strDisplayName);
            X::KWARGS kwargs;
            g_api.Fire(2, args, kwargs);
        });

    g_api.SetHostApis(pTab);

    std::string strFullPath;
    std::string strFolderPath;
    std::string strLibName;
    GetCurLibInfo((void*)Load, strFullPath, strFolderPath, strLibName);

    g_config.appPath = new char[strFolderPath.length() + 1];
    memcpy((char*)g_config.appPath, strFolderPath.data(), strFolderPath.length() + 1);

    g_config.dbg = false;
    g_config.runEventLoopInThread = true;
    g_config.enterEventLoop = true;
    int retCode = g_xload.Load(&g_config);
    std::cout << "XLang RetCode: " << retCode << std::endl;
    if (retCode == 0) 
    {
        g_xload.Run();
        X::RegisterPackage<SunshineAPI>(hostPath, "sunshine", &g_api);
        X::g_pXHost->Lrpc_Listen(LRPC_PORT, false);
    }
    return retCode;
}
extern "C"  PB_EXPORT void Unload()
{
    g_api.SetHostApis(nullptr);
    g_xload.Unload();
}