#include "export_apis.h"
#include "Pbridge.h"
#include "main.h"
#include "config.h"
#include "rtsp.h"
#include "process.h"
#include "metrics.h"
#include <unordered_map>
#include <regex>

#include <Simple-Web-Server/utility.hpp>
// lib includes
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#define PBridgeLib "polaris_bridge.dll"
using namespace std::literals;
namespace video
{
  extern void set_display_changed(std::vector<std::string> &output_liste);
  extern bool ValidateDisplay(const char *displayName); 
  extern void StopVideoStreaming(int streamId);
}

typedef int (*funcLoad)(void* hostCallTable,const char* hostPath);
typedef void (*funcUnload)();

SunshineCallTable g_SunshineCallTable = {0};

static std::unordered_map<std::string, std::string> g_Wordbooks;
static std::mutex g_WordbooksMutex;
bool SetWordbooks(const char* key,char* value)
{
    std::string strKey(key);
    std::string strValue(value);
    g_WordbooksMutex.lock();
    g_Wordbooks[strKey] = strValue;
    g_WordbooksMutex.unlock();
    BOOST_LOG(info) << "SetWordbooks, set " << strKey << ", " << strValue;
    return true;
}

bool QueryWordbooks(std::string strKey,std::string& strValue)
{
    g_WordbooksMutex.lock();
    auto it = g_Wordbooks.find(strKey);
    if(it != g_Wordbooks.end())
    {
        strValue = it->second;
        g_WordbooksMutex.unlock();
        return true;
    }
    g_WordbooksMutex.unlock();
    return false;
}

bool
RemoveWordbooks(std::string strKey) {
  g_WordbooksMutex.lock();
  auto it = g_Wordbooks.find(strKey);
  if (it != g_Wordbooks.end()) {
    g_Wordbooks.erase(it);
    g_WordbooksMutex.unlock();
    return true;
  }
  g_WordbooksMutex.unlock();
  return false;
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::string::npos) {
        str.replace(startPos, from.length(), to);
        startPos += to.length(); // Move past the replaced part
    }
    return str;
}

//called from Polaris to set the display list
bool SetDisplayList(const char* list)
{
    //std::string strListTmp(list);
    //BOOST_LOG(info) << "SetDisplayList:" << strListTmp;
    //std::string strList = replaceAll(strListTmp, "\\\\", "\\");
    //BOOST_LOG(info) << "after change, SetDisplayList:" << strList;
    std::string strList(list);

    std::vector<std::string> output_list;
    std::string output;
    std::istringstream iss(strList);
    while (std::getline(iss, output, ','))
    {
      std::string outName(output,strlen(output.c_str()));
      output_list.push_back(output);
    }
    video::set_display_changed(output_list);

    return true;
}
bool StopVideoStreaming(int streamId)
{
    video::StopVideoStreaming(streamId);
    return true;
}

//called from other module to notify the client status changed
void OnClientStatusChanged(std::string clientId,int status)
{
    if(g_SunshineCallTable.OnClientStatusChanged)
    {
        g_SunshineCallTable.OnClientStatusChanged(clientId.c_str(), status);
    }
}

void OnSessionStatusChanged(int status, std::string statusInfo)
{
    if(g_SunshineCallTable.OnSessionStatusChanged)
    {
        g_SunshineCallTable.OnSessionStatusChanged(status, statusInfo.c_str());
    }
}

void OnDummyDisplay(int displayId, bool isDummy, const std::string& displayName)
{
    if(g_SunshineCallTable.OnDummyDisplay)
    {
        g_SunshineCallTable.OnDummyDisplay(displayId, isDummy, displayName.c_str());
    }
}


#ifdef _WIN32
static HMODULE _hModule = nullptr;
#endif

int
QueryWordbookswithLen(const char *key, char *buffer, int lenMax)
{
    std::string strKey(key);
    std::string strValue;
    bool bOK = QueryWordbooks(strKey, strValue);
    if (!bOK)
    {
      return 0;
    }
    int szValue = strValue.size();
    int len = szValue > lenMax ? lenMax : szValue;
    memcpy(buffer, strValue.c_str(), len);
    return len;
}

using args_t = SimpleWeb::CaseInsensitiveMultimap;
std::string
get_arg(const args_t &args, const char *name) {
    auto it = args.find(name);
    if (it == std::end(args)) {
        throw std::out_of_range(name);
    }
    return it->second;
}

rtsp_stream::launch_session_t
make_launch_session(bool host_audio, const args_t &args, std::vector<unsigned char> & AesKey,std::string &clientUuid) {
  rtsp_stream::launch_session_t launch_session;
  launch_session.host_audio = host_audio;
  launch_session.gcm_key = util::from_hex<crypto::aes_t>(get_arg(args, "rikey"), true);
  uint32_t prepend_iv = util::endian::big<uint32_t>(util::from_view(get_arg(args, "rikeyid")));
  auto prepend_iv_p = (uint8_t *) &prepend_iv;
  auto next = std::copy(prepend_iv_p, prepend_iv_p + sizeof(prepend_iv), std::begin(launch_session.iv));
  std::fill(next, std::end(launch_session.iv), 0);
  launch_session.AesKey.assign(AesKey.begin(), AesKey.end());

  launch_session.szClientUUid = clientUuid;

  return launch_session;
}

bool TerminateSession() {
    if (rtsp_stream::session_count() != 0) {
      return false;
    }

    if (proc::proc.running() > 0) {
      proc::proc.terminate();
    }
    return true;
}

int checkResumeArgs(args_t &args) {
    if (rtsp_stream::isIniting)
    {
      BOOST_LOG(error) << "resume sunshine is initing";
      return 601;
    }
    if (rtsp_stream::session_count() == config::stream.channels) {
        BOOST_LOG(error) << "resume rtsp_stream::session_count reached the upper limit : " << config::stream.channels;
        return 503;
    }
    auto current_appid = proc::proc.running();
    if (current_appid == 0) {
      BOOST_LOG(error) << "no process running, current appid: " << current_appid;
      return 503;
    }
    return 0;
}

int checkLaunchArgs(args_t &args) {
  if (rtsp_stream::isIniting)
    {
      BOOST_LOG(error) << "launch sunshine is initing";
      return 601;
    }
    if (rtsp_stream::session_count() == config::stream.channels) {
        BOOST_LOG(error) << "launch rtsp_stream::session_count reached the upper limit : " << config::stream.channels;
        return 503;
    }
    auto current_appid = proc::proc.running();
    if (current_appid > 0) {
      BOOST_LOG(error) << "current process still running, current appid: " << current_appid;
      return 400;
    }

    int appid = stoi(get_arg(args, "appid"));
    if (appid > 0) {
      auto err = proc::proc.execute(appid);
      if (err != 0) {
        BOOST_LOG(error) << "checkLaunchArgs, proc::proc.execute error: " << err;
        return err;
      }
    }

    return 0;
}

void RunSession(args_t &args, boost::property_tree::ptree &pt, const char *aesKey) {
    if(aesKey == nullptr || aesKey[0] ==0) {
      ENCRYPT_FLAG = 0;
      BOOST_LOG(info) << "***** RunSession without AES encryption.********";
    }
    else {
      ENCRYPT_FLAG =1;
      BOOST_LOG(info) << "***** RunSession with AES encryption.********";
    }
    std::string sessionID = args.find("sessionID")->second;
    std::string userName = args.find("username")->second;

    auto iter = args.find("watermark");
    if (iter == args.end() || iter->second == "Disable") {
      config::stream.userName = "";
    } else {
      config::stream.userName = userName;
    }
    proc::proc.setCSession(sessionID);

    bool host_audio = pt.get<bool>("host_audio");

    std::string aesKeyStr(aesKey);
    std::vector<unsigned char> Aeskey(aesKeyStr.begin(), aesKeyStr.end());

    std::string clientUUID = pt.get<std::string>("clientUUID");

    rtsp_stream::launch_session_raise(make_launch_session(host_audio, args, Aeskey, clientUUID));
    OnClientStatusChanged(clientUUID, SunshineStatus::ClientStatus_Connect);
}


std::unordered_map<std::string_view, std::function<int(args_t& args)>> g_actionMap = {
    {"launch"sv, checkLaunchArgs},
    {"resume"sv, checkResumeArgs},
};

int OnClientAction(const char *name, const char *jsonArgs, const char *aesKey) {
    BOOST_LOG(info) << "OnClientAction, name " << name << ", jsonArgs : " << jsonArgs << ", aesKey : " << aesKey;
    auto fn = g_actionMap.find(name);
    if (fn != g_actionMap.end()) {
        std::istringstream is(jsonArgs);
        boost::property_tree::ptree pt;
        read_json(is, pt);

        const boost::property_tree::ptree argsPt = pt.get_child("args");
        args_t args;
        for (const auto& item : argsPt) {
          args.emplace(item.first, item.second.get_value<std::string>());
        }

        if (int retCode = fn->second(args); retCode != 0) {
            return retCode;
        }
        RunSession(args, pt, aesKey);
    } else if (std::string(name) == "terminate") {
      TerminateSession();
    }  else {
      // Todo: handle other condition
      BOOST_LOG(error) << "OnClientAction, name : " << name << " not handled";
      return 404;
    }
    return 200;
}

int OnQueryRuntimeInfo(const char *name) {
    BOOST_LOG(debug) << "OnQueryRuntimeInfo, name : " << name;
    std::string infoName(name);
    if (infoName == "proc::proc.running()") {
      return proc::proc.running();
    } else if (infoName == "rtsp_stream::session_count() == config::stream.channels") {
      return (!rtsp_stream::isIniting && rtsp_stream::session_count() == config::stream.channels) ? 1 : 0;
    }

    BOOST_LOG(warning) << "OnQueryRuntimeInfo, name : " << name << " not handled";
    return 0;
}

double OnQueryMetrics(const char *name) {
  std::string metricsName(name);
  auto res = MetricsManager::I().query_metrics(metricsName);
  return res;
}

struct ValidationResult {
  bool result;
  std::chrono::steady_clock::time_point timestamp;
};
bool
OnValidateDisplay(const char *display) {
  static std::unordered_map<std::string, ValidationResult> cache;
  auto now = std::chrono::steady_clock::now();
  std::string displayName = display;
  const int expired_seconds = 120;
  // Check if the result is in the cache and still valid
  auto it = cache.find(displayName);
  if (it != cache.end()) {
    auto cnt = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
    if (cnt < expired_seconds) {
      return it->second.result;
    }
  }

  // Call the real ValidateDisplay function
  bool validationResult = video::ValidateDisplay(display);

  // Update the cache
  cache[displayName] = { validationResult, now };

  return validationResult;
}

bool LoadBridge(const char* hostPath)
{
#ifdef _WIN32
    HMODULE hModule = LoadLibraryEx(PBridgeLib, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (hModule == nullptr)
    {
        return false;
    }
    _hModule = hModule;
    funcLoad load = (funcLoad) GetProcAddress(hModule, "Load");
    int retCode =-1;
    if(load)
    {
        g_SunshineCallTable.SetDisplayList = SetDisplayList;
        g_SunshineCallTable.StopVideoStreaming = StopVideoStreaming;
        g_SunshineCallTable.SetWordbooks = SetWordbooks;
        g_SunshineCallTable.QueryWordbooks = QueryWordbookswithLen;
        g_SunshineCallTable.OnClientAction = OnClientAction;
        g_SunshineCallTable.ValidateDisplay = OnValidateDisplay;
        g_SunshineCallTable.OnQueryRuntimeInfo = OnQueryRuntimeInfo;

        g_SunshineCallTable.OnQueryMetrics =  OnQueryMetrics;
        retCode = load((void *) &g_SunshineCallTable, hostPath);
    }
#endif
    return (retCode == 0);
}
void UnloadBridge()
{
    if(_hModule == nullptr)
    {
        return;
    }
    #ifdef _WIN32
    funcUnload unload = (funcUnload) GetProcAddress(_hModule, "Unload");
    if(unload)
    {
        unload();
    }
    FreeLibrary(_hModule);
    _hModule = nullptr;
    #endif 
}