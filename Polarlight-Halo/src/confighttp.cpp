// Created by TheElixZammuto on 2021-05-09.
// TODO: Authentication, better handling of routes common to nvhttp, cleanup

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>
#include <set>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/algorithm/string.hpp>

#include <boost/asio/ssl/context.hpp>

#include <boost/filesystem.hpp>

#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <boost/asio/ssl/context_base.hpp>

#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "httpcommon.h"
#include "main.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "utility.h"
#include "uuid.h"
#include "version.h"
#include <sstream>
#include "metrics.h"
#include <string>

using namespace std::literals;

namespace confighttp {
  namespace fs = std::filesystem;
  namespace pt = boost::property_tree;

  using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;

  using args_t = SimpleWeb::CaseInsensitiveMultimap;
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  enum class op_e {
    ADD,
    REMOVE
  };

  void
  print_req(const req_https_t &request) {
    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << (name == "Authorization" ? "CREDENTIALS REDACTED" : val);
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  void
  send_unauthorized(resp_https_t response, req_https_t request) {
    auto address = request->remote_endpoint().address().to_string();
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      { "WWW-Authenticate", R"(Basic realm="Sunshine Gamestream Host", charset="UTF-8")" }
    };
    response->write(SimpleWeb::StatusCode::client_error_unauthorized, headers);
  }

  void
  send_redirect(resp_https_t response, req_https_t request, const char *path) {
    auto address = request->remote_endpoint().address().to_string();
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      { "Location", path }
    };
    response->write(SimpleWeb::StatusCode::redirection_temporary_redirect, headers);
  }

  bool
  authenticate(resp_https_t response, req_https_t request) {
    auto address = request->remote_endpoint().address().to_string();
    auto ip_type = net::from_address(address);

    if (ip_type > http::origin_web_ui_allowed) {
      BOOST_LOG(info) << "Web UI: ["sv << address << "] -- denied"sv;
      response->write(SimpleWeb::StatusCode::client_error_forbidden);
      return false;
    }

    // If credentials are shown, redirect the user to a /welcome page
    if (config::halo.username.empty()) {
      send_redirect(response, request, "/welcome");
      return false;
    }

    auto fg = util::fail_guard([&]() {
      send_unauthorized(response, request);
    });

    auto auth = request->header.find("authorization");
    if (auth == request->header.end()) {
      return false;
    }

    auto &rawAuth = auth->second;
    auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));

    int index = authData.find(':');
    if (index >= authData.size() - 1) {
      return false;
    }

    auto username = authData.substr(0, index);
    auto password = authData.substr(index + 1);
    auto hash = util::hex(crypto::hash(password + config::halo.salt)).to_string();

    if (username != config::halo.username || hash != config::halo.password) {
      return false;
    }

    fg.disable();
    return true;
  }

  void
  not_found(resp_https_t response, req_https_t request) {
    pt::ptree tree;
    tree.put("root.<xmlattr>.status_code", 404);

    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());

    *response << "HTTP/1.1 404 NOT FOUND\r\n"
              << data.str();
  }

  // todo - combine these functions into a single function that accepts the page, i.e "index", "pin", "apps"
  void
  getIndexPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "index.html");
    response->write(header + content);
  }

  void
  getPinPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "pin.html");
    response->write(header + content);
  }

  void
  getAppsPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Access-Control-Allow-Origin", "https://images.igdb.com/");

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "apps.html");
    response->write(header + content, headers);
  }

  void
  getClientsPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "clients.html");
    response->write(header + content);
  }

  void
  getConfigPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "config.html");
    response->write(header + content);
  }

  void
  getPasswordPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "password.html");
    response->write(header + content);
  }

  void
  getWelcomePage(resp_https_t response, req_https_t request) {
    print_req(request);
    if (!config::halo.username.empty()) {
      send_redirect(response, request, "/");
      return;
    }
    std::string header = read_file(WEB_DIR "header-no-nav.html");
    std::string content = read_file(WEB_DIR "welcome.html");
    response->write(header + content);
  }

  void
  getTroubleshootingPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string header = read_file(WEB_DIR "header.html");
    std::string content = read_file(WEB_DIR "troubleshooting.html");
    response->write(header + content);
  }

  void
  getFaviconImage(resp_https_t response, req_https_t request) {
    // todo - combine function with getSunshineLogoImage and possibly getNodeModules
    // todo - use mime_types map
    print_req(request);

    std::ifstream in(WEB_DIR "images/favicon.ico", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/x-icon");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  void
  getSunshineLogoImage(resp_https_t response, req_https_t request) {
    // todo - combine function with getFaviconImage and possibly getNodeModules
    // todo - use mime_types map
    print_req(request);

    std::ifstream in(WEB_DIR "images/logo-sunshine-45.png", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/png");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  bool
  isChildPath(fs::path const &base, fs::path const &query) {
    auto relPath = fs::relative(base, query);
    return *(relPath.begin()) != fs::path("..");
  }

  void
  getNodeModules(resp_https_t response, req_https_t request) {
    print_req(request);
    fs::path webDirPath(WEB_DIR);
    fs::path nodeModulesPath(webDirPath / "node_modules");

    // .relative_path is needed to shed any leading slash that might exist in the request path
    auto filePath = fs::weakly_canonical(webDirPath / fs::path(request->path).relative_path());

    // Don't do anything if file does not exist or is outside the node_modules directory
    if (!isChildPath(filePath, nodeModulesPath)) {
      BOOST_LOG(warning) << "Someone requested a path " << filePath << " that is outside the node_modules folder";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
    else if (!fs::exists(filePath)) {
      response->write(SimpleWeb::StatusCode::client_error_not_found);
    }
    else {
      auto relPath = fs::relative(filePath, webDirPath);
      // get the mime type from the file extension mime_types map
      // remove the leading period from the extension
      auto mimeType = mime_types.find(relPath.extension().string().substr(1));
      // check if the extension is in the map at the x position
      if (mimeType != mime_types.end()) {
        // if it is, set the content type to the mime type
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", mimeType->second);
        std::ifstream in(filePath.string(), std::ios::binary);
        response->write(SimpleWeb::StatusCode::success_ok, in, headers);
      }
      // do not return any file if the type is not in the map
    }
  }

  void
  getApps(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string content = read_file(config::stream.file_apps.c_str());
    response->write(content);
  }

  static std::vector<std::string> split(const std::string& str, char delim)
  {
    std::vector<std::string> list;
    std::string temp;
    std::stringstream ss(str);
    while (std::getline(ss, temp, delim))
    {
      list.push_back(temp);
    }
    return list;
  }
  
  void
  getMetrics(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);
    
    //std::string content = read_file(config::stream.file_apps.c_str());
    std::string content = "";
  std::string metrics_items;
  
  for (auto &[name, val] : request->parse_query_string()) {
      if (name == "metrics_list") {
        metrics_items = val;
      break;
      }
    }  
    auto metrics_list = split(metrics_items, ',');
    char buf[100];
    for (auto &metrics_item : metrics_list) {
      content += metrics_item;
      content += ":";
          auto val = MetricsManager::I().query_metrics(metrics_item);
          std::snprintf(buf,100, "%lld",val);
          content += buf;      
      content += "\n";
    }
    response->write(content);
  }

  void
  getLogs(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string content = read_file(config::halo.log_file.c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/plain");
    response->write(SimpleWeb::StatusCode::success_ok, content, headers);
  }

  void
  saveApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    pt::ptree inputTree, fileTree;

    BOOST_LOG(fatal) << config::stream.file_apps;
    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      pt::read_json(config::stream.file_apps, fileTree);

      if (inputTree.get_child("prep-cmd").empty()) {
        inputTree.erase("prep-cmd");
      }

      if (inputTree.get_child("detached").empty()) {
        inputTree.erase("detached");
      }

      auto &apps_node = fileTree.get_child("apps"s);
      int index = inputTree.get<int>("index");

      inputTree.erase("index");

      if (index == -1) {
        apps_node.push_back(std::make_pair("", inputTree));
      }
      else {
        // Unfortunately Boost PT does not allow to directly edit the array, copy should do the trick
        pt::ptree newApps;
        int i = 0;
        for (const auto &kv : apps_node) {
          if (i == index) {
            newApps.push_back(std::make_pair("", inputTree));
          }
          else {
            newApps.push_back(std::make_pair("", kv.second));
          }
          i++;
        }
        fileTree.erase("apps");
        fileTree.push_back(std::make_pair("apps", newApps));
      }
      pt::write_json(config::stream.file_apps, fileTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveApp: "sv << e.what();

      outputTree.put("status", "false");
      outputTree.put("error", "Invalid Input JSON");
      return;
    }

    outputTree.put("status", "true");
    proc::refresh(config::stream.file_apps);
  }

  void
  deleteApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });
    pt::ptree fileTree;
    try {
      pt::read_json(config::stream.file_apps, fileTree);
      auto &apps_node = fileTree.get_child("apps"s);
      int index = stoi(request->path_match[1]);

      if (index < 0) {
        outputTree.put("status", "false");
        outputTree.put("error", "Invalid Index");
        return;
      }
      else {
        // Unfortunately Boost PT does not allow to directly edit the array, copy should do the trick
        pt::ptree newApps;
        int i = 0;
        for (const auto &kv : apps_node) {
          if (i++ != index) {
            newApps.push_back(std::make_pair("", kv.second));
          }
        }
        fileTree.erase("apps");
        fileTree.push_back(std::make_pair("apps", newApps));
      }
      pt::write_json(config::stream.file_apps, fileTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", "Invalid File JSON");
      return;
    }

    outputTree.put("status", "true");
    proc::refresh(config::stream.file_apps);
  }

  void
  uploadCover(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();
    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      SimpleWeb::StatusCode code = SimpleWeb::StatusCode::success_ok;
      if (outputTree.get_child_optional("error").has_value()) {
        code = SimpleWeb::StatusCode::client_error_bad_request;
      }

      pt::write_json(data, outputTree);
      response->write(code, data.str());
    });
    pt::ptree inputTree;
    try {
      pt::read_json(ss, inputTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "UploadCover: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", e.what());
      return;
    }

    auto key = inputTree.get("key", "");
    if (key.empty()) {
      outputTree.put("error", "Cover key is required");
      return;
    }
    auto url = inputTree.get("url", "");

    const std::string coverdir = platf::appdata().string() + "/covers/";
    if (!boost::filesystem::exists(coverdir)) {
      boost::filesystem::create_directory(coverdir);
    }

    std::basic_string path = coverdir + http::url_escape(key) + ".png";
    if (!url.empty()) {
      if (http::url_get_host(url) != "images.igdb.com") {
        outputTree.put("error", "Only images.igdb.com is allowed");
        return;
      }
      if (!http::download_file(url, path)) {
        outputTree.put("error", "Failed to download cover");
        return;
      }
    }
    else {
      auto data = SimpleWeb::Crypto::Base64::decode(inputTree.get<std::string>("data"));

      std::ofstream imgfile(path);
      imgfile.write(data.data(), (int) data.size());
    }
    outputTree.put("path", path);
  }

  void
  getConfig(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    outputTree.put("status", "true");
    outputTree.put("platform", HALO_PLATFORM);
    outputTree.put("version", PROJECT_VER);
    outputTree.put("restart_supported", platf::restart_supported());

    auto vars = config::parse_config(read_file(config::halo.config_file.c_str()));

    for (auto &[name, value] : vars) {
      outputTree.put(std::move(name), std::move(value));
    }
  }

  void
  saveConfig(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;
    BOOST_LOG(info) << "saveConfig: ";
    print_req(request);

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();
    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });
    pt::ptree inputTree;
    bool dispChanged = false;
    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      for (const auto &kv : inputTree) {
        std::string value = inputTree.get<std::string>(kv.first);
        //to support dyanmicly creating virtual display, we need to change back
        if(kv.first =="output_name") {
          if(config::video.output_name != value) {
            dispChanged = true;
            config::video.output_fullname = value;
            inputTree.put("output_fullname",value);
            auto pos = value.find(" --");
            if (pos != value.npos) {
               value = value.substr(0, pos);
               config::video.output_name =value;
               inputTree.put("output_name",value);
            }
          }
        }
        else if(kv.first =="output2_name") {
          if(config::video.output2_name != value) {
            dispChanged = true;
            config::video.output2_fullname = value;
            inputTree.put("output2_fullname",value);
            auto pos = value.find(" --");
            if (pos != value.npos) {
               value = value.substr(0, pos);
               config::video.output2_name =value;
               inputTree.put("output2_name",value);           
            }
            BOOST_LOG(info) << "output2_name: ";
            if (!config::video.output2_name.empty()) {
              config::video.haveSecondOutput = true;
              config::video.afterhaveSecondOutput = config::video.haveSecondOutput;
            }  
          }              
        }
      }
      int v_disp = 0;
      config::video.virtualDisplayCount =v_disp;
      inputTree.put("virtualDisplayCount",v_disp); 
      for (const auto &kv : inputTree) {
        std::string value = inputTree.get<std::string>(kv.first);
        if (value.length() == 0 || value.compare("null") == 0) continue;
        configStream << kv.first << " = " << value << std::endl;        
      }
      write_file(config::halo.config_file.c_str(), configStream.str());
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", e.what());
      return;
    }
#ifdef _do_dynamic_switch    
    if(dispChanged) {
      video::reinit();
    }
#endif    
  }

#if 0
  void virtualDisplay(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();

    pt::ptree inputTree, outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      auto str = data.str();
      response->write(str);
    });

    try {
      pt::read_json(ss, inputTree);
      auto addOrRemove = inputTree.count("AddOrRemove") > 0 ? inputTree.get<bool>("AddOrRemove") : true;
      bool bOK = true;
      std::string display_outputs;
      int prev_num = AuroraMgr().EnumDisplayOutputs(display_outputs,true);

      if(addOrRemove){
        bOK = AuroraMgr().CreateDisplay();
      }
      else {
        bOK = AuroraMgr().RemoveDisplay();
      }
      int loop_cnt =100;
      int new_num = AuroraMgr().EnumDisplayOutputs(display_outputs);
      while(new_num == prev_num && loop_cnt>0) {
        std::this_thread::sleep_for(50ms);
        new_num = AuroraMgr().EnumDisplayOutputs(display_outputs);
        loop_cnt--;
      }
      outputTree.put("display_outputs", display_outputs);
      outputTree.put("status", bOK?"true":"false");
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "virtualDisplay: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", e.what());
      return;
    }
  }
#endif 
  void
  restart(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();
    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    if (!platf::restart_supported()) {
      outputTree.put("status", false);
      outputTree.put("error", "Restart is not currently supported on this platform");
      return;
    }

    if (!platf::restart()) {
      outputTree.put("status", false);
      outputTree.put("error", "Restart failed");
      return;
    }

    outputTree.put("status", true);
  }

  void
  savePassword(resp_https_t response, req_https_t request) {
    if (!config::halo.username.empty() && !authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();

    pt::ptree inputTree, outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      auto username = inputTree.count("currentUsername") > 0 ? inputTree.get<std::string>("currentUsername") : "";
      auto newUsername = inputTree.get<std::string>("newUsername");
      auto password = inputTree.count("currentPassword") > 0 ? inputTree.get<std::string>("currentPassword") : "";
      auto newPassword = inputTree.count("newPassword") > 0 ? inputTree.get<std::string>("newPassword") : "";
      auto confirmPassword = inputTree.count("confirmNewPassword") > 0 ? inputTree.get<std::string>("confirmNewPassword") : "";
      if (newUsername.length() == 0) newUsername = username;
      if (newUsername.length() == 0) {
        outputTree.put("status", false);
        outputTree.put("error", "Invalid Username");
      }
      else {
        auto hash = util::hex(crypto::hash(password + config::halo.salt)).to_string();
        if (config::halo.username.empty() || (username == config::halo.username && hash == config::halo.password)) {
          if (newPassword.empty() || newPassword != confirmPassword) {
            outputTree.put("status", false);
            outputTree.put("error", "Password Mismatch");
          }
          else {
            http::save_user_creds(config::halo.credentials_file, newUsername, newPassword);
            http::reload_user_creds(config::halo.credentials_file);
            outputTree.put("status", true);
          }
        }
        else {
          outputTree.put("status", false);
          outputTree.put("error", "Invalid Current Credentials");
        }
      }
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePassword: "sv << e.what();
      outputTree.put("status", false);
      outputTree.put("error", e.what());
      return;
    }
  }

  void
  savePin(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    pt::ptree inputTree, outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      std::string pin = inputTree.get<std::string>("pin");
      outputTree.put("status", nvhttp::pin(pin));
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePin: "sv << e.what();
      outputTree.put("status", false);
      outputTree.put("error", e.what());
      return;
    }
  }

  void
  unpairAll(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });
    nvhttp::erase_all_clients();
    outputTree.put("status", true);
  }

  void
  closeApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

  proc::proc.terminate();
    outputTree.put("status", true);
  }

  bool g_running = false;
  std::mutex g_cfghttpMutex;
  std::condition_variable g_cfghttpCV;
  void
  start() {
    // auto shutdown_event = mail::man->event<bool>(mail::shutdown);
    std::unique_lock lk(g_cfghttpMutex);
    g_running = true;
    auto port_https = map_port(PORT_HTTPS);

    https_server_t server { config::nvhttp.cert, config::nvhttp.pkey };
    server.default_resource["GET"] = not_found;
    server.resource["^/$"]["GET"] = getIndexPage;
    server.resource["^/pin$"]["GET"] = getPinPage;
    server.resource["^/apps$"]["GET"] = getAppsPage;
    server.resource["^/clients$"]["GET"] = getClientsPage;
    server.resource["^/config$"]["GET"] = getConfigPage;
    server.resource["^/password$"]["GET"] = getPasswordPage;
    server.resource["^/welcome$"]["GET"] = getWelcomePage;
    server.resource["^/troubleshooting$"]["GET"] = getTroubleshootingPage;
    server.resource["^/metrics$"]["GET"] = getMetrics;
    server.resource["^/api/pin$"]["POST"] = savePin;
    server.resource["^/api/apps$"]["GET"] = getApps;
    server.resource["^/api/logs$"]["GET"] = getLogs;
    server.resource["^/api/apps$"]["POST"] = saveApp;
    server.resource["^/api/config$"]["GET"] = getConfig;
    server.resource["^/api/config$"]["POST"] = saveConfig;
    server.resource["^/api/restart$"]["POST"] = restart;
    //server.resource["^/api/vdisp$"]["POST"] = virtualDisplay;
    server.resource["^/api/password$"]["POST"] = savePassword;
    server.resource["^/api/apps/([0-9]+)$"]["DELETE"] = deleteApp;
    server.resource["^/api/clients/unpair$"]["POST"] = unpairAll;
    server.resource["^/api/apps/close$"]["POST"] = closeApp;
    server.resource["^/api/covers/upload$"]["POST"] = uploadCover;
    server.resource["^/images/favicon.ico$"]["GET"] = getFaviconImage;
    server.resource["^/images/logo-sunshine-45.png$"]["GET"] = getSunshineLogoImage;
    server.resource["^/node_modules\\/.+$"]["GET"] = getNodeModules;
    server.config.reuse_address = true;
    server.config.address = "127.0.0.1"s;
    server.config.port = port_https;

    auto accept_and_run = [&](auto *s) {
      BOOST_LOG(info) << "********** confighttp::start() **********";
      try {
        s->start([](unsigned short port) {
          BOOST_LOG(info) << "Configuration UI available at [https://localhost:"sv << port << "]";
        });
      }
      catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling server->stop() from a different thread
        {
          std::lock_guard lock(g_cfghttpMutex);
        }
        if (!g_running) {
          return;
        }

        BOOST_LOG(fatal) << "Couldn't start Configuration HTTPS server on port ["sv << port_https << "]: "sv << err.what();
        return;
      }
    };
    std::thread tcp { accept_and_run, &server };

    g_cfghttpCV.wait(lk,[]()-> bool{return g_running == false;});

    BOOST_LOG(info) << "**************confighttp::start(), server.stop()";

    server.stop();

    tcp.join();
  }

    void 
  stop() {
    {
      std::lock_guard lock(g_cfghttpMutex);
      g_running = false;
    }
    g_cfghttpCV.notify_one();
  }

  bool
  isRunning() {
    std::lock_guard lock(g_cfghttpMutex);
    return g_running;
  }
}  // namespace confighttp
