/**
  * @file main.cpp
  */

// standard includes
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

// lib includes
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/attributes.hpp>
#include <boost/filesystem.hpp>

// local includes
#include "config.h"
#include "confighttp.h"
#include "httpcommon.h"
#include "main.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "system_tray.h"
#include "thread_pool.h"
#include "upnp.h"
#include "version.h"
#ifdef _WIN32
#include <windows.h>
#include <winuser.h>
#include <dbt.h>
#endif

#include "Halotype.h"

extern "C" {
#include <rs.h>
}

safe::mail_t mail::man;

using namespace std::literals;
namespace bl = boost::log;

thread_pool_util::ThreadPool task_pool;
bl::sources::severity_logger<int> verbose(0);  // Dominating output
bl::sources::severity_logger<int> debug(1);  // Follow what is happening
bl::sources::severity_logger<int> info(2);  // Should be informed about
bl::sources::severity_logger<int> warning(3);  // Strange events
bl::sources::severity_logger<int> error(4);  // Recoverable errors
bl::sources::severity_logger<int> fatal(5);  // Unrecoverable errors

bool display_cursor = true;
std::vector<std::string> gPhysicalDisplayNames;
int gNumPhysicalDisplays;
std::string physical_display_data_file;


using ostream_sink = bl::sinks::asynchronous_sink<bl::sinks::text_ostream_backend>;
using file_sink = bl::sinks::synchronous_sink<bl::sinks::text_file_backend>;
boost::shared_ptr<ostream_sink> console_sink;
boost::shared_ptr<file_sink> log_file_sink;

struct NoDelete {
  void
  operator()(void *) {}
};

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)

/**
  * @brief Print help to stdout.
  * @param name The name of the program.
  *
  * EXAMPLES:
  * ```cpp
  * print_help("sunshine");
  * ```
  */
void
print_help(const char *name) {
    std::cout
    << "Usage: "sv << name << " [options] [/path/to/configuration_file] [--cmd]"sv << std::endl
    << "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl
    << std::endl
    << "    Note: The configuration will be created if it doesn't exist."sv << std::endl
    << std::endl
    << "    --help                    | print help"sv << std::endl
    << "    --creds username password | set user credentials for the Web manager"sv << std::endl
    << "    --version                 | print the version of sunshine"sv << std::endl
    << std::endl
    << "    flags"sv << std::endl
    << "        -0 | Read PIN from stdin"sv << std::endl
    << "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl
    << "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv << std::endl
    << "        -2 | Force replacement of headers in video stream"sv << std::endl
    << "        -p | Enable/Disable UPnP"sv << std::endl
    << std::endl;
}

namespace help {
  int
  entry(const char *name, int argc, char *argv[]) {
    print_help(name);
    return 0;
  }
}  // namespace help

namespace version {
  int
  entry(const char *name, int argc, char *argv[]) {
    std::cout << PROJECT_NAME << " version: v" << PROJECT_VER << std::endl;
    return 0;
  }
}  // namespace version

/**
  * @brief Flush the log.
  *
  * EXAMPLES:
  * ```cpp
  * log_flush();
  * ```
  */
void
log_flush() {
  console_sink->flush();
  log_file_sink->flush();
}

std::map<int, std::function<void()>> signal_handlers;
void
on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

template <class FN>
void
on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}

namespace gen_creds {
  int
  entry(const char *name, int argc, char *argv[]) {
    if (argc < 2 || argv[0] == "help"sv || argv[1] == "help"sv) {
      print_help(name);
      return 0;
    }

    http::save_user_creds(config::halo.credentials_file, argv[0], argv[1]);

    return 0;
  }
}  // namespace gen_creds

std::map<std::string_view, std::function<int(const char *name, int argc, char **argv)>> cmd_to_func {
  { "creds"sv, gen_creds::entry },
  { "help"sv, help::entry },
  { "version"sv, version::entry }
};

/**
  * @brief Main application entry point.
  * @param argc The number of arguments.
  * @param argv The arguments.
  *
  * EXAMPLES:
  * ```cpp
  * main(1, const char* args[] = {"sunshine", nullptr});
  * ```
  */

void log_write(const char* szLog)
{
    BOOST_LOG(info) << szLog;
}

/**
  * @brief Read a file to string.
  * @param path The path of the file.
  * @return `std::string` : The contents of the file.
  *
  * EXAMPLES:
  * ```cpp
  * std::string contents = read_file("path/to/file");
  * ```
  */
std::string
read_file(const char *path) {
  if (!std::filesystem::exists(path)) {
    BOOST_LOG(debug) << "Missing file: " << path;
    return {};
  }

  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  while (!in.eof()) {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}

/**
  * @brief Writes a file.
  * @param path The path of the file.
  * @param contents The contents to write.
  * @return `int` : `0` on success, `-1` on failure.
  *
  * EXAMPLES:
  * ```cpp
  * int write_status = write_file("path/to/file", "file contents");
  * ```
  */
int
write_file(const char *path, const std::string_view &contents) {
  std::ofstream out(path);

  if (!out.is_open()) {
    return -1;
  }

  out << contents;

  return 0;
}


/**
  * @brief Map a specified port based on the base port.
  * @param port The port to map as a difference from the base port.
  * @return `std:uint16_t` : The mapped port number.
  *
  * EXAMPLES:
  * ```cpp
  * std::uint16_t mapped_port = map_port(1);
  * ```
  */
std::uint16_t
map_port(int port) {
  // TODO: Ensure port is in the range of 21-65535
  // TODO: Ensure port is not already in use by another application
  return (std::uint16_t)((int) config::halo.port + port);
}







HaloCallTable *g_HaloCallTable = nullptr;
std::unique_ptr<std::thread> g_NvhttpThreadPtr;
std::unique_ptr<std::thread> g_CfghttpThreadPtr;
std::unique_ptr<platf::deinit_t> mDNS;
std::future<void> sync_mDNS;
void InitLogging();

extern "C" __declspec(dllexport) void
StartHalo(void *callTable) {
  InitLogging();
  if (callTable != nullptr) {
    g_HaloCallTable = (HaloCallTable*)callTable;
    g_HaloCallTable->CloseBlockedPair = [] (const char * uniqueID) {
      nvhttp::closeBlockedPair(uniqueID);
    };
  }

  if (http::init()) {
          BOOST_LOG(error) << "http dll failed to initialize"sv;
          return;
  }
  g_NvhttpThreadPtr = std::make_unique<std::thread>(nvhttp::start);
  g_CfghttpThreadPtr = std::make_unique<std::thread>(confighttp::start);

  BOOST_LOG(info) << "Server thread created"sv;
}

extern "C" __declspec(dllexport) void
StopHalo() {
  if (nvhttp::isRunning()) {
    BOOST_LOG(info) << "try stop nvhttp server"sv;
    nvhttp::stop();
    if (g_NvhttpThreadPtr->joinable()) {
      g_NvhttpThreadPtr->join();
    }
    g_NvhttpThreadPtr.reset(nullptr);
  }

  if (confighttp::isRunning()) {
    confighttp::stop();
    if (g_CfghttpThreadPtr->joinable()) {
      g_CfghttpThreadPtr->join();
    }
    g_CfghttpThreadPtr.reset(nullptr);
  }
  BOOST_LOG(info) << "Server stoped"sv;
}

void
ClientAction(const std::string &name, const std::string &jsonArgs, const std::string &clientUUID) {
  if (g_HaloCallTable) {
    g_HaloCallTable->OnClientAction(name.c_str(), jsonArgs.c_str(), clientUUID.c_str());
  }
}

void
TriggerPairTask(std::string uniqueID) {
  if (g_HaloCallTable) {
    g_HaloCallTable->TriggerPairTask(uniqueID.c_str());
  }
}

void
ClientAction(const std::string &name) {
  if (g_HaloCallTable) {
    g_HaloCallTable->OnClientAction(name.c_str(), "", "");
  }
}

void
ClientSendMsg(const std::string &clientId, const std::string &type, const std::string &msg, const std::string &reqKey) {
  if (g_HaloCallTable) {
    g_HaloCallTable->OnClientSendMsg(clientId.c_str(), type.c_str(), msg.c_str(), reqKey.c_str());
  }
}

const int LenMax = 16*1024*1024;
std::vector<unsigned char> _dummyContentType;
bool
QueryWordbooks(const std::string &key, std::vector<unsigned char> &value, std::vector<unsigned char> &contentType) {
  int valueLen = LenMax;
  int contentTypeLen = 1024;
  bool bOk = false;
  if (g_HaloCallTable)
  {
    value.resize(LenMax);
    contentType.resize(contentTypeLen);
    bOk = g_HaloCallTable->QueryWordbooks(key.c_str(), value.data(), &valueLen, contentType.data(), &contentTypeLen);
    value.resize(valueLen);
    contentType.resize(contentTypeLen);
  }
  return bOk;
}

int
RemoveWordbooks(const std::string &key) {
  int rmCnt = 0;
  if (g_HaloCallTable)
  {
    rmCnt = g_HaloCallTable->RemoveWordbooks(key.c_str());
  }
return rmCnt;
}

bool
WaitWordbook(const std::string &key,
  std::vector<unsigned char> &value,
  std::vector<unsigned char> &contentType, bool needDelete) {
  BOOST_LOG(info) << "in WaitWordbook(), waiting for wordbook response , key =  " << key;
    int queryCount = 1000;
    bool succ = false;
    while (!succ && queryCount-- > 0) {
      succ = QueryWordbooks(key, value, contentType);
      if (succ) {
          break;
      }
      std::this_thread::sleep_for(10ms);
    }
    BOOST_LOG(info) <<
      "in WaitWordbook(), received wordbook response , result =  " <<
      succ <<
      " , value.size = " <<
      value.size();
    if (succ && needDelete)
    {
      RemoveWordbooks(key);
    }
    return succ;
}

void
ClientQueryRuntimeInfo(const std::string &name) {
  if (g_HaloCallTable)
  {
    g_HaloCallTable->QueryRuntimeInfo(name.c_str());
  }
}

void
PortalConfigChange(const std::string &jsonConfig) {
  if (g_HaloCallTable) {
    g_HaloCallTable->OnPortalConfigChange(jsonConfig.c_str());
  }
}

void InitLogging() {
  // 1. create console sink
  console_sink = boost::make_shared<ostream_sink>();
  boost::shared_ptr<std::ostream> stream { &std::cout, NoDelete {} };
  console_sink->locked_backend()->add_stream(stream);

  // 2. create log file sink, rotate every day
  std::string rotate_log_file = config::halo.log_file;
  rotate_log_file = rotate_log_file.substr(0, rotate_log_file.size() - 4);
  rotate_log_file += "_%Y%m%d.log",
  log_file_sink = bl::add_file_log(bl::keywords::file_name = config::halo.log_file,
    bl::keywords::target_file_name = rotate_log_file,
    bl::keywords::time_based_rotation = bl::sinks::file::rotation_at_time_point(0, 0, 0),
    bl::keywords::open_mode = std::ios_base::app, // Open for append and immediately rotate
    bl::keywords::enable_final_rotation = false // Rotate only on startup to cover also the crash / kill cases
);

  console_sink->set_filter(severity >= config::halo.min_log_level);
  log_file_sink->set_filter(severity >= config::halo.min_log_level);

  auto formatter = [message = "Message"s, severity = "Severity"s](const bl::record_view &view,
    bl::formatting_ostream &os) {
    constexpr int DATE_BUFFER_SIZE = 21 + 2 + 1;  // Full string plus ": \0"

    auto log_level = view.attribute_values()[severity].extract<int>().get();

    std::string_view log_type;
    switch (log_level) {
      case 0:
        log_type = "Verbose: "sv;
        break;
      case 1:
        log_type = "Debug: "sv;
        break;
      case 2:
        log_type = "Info: "sv;
        break;
      case 3:
        log_type = "Warning: "sv;
        break;
      case 4:
        log_type = "Error: "sv;
        break;
      case 5:
        log_type = "Fatal: "sv;
        break;
    };

    char _date[DATE_BUFFER_SIZE];
    std::time_t t = std::time(nullptr);
    strftime(_date, DATE_BUFFER_SIZE, "[%Y:%m:%d:%H:%M:%S]: ", std::localtime(&t));

    os << _date << log_type << view.attribute_values()[message].extract<std::string>();
  };

  console_sink->set_formatter(formatter);
  log_file_sink->set_formatter(formatter);

  // Flush after each log record to ensure log file contents on disk isn't stale.
  // This is particularly important when running from a Windows service.
  console_sink->locked_backend()->auto_flush(true);
  log_file_sink->locked_backend()->auto_flush(true);

  bl::core::get()->add_sink(console_sink);
  bl::core::get()->add_sink(log_file_sink);
}


BOOL APIENTRY
DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    BOOL succ = TRUE;
    switch (ul_reason_for_call) {
      case DLL_PROCESS_ATTACH: {
        BOOST_LOG(info) << "loadding Halo dll"sv;
        if (config::parse(0, nullptr)) {
          BOOST_LOG(error) << "Halo dll failed to initialize"sv;
          succ = FALSE;
          break;
        }
        proc::refresh(config::stream.file_apps);
        BOOST_LOG(info) << "Halo dll loaded"sv;
      }break;
      case DLL_THREAD_ATTACH:{

      }break;
      case DLL_THREAD_DETACH:{

      }break;
      case DLL_PROCESS_DETACH: {
        BOOST_LOG(info) << "hhtp dll unloaded"sv;
      }break;
      default:{

      }
    }
    return succ;
}

int main(int argc, char *argv[]) {
  if (config::parse(0, nullptr)) {
    BOOST_LOG(error) << "Halo dll failed to initialize"sv;
    return -1;
  }
  StartHalo(nullptr);
  system("pause");
  StopHalo();
  return 0;
}