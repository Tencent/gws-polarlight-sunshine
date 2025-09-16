#ifndef SUNSHINE_CONFIG_H
#define SUNSHINE_CONFIG_H

#include <bitset>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
//to archive the current log file
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

//to delete unnecessary log files
#include <dirent.h>
#include <algorithm>
#include <unistd.h>
#include <vector>
#include <iostream>

#define MAX_LOG_RETAINED 20
#define DIPS_PLACEHOLDER  "\\\\.\\DISPLAPLACEHOLDER"
#define DISPLAY_SCAN_LOOPCOUNT 2
#define CHECK_SYSTEM_READY_LOOPCOUNT 3

extern int ENCRYPT_FLAG;  //encypt flag

namespace config {
  struct video_t {
    // ffmpeg params
    int qp;  // higher == more compression and less quality

    int hevc_mode;

    int min_threads;  // Minimum number of threads/slices for CPU encoding
    struct {
      std::string sw_preset;
      std::string sw_tune;
    } sw;

    struct {
      std::optional<int> nv_preset;
      std::optional<int> nv_tune;
      std::optional<int> nv_rc;
      int nv_coder;
    } nv;

    struct {
      std::optional<int> qsv_preset;
      std::optional<int> qsv_cavlc;
    } qsv;

    struct {
      std::optional<int> amd_quality_h264;
      std::optional<int> amd_quality_hevc;
      std::optional<int> amd_rc_h264;
      std::optional<int> amd_rc_hevc;
      std::optional<int> amd_usage_h264;
      std::optional<int> amd_usage_hevc;
      std::optional<int> amd_preanalysis;
      std::optional<int> amd_vbaq;
      int amd_coder;
    } amd;

    struct {
      int vt_allow_sw;
      int vt_require_sw;
      int vt_realtime;
      int vt_coder;
    } vt;

    std::string capture;
    std::string encoder;
    std::string adapter_name;
    std::string output_name;
    //shawn: encoder2,adapter2_name,output2_name is used for second display support
    //also set haveSecondOutput to true,which indicates second display in using
    std::string encoder2;
    std::string adapter2_name;
    // (Shawn) Setting the default to true allows the client to easily 
    // stop video streaming and close the window.
    // In Sunshine, we only open the capture thread 
    // to wait for a receive ping, which doesn't add much cost.
    std::string output2_name = DIPS_PLACEHOLDER;
    bool haveSecondOutput = true;
    bool afterhaveSecondOutput;     //
    bool bChangeSecondOutputFlag;   //use is display count change flag
    int virtualDisplayCount;//how many virtual display need to be created
    bool dwmflush;
    std::string output_fullname;//name -- [left,top,right,bottom] format
    std::string output2_fullname;  //name -- [left,top,right,bottom] format
    std::string output_filter;//when do output name matching, excludes this list
  };

  struct audio_t {
    std::string sink;
    std::string virtual_sink;
  };

  struct stream_t {
    std::chrono::milliseconds ping_timeout;

    std::string file_apps;

    int fec_percentage;

    // max unique instances of video and audio streams
    int channels;
    std::string userName;
  };

  struct nvhttp_t {
    // Could be any of the following values:
    // pc|lan|wan
    std::string origin_pin_allowed;
    std::string origin_web_ui_allowed;

    std::string pkey;  // must be 2048 bits
    std::string cert;  // must be signed with a key of 2048 bits

    std::string sunshine_name;

    std::string file_state;

    std::string external_ip;
    std::vector<std::string> resolutions;
    std::vector<int> fps;
  };

  struct input_t {
    std::unordered_map<int, int> keybindings;

    std::chrono::milliseconds back_button_timeout;
    std::chrono::milliseconds key_repeat_delay;
    std::chrono::duration<double> key_repeat_period;

    std::string gamepad;

    bool keyboard;
    bool mouse;
    bool controller;
  };

  namespace flag {
    enum flag_e : std::size_t {
      PIN_STDIN = 0,  // Read PIN from stdin instead of http
      FRESH_STATE,  // Do not load or save state
      FORCE_VIDEO_HEADER_REPLACE,  // force replacing headers inside video data
      UPNP,  // Try Universal Plug 'n Play
      CONST_PIN,  // Use "universal" pin
      FLAG_SIZE
    };
  }

  struct prep_cmd_t {
    prep_cmd_t(std::string &&do_cmd, std::string &&undo_cmd):
        do_cmd(std::move(do_cmd)), undo_cmd(std::move(undo_cmd)) {}
    explicit prep_cmd_t(std::string &&do_cmd):
        do_cmd(std::move(do_cmd)) {}
    std::string do_cmd;
    std::string undo_cmd;
  };

  struct sunshine_t {
    int min_log_level;
    std::bitset<flag::FLAG_SIZE> flags;
    std::string credentials_file;

    std::string username;
    std::string password;
    std::string salt;

    std::string config_file;

    struct cmd_t {
      std::string name;
      int argc;
      char **argv;
    } cmd;

    std::uint16_t port;
    std::string log_file;

    std::vector<prep_cmd_t> prep_cmds;
    bool  privacy_mode;
  };

  extern video_t video;
  extern audio_t audio;
  extern stream_t stream;
  extern nvhttp_t nvhttp;
  extern input_t input;
  extern sunshine_t sunshine;

  int
  parse(int argc, char *argv[]);
  std::unordered_map<std::string, std::string>
  parse_config(const std::string_view &file_content);
  int
  archive_log_file(const char *path);
  int
  delete_unnecessary_log_files(const char *path);

  class LogFile {
  public:
    std::string name;
    time_t mtime;
  };

}  // namespace config
#endif
