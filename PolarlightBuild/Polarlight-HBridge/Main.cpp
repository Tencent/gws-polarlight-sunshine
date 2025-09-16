// Main.cpp : Defines the entry point for the HBridge.
//
#include <signal.h>
#include <vector>
#include <string>
#include <cstring>
#include "xload.h"
#include "xlang.h"
#include "xpackage.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include "HaloType.h"
#include "Halo.h"
#include "HaloAPI.h"
#include "log.h"

#if (WIN32)
#include <Windows.h>
#include <wtsapi32.h>
#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "wtsapi32.lib")
#endif

#if (WIN32)
#define Path_Sep_S "\\"
#define Path_Sep '\\'
#else
#include <string.h> //for memcpy
#define Path_Sep_S "/"
#define Path_Sep '/'
#endif


struct ParamConfig
{
    X::Config config;
    bool print_usage = false;//-help |-? |-h
};

X::XLoad g_xLoad;
void signal_callback_handler(int signum)
{
    X::AppEventCode code = g_xLoad.HandleAppEvent(signum);
    if (code == X::AppEventCode::Exit)
    {
        exit(signum);
    }
    signal(SIGINT, signal_callback_handler);
}

void PrintUsage()
{
    std::cout <<
        "halo [-dbg]\n\
      [file parameters]" << std::endl;
    std::cout << "halo -help | -? | -h for help" << std::endl;
}
bool ParseCommandLine(std::vector<std::string>& params, ParamConfig& paramCfg)
{
    //first one is exe file name with path
    std::string progName = params[0];
    std::string strAppPath;
    auto pos = progName.rfind(Path_Sep);
    if (pos != progName.npos)
    {
        strAppPath = progName.substr(0, pos);
    }
#if (WIN32)
    else
    {
        char buffer[MAX_PATH];
        DWORD length = GetModuleFileName(nullptr, buffer, MAX_PATH);
        progName = buffer;
        auto pos = progName.rfind(Path_Sep);
        if (pos != progName.npos)
        {
            strAppPath = progName.substr(0, pos);
        }
        else
        {
            strAppPath = "";
        }
    }
#endif
    paramCfg.config.appPath = new char[strAppPath.length() + 1];
    memcpy((char*)paramCfg.config.appPath, strAppPath.data(), strAppPath.length() + 1);
    paramCfg.config.appFullName = new char[progName.length() + 1];
    memcpy((char*)paramCfg.config.appFullName, progName.data(), progName.length() + 1);

    if (params.size() == 1)
    {
        //paramCfg.print_usage = true;
        return true;
    }
    std::string strPassInParams;
    int i = 1;
    bool starFile = false;
    while (i < (int)params.size())
    {
        std::string& s = params[i];
        if (s.size() == 0)
        {
            continue;
        }
        if (!starFile && s[0] == '-')
        {
            if (s == "-help" || s == "-?" || s == "-h" || s == "-H")
            {
                paramCfg.print_usage = true;
                i++;
            }
            else if (s == "-dbg")
            {
                paramCfg.config.dbg = true;
                i++;
            }
        }
        else if (!starFile)
        {
            starFile = true;
            //first one is file name
            paramCfg.config.fileName = new char[s.length() + 1];
            memcpy((char*)paramCfg.config.fileName, s.data(), s.length() + 1);
            i++;
        }
        else
        {//parse passIn Params after file name
            if (strPassInParams.empty())
            {
                strPassInParams = s;
            }
            else
            {
                strPassInParams += "\n" + s;
            }
            i++;
        }
    }
    if (!strPassInParams.empty())
    {
        paramCfg.config.passInParams = new char[strPassInParams.length() + 1];
        memcpy((char*)paramCfg.config.passInParams, strPassInParams.data(), strPassInParams.length() + 1);
    }
    return true;
}

void log(std::string text) {
    Polarlight::log.SetCurInfo("HBridge", 147, 0);
    Polarlight::log.SetLevel(0);
    Polarlight::log << text;
    Polarlight::log << LINE_END;
}

int main(int argc, char* argv[])
{
    bool haloIsOK = Polarlight::Halo::loadHalo();
    if (haloIsOK)
    {
        Polarlight::Halo::startHalo();
    }

    std::vector<std::string> params(argv, argv + argc);
    ParamConfig paramConfig;

    ParseCommandLine(params, paramConfig);
    if (paramConfig.print_usage)
    {
        PrintUsage();
        return 0;
    }
    std::string appPath(paramConfig.config.appPath);
    std::string logFolder = appPath + "\\log";
    std::string logFilePath = logFolder + Path_Sep + "HBridge.log";
    Polarlight::log.SetLogFileName(logFilePath);
    Polarlight::log.Init();

    log("/********** Hbridge start **********/");

    HBridge::HaloAPI::I().Init();

    const int lrpc_port = 31417;

    signal(SIGINT, signal_callback_handler);
    int retCode = g_xLoad.Load(&paramConfig.config);
    if (retCode != 0)
    {
        return retCode;
    }
    g_xLoad.Run();
    X::RegisterPackage<HBridge::HaloAPI>(
        (char*)paramConfig.config.appFullName,
        "halo", &HBridge::HaloAPI::I());
    X::g_pXHost->Lrpc_Listen(lrpc_port, true);

    Polarlight::Halo::stopHalo();
    Polarlight::Halo::unloadHalo();

    g_xLoad.Unload();
    return 0;
}
