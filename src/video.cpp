// Created by loki on 6/6/19.

#include <atomic>
#include <bitset>
#include <iostream>
#include <thread>

extern "C" {
#include <libavutil/mastering_display_metadata.h>
#include <libswscale/swscale.h>
}

#include "cbs.h"
#include "config.h"
#include "input.h"
#include "main.h"
#include "metrics.h"
#include "platform/common.h"
#include "round_robin.h"
#include "stream.h"
#include "sync.h"
#include "video.h"
#include "AuroraLib.h"
#include <chrono>

#ifdef _WIN32
extern "C" {
  #include <libavutil/hwcontext_d3d11va.h>
}
#endif

namespace stream {
extern int scanDisplays( std::vector<std::string> &displayNames,
                    std::vector<std::string> &virtualDisplayNames,
                    std::vector<std::string> &physicalDisplayNames,
                    int &numPhysical,
                    int &numVirtual );
//void cleanup_privacy_setup();
}

using namespace std::literals;
namespace video {
  void get_output_names(std::string& output_Name,std::string& output2_Name)
  {
    output_Name = config::video.output_name;
    output2_Name = config::video.output2_name;
  }
  void set_output_names(int displayId,std::string& name)
  {
    if( displayId ==1)
    {
      config::video.output_name = name;
    }
    else if(displayId ==2)
    {
      config::video.output2_name = name;
    }
  }
  //Display Changed Notifcation
  static std::mutex _display_changed_mutex;
  static bool g_display_changed = false;
  static std::vector<std::string> g_output_list;
  void set_display_changed(std::vector<std::string>& output_list)
  {
    _display_changed_mutex.lock();
    g_display_changed = true;
    g_output_list = output_list;
    _display_changed_mutex.unlock();
  }
  bool IsDisplay_changed(std::vector<std::string>& output_list)
  {
    bool bChanged = false;
    _display_changed_mutex.lock();
    bChanged = g_display_changed;
    if(bChanged)
    {
      output_list = g_output_list;
      g_display_changed = false;
    }
    _display_changed_mutex.unlock();
    return bChanged;
  }
  std::string GetOutputName(int displayIndex)
  {
    if(displayIndex ==1)
    {
      return config::video.output_name;
    }
    else if(displayIndex ==2)
    {
      return config::video.output2_name;
    }
    return "";
  }
  constexpr auto hevc_nalu = "\000\000\000\001("sv;
  constexpr auto h264_nalu = "\000\000\000\001e"sv;

  void
  free_ctx(AVCodecContext *ctx) {
    avcodec_free_context(&ctx);
  }

  void
  free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  void
  free_buffer(AVBufferRef *ref) {
    av_buffer_unref(&ref);
  }

  using ctx_t = util::safe_ptr<AVCodecContext, free_ctx>;
  using frame_t = util::safe_ptr<AVFrame, free_frame>;
  using buffer_t = util::safe_ptr<AVBufferRef, free_buffer>;
  using sws_t = util::safe_ptr<SwsContext, sws_freeContext>;
  using img_event_t = std::shared_ptr<safe::event_t<std::shared_ptr<platf::img_t>>>;

  namespace nv {

    enum class profile_h264_e : int {
      baseline,
      main,
      high,
      high_444p,
    };

    enum class profile_hevc_e : int {
      main,
      main_10,
      rext,
    };
  }  // namespace nv

  namespace qsv {

    enum class profile_h264_e : int {
      baseline = 66,
      main = 77,
      high = 100,
    };

    enum class profile_hevc_e : int {
      main = 1,
      main_10 = 2,
    };
  }  // namespace qsv

  platf::mem_type_e
  map_base_dev_type(AVHWDeviceType type);
  platf::pix_fmt_e
  map_pix_fmt(AVPixelFormat fmt);

  util::Either<buffer_t, int>
  dxgi_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx);
  util::Either<buffer_t, int>
  vaapi_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx);
  util::Either<buffer_t, int>
  cuda_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx);

  int
  hwframe_ctx(ctx_t &ctx, platf::hwdevice_t *hwdevice, buffer_t &hwdevice_ctx, AVPixelFormat format);

  //static std::thread videoInitThread;
  class swdevice_t: public platf::hwdevice_t {
  public:
    int
    convert(platf::img_t &img) override {
      av_frame_make_writable(sw_frame.get());

      const int linesizes[2] {
        img.row_pitch, 0
      };

      std::uint8_t *data[4];

      data[0] = sw_frame->data[0] + offsetY;
      if (sw_frame->format == AV_PIX_FMT_NV12) {
        data[1] = sw_frame->data[1] + offsetUV * 2;
        data[2] = nullptr;
      }
      else {
        data[1] = sw_frame->data[1] + offsetUV;
        data[2] = sw_frame->data[2] + offsetUV;
        data[3] = nullptr;
      }

      int ret = sws_scale(sws.get(), (std::uint8_t *const *) &img.data, linesizes, 0, img.height, data, sw_frame->linesize);
      if (ret <= 0) {
        BOOST_LOG(error) << "Couldn't convert image to required format and/or size"sv;

        return -1;
      }

      // If frame is not a software frame, it means we still need to transfer from main memory
      // to vram memory
      if (frame->hw_frames_ctx) {
        auto status = av_hwframe_transfer_data(frame, sw_frame.get(), 0);
        if (status < 0) {
          char string[AV_ERROR_MAX_STRING_SIZE];
          BOOST_LOG(error) << "Failed to transfer image data to hardware frame: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
          return -1;
        }
      }

      return 0;
    }

    int
    set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
      this->frame = frame;

      // If it's a hwframe, allocate buffers for hardware
      if (hw_frames_ctx) {
        hw_frame.reset(frame);

        if (av_hwframe_get_buffer(hw_frames_ctx, frame, 0)) return -1;
      }
      else {
        sw_frame.reset(frame);
      }

      return 0;
    }

    void
    set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
      sws_setColorspaceDetails(sws.get(),
        sws_getCoefficients(SWS_CS_DEFAULT), 0,
        sws_getCoefficients(colorspace), color_range - 1,
        0, 1 << 16, 1 << 16);
    }

    /**
   * When preserving aspect ratio, ensure that padding is black
   */
    int
    prefill() {
      auto frame = sw_frame ? sw_frame.get() : this->frame;
      auto width = frame->width;
      auto height = frame->height;

      av_frame_get_buffer(frame, 0);
      sws_t sws {
        sws_getContext(
          width, height, AV_PIX_FMT_BGR0,
          width, height, (AVPixelFormat) frame->format,
          SWS_LANCZOS | SWS_ACCURATE_RND,
          nullptr, nullptr, nullptr)
      };

      if (!sws) {
        return -1;
      }

      util::buffer_t<std::uint32_t> img { (std::size_t)(width * height) };
      std::fill(std::begin(img), std::end(img), 0);

      const int linesizes[2] {
        width, 0
      };

      av_frame_make_writable(frame);

      auto data = img.begin();
      int ret = sws_scale(sws.get(), (std::uint8_t *const *) &data, linesizes, 0, height, frame->data, frame->linesize);
      if (ret <= 0) {
        BOOST_LOG(error) << "Couldn't convert image to required format and/or size"sv;

        return -1;
      }

      return 0;
    }

    int
    init(int in_width, int in_height, AVFrame *frame, AVPixelFormat format, bool hardware) {
      // If the device used is hardware, yet the image resides on main memory
      if (hardware) {
        sw_frame.reset(av_frame_alloc());

        sw_frame->width = frame->width;
        sw_frame->height = frame->height;
        sw_frame->format = format;
      }
      else {
        this->frame = frame;
      }

      if (prefill()) {
        return -1;
      }

      auto out_width = frame->width;
      auto out_height = frame->height;

      // Ensure aspect ratio is maintained
      auto scalar = std::fminf((float) out_width / in_width, (float) out_height / in_height);
      out_width = in_width * scalar;
      out_height = in_height * scalar;

      // result is always positive
      auto offsetW = (frame->width - out_width) / 2;
      auto offsetH = (frame->height - out_height) / 2;
      offsetUV = (offsetW + offsetH * frame->width / 2) / 2;
      offsetY = offsetW + offsetH * frame->width;

      sws.reset(sws_getContext(
        in_width, in_height, AV_PIX_FMT_BGR0,
        out_width, out_height, format,
        SWS_LANCZOS | SWS_ACCURATE_RND,
        nullptr, nullptr, nullptr));

      return sws ? 0 : -1;
    }

    ~swdevice_t() override {}

    // Store ownership when frame is hw_frame
    frame_t hw_frame;

    frame_t sw_frame;
    sws_t sws;

    // offset of input image to output frame in pixels
    int offsetUV;
    int offsetY;
  };

  enum flag_e {
    DEFAULT = 0x00,
    PARALLEL_ENCODING = 0x01,
    H264_ONLY = 0x02,  // When HEVC is too heavy
    LIMITED_GOP_SIZE = 0x04,  // Some encoders don't like it when you have an infinite GOP_SIZE. *cough* VAAPI *cough*
    SINGLE_SLICE_ONLY = 0x08,  // Never use multiple slices <-- Older intel iGPU's ruin it for everyone else :P
    CBR_WITH_VBR = 0x10,  // Use a VBR rate control mode to simulate CBR
    RELAXED_COMPLIANCE = 0x20,  // Use FF_COMPLIANCE_UNOFFICIAL compliance mode
    NO_RC_BUF_LIMIT = 0x40,  // Don't set rc_buffer_size
  };

  struct encoder_t {
    std::string_view name;
    enum flag_e {
      PASSED,  // Is supported
      REF_FRAMES_RESTRICT,  // Set maximum reference frames
      REF_FRAMES_AUTOSELECT,  // Allow encoder to select maximum reference frames (If !REF_FRAMES_RESTRICT --> REF_FRAMES_AUTOSELECT)
      SLICE,  // Allow frame to be partitioned into multiple slices
      CBR,  // Some encoders don't support CBR, if not supported --> attempt constant quantatication parameter instead
      DYNAMIC_RANGE,  // hdr
      VUI_PARAMETERS,  // AMD encoder with VAAPI doesn't add VUI parameters to SPS
      NALU_PREFIX_5b,  // libx264/libx265 have a 3-byte nalu prefix instead of 4-byte nalu prefix
      MAX_FLAGS
    };

    static std::string_view
    from_flag(flag_e flag) {
#define _CONVERT(x) \
  case flag_e::x:   \
    return #x##sv
      switch (flag) {
        _CONVERT(PASSED);
        _CONVERT(REF_FRAMES_RESTRICT);
        _CONVERT(REF_FRAMES_AUTOSELECT);
        _CONVERT(SLICE);
        _CONVERT(CBR);
        _CONVERT(DYNAMIC_RANGE);
        _CONVERT(VUI_PARAMETERS);
        _CONVERT(NALU_PREFIX_5b);
        _CONVERT(MAX_FLAGS);
      }
#undef _CONVERT

      return "unknown"sv;
    }

    struct option_t {
      KITTY_DEFAULT_CONSTR_MOVE(option_t)
      option_t(const option_t &) = default;

      std::string name;
      std::variant<int, int *, std::optional<int> *, std::string, std::string *> value;

      option_t(std::string &&name, decltype(value) &&value):
          name { std::move(name) }, value { std::move(value) } {}
    };

    AVHWDeviceType base_dev_type, derived_dev_type;
    AVPixelFormat dev_pix_fmt;

    AVPixelFormat static_pix_fmt, dynamic_pix_fmt;

    struct {
      std::vector<option_t> common_options;
      std::vector<option_t> sdr_options;
      std::vector<option_t> hdr_options;
      std::optional<option_t> qp;

      std::string name;
      std::bitset<MAX_FLAGS> capabilities;

      bool
      operator[](flag_e flag) const {
        return capabilities[(std::size_t) flag];
      }

      std::bitset<MAX_FLAGS>::reference
      operator[](flag_e flag) {
        return capabilities[(std::size_t) flag];
      }
    } hevc, h264;

    int flags;

    std::function<util::Either<buffer_t, int>(platf::hwdevice_t *hwdevice)> make_hwdevice_ctx;
  };

  class session_t {
  public:
    session_t() = default;
    session_t(ctx_t &&ctx, std::shared_ptr<platf::hwdevice_t> &&device, int inject):
        ctx { std::move(ctx) }, device { std::move(device) }, inject { inject } {}

    session_t(session_t &&other) noexcept = default;
    ~session_t() {
      // Order matters here because the context relies on the hwdevice still being valid
      ctx.reset();
      device.reset();
    }

    // Ensure objects are destroyed in the correct order
    session_t &
    operator=(session_t &&other) {
      device = std::move(other.device);
      ctx = std::move(other.ctx);
      replacements = std::move(other.replacements);
      sps = std::move(other.sps);
      vps = std::move(other.vps);

      inject = other.inject;

      return *this;
    }

    ctx_t ctx;
    std::shared_ptr<platf::hwdevice_t> device;

    std::vector<packet_raw_t::replace_t> replacements;

    cbs::nal_t sps;
    cbs::nal_t vps;

    // inject sps/vps data into idr pictures
    int inject;
  };

  struct sync_session_ctx_t {
    safe::signal_t *join_event;
    safe::mail_raw_t::event_t<bool> shutdown_event;
    safe::mail_raw_t::queue_t<packet_t> packets;
    safe::mail_raw_t::event_t<bool> idr_events;
    safe::mail_raw_t::event_t<hdr_info_t> hdr_events;
    safe::mail_raw_t::event_t<input::touch_port_t> touch_port_events;

    config_t config;
    int frame_nr;
    void *channel_data;
  };

  struct sync_session_t {
    sync_session_ctx_t *ctx;

    platf::img_t *img_tmp;
    session_t session;
  };

  using encode_session_ctx_queue_t = safe::queue_t<sync_session_ctx_t>;
  using encode_e = platf::capture_e;

  struct capture_ctx_t {
    img_event_t images;
    config_t config;
  };

  struct capture_thread_async_ctx_t {
    std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue;
    std::thread capture_thread;

    safe::signal_t reinit_event;
    const encoder_t *encoder_p;
    sync_util::sync_t<std::weak_ptr<platf::display_t>> display_wp;
  };

  struct capture_thread_sync_ctx_t {
    encode_session_ctx_queue_t encode_session_ctx_queue { 30 };
  };

  int
  start_capture_sync(capture_thread_sync_ctx_t &ctx,char *data);
  void
  end_capture_sync(capture_thread_sync_ctx_t &ctx);
  int
  start_capture_async(capture_thread_async_ctx_t &ctx, char* data);
  int
  start_capture_async2(capture_thread_async_ctx_t &ctx, char* data);
  void
  end_capture_async(capture_thread_async_ctx_t &ctx);

  // Keep a reference counter to ensure the capture thread only runs when other threads have a reference to the capture thread
  auto capture_thread_async = safe::make_shared<capture_thread_async_ctx_t>(start_capture_async, end_capture_async);
  auto capture_thread_sync = safe::make_shared<capture_thread_sync_ctx_t>(start_capture_sync, end_capture_sync);
  //Shawn: add capture_thread_async2 to support second display
  auto capture_thread_async2 = safe::make_shared<capture_thread_async_ctx_t>(start_capture_async2, end_capture_async);

  static encoder_t nvenc {
    "nvenc"sv,
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_D3D11,
#else
    AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_CUDA,
#endif
    AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
    {
      // Common options
      {
        { "delay"s, 0 },
        { "forced-idr"s, 1 },
        { "zerolatency"s, 1 },
        { "preset"s, &config::video.nv.nv_preset },
        { "tune"s, &config::video.nv.nv_tune },
        { "rc"s, &config::video.nv.nv_rc },
      },
      // SDR-specific options
      {
        { "profile"s, (int) nv::profile_hevc_e::main },
      },
      // HDR-specific options
      {
        { "profile"s, (int) nv::profile_hevc_e::main_10 },
      },
      std::nullopt,
      "hevc_nvenc"s,
    },
    {
      {
        { "delay"s, 0 },
        { "forced-idr"s, 1 },
        { "zerolatency"s, 1 },
        { "preset"s, &config::video.nv.nv_preset },
        { "tune"s, &config::video.nv.nv_tune },
        { "rc"s, &config::video.nv.nv_rc },
        { "coder"s, &config::video.nv.nv_coder },
      },
      // SDR-specific options
      {
        { "profile"s, (int) nv::profile_h264_e::high },
      },
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "h264_nvenc"s,
    },
    PARALLEL_ENCODING,
#ifdef _WIN32
    dxgi_make_hwdevice_ctx
#else
    cuda_make_hwdevice_ctx
#endif
  };

#ifdef _WIN32
  static encoder_t quicksync {
    "quicksync"sv,
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_QSV,
    AV_PIX_FMT_QSV,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    {
      // Common options
      {
        { "preset"s, &config::video.qsv.qsv_preset },
        { "forced_idr"s, 1 },
        { "async_depth"s, 1 },
        { "low_delay_brc"s, 1 },
        { "low_power"s, 1 },
        { "recovery_point_sei"s, 0 },
        { "pic_timing_sei"s, 0 },
      },
      // SDR-specific options
      {
        { "profile"s, (int) qsv::profile_hevc_e::main },
      },
      // HDR-specific options
      {
        { "profile"s, (int) qsv::profile_hevc_e::main_10 },
      },
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "hevc_qsv"s,
    },
    {
      // Common options
      {
        { "preset"s, &config::video.qsv.qsv_preset },
        { "cavlc"s, &config::video.qsv.qsv_cavlc },
        { "forced_idr"s, 1 },
        { "async_depth"s, 1 },
        { "low_delay_brc"s, 1 },
        { "low_power"s, 1 },
        { "recovery_point_sei"s, 0 },
        { "vcm"s, 1 },
        { "pic_timing_sei"s, 0 },
        { "max_dec_frame_buffering"s, 1 },
      },
      // SDR-specific options
      {
        { "profile"s, (int) qsv::profile_h264_e::high },
      },
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp"s, &config::video.qp }),
      "h264_qsv"s,
    },
    PARALLEL_ENCODING | CBR_WITH_VBR | RELAXED_COMPLIANCE | NO_RC_BUF_LIMIT,
    dxgi_make_hwdevice_ctx,
  };

  static encoder_t amdvce {
    "amdvce"sv,
    AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_D3D11,
    AV_PIX_FMT_NV12, AV_PIX_FMT_P010,
    {
      // Common options
      {
        { "filler_data"s, true },
        { "gops_per_idr"s, 1 },
        { "header_insertion_mode"s, "idr"s },
        { "preanalysis"s, &config::video.amd.amd_preanalysis },
        { "qmax"s, 51 },
        { "qmin"s, 0 },
        { "quality"s, &config::video.amd.amd_quality_hevc },
        { "rc"s, &config::video.amd.amd_rc_hevc },
        { "usage"s, &config::video.amd.amd_usage_hevc },
        { "vbaq"s, &config::video.amd.amd_vbaq },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
      "hevc_amf"s,
    },
    {
      // Common options
      {
        { "filler_data"s, true },
        { "log_to_dbg"s, "1"s },
        { "preanalysis"s, &config::video.amd.amd_preanalysis },
        { "qmax"s, 51 },
        { "qmin"s, 0 },
        { "quality"s, &config::video.amd.amd_quality_h264 },
        { "rc"s, &config::video.amd.amd_rc_h264 },
        { "usage"s, &config::video.amd.amd_usage_h264 },
        { "vbaq"s, &config::video.amd.amd_vbaq },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>({ "qp_p"s, &config::video.qp }),
      "h264_amf"s,
    },
    PARALLEL_ENCODING,
    dxgi_make_hwdevice_ctx
  };
#endif

  static encoder_t software {
    "software"sv,
    AV_HWDEVICE_TYPE_NONE, AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_NONE,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10,
    {
      // x265's Info SEI is so long that it causes the IDR picture data to be
      // kicked to the 2nd packet in the frame, breaking Moonlight's parsing logic.
      // It also looks like gop_size isn't passed on to x265, so we have to set
      // 'keyint=-1' in the parameters ourselves.
      {
        { "forced-idr"s, 1 },
        { "x265-params"s, "info=0:keyint=-1"s },
        { "preset"s, &config::video.sw.sw_preset },
        { "tune"s, &config::video.sw.sw_tune },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "libx265"s,
    },
    {
      // Common options
      {
        { "preset"s, &config::video.sw.sw_preset },
        { "tune"s, &config::video.sw.sw_tune },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "libx264"s,
    },
    H264_ONLY | PARALLEL_ENCODING,

    nullptr
  };

#ifdef __linux__
  static encoder_t vaapi {
    "vaapi"sv,
    AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_VAAPI,
    AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P10,
    {
      // Common options
      {
        { "async_depth"s, 1 },
        { "sei"s, 0 },
        { "idr_interval"s, std::numeric_limits<int>::max() },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "hevc_vaapi"s,
    },
    {
      // Common options
      {
        { "async_depth"s, 1 },
        { "sei"s, 0 },
        { "idr_interval"s, std::numeric_limits<int>::max() },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::make_optional<encoder_t::option_t>("qp"s, &config::video.qp),
      "h264_vaapi"s,
    },
    LIMITED_GOP_SIZE | PARALLEL_ENCODING | SINGLE_SLICE_ONLY,

    vaapi_make_hwdevice_ctx
  };
#endif

#ifdef __APPLE__
  static encoder_t videotoolbox {
    "videotoolbox"sv,
    AV_HWDEVICE_TYPE_NONE, AV_HWDEVICE_TYPE_NONE,
    AV_PIX_FMT_VIDEOTOOLBOX,
    AV_PIX_FMT_NV12, AV_PIX_FMT_NV12,
    {
      // Common options
      {
        { "allow_sw"s, &config::video.vt.vt_allow_sw },
        { "require_sw"s, &config::video.vt.vt_require_sw },
        { "realtime"s, &config::video.vt.vt_realtime },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::nullopt,
      "hevc_videotoolbox"s,
    },
    {
      // Common options
      {
        { "allow_sw"s, &config::video.vt.vt_allow_sw },
        { "require_sw"s, &config::video.vt.vt_require_sw },
        { "realtime"s, &config::video.vt.vt_realtime },
      },
      {},  // SDR-specific options
      {},  // HDR-specific options
      std::nullopt,
      "h264_videotoolbox"s,
    },
    DEFAULT,

    nullptr
  };
#endif

  static std::vector<encoder_t> encoders {
#ifndef __APPLE__
    nvenc,
#endif
#ifdef _WIN32
    quicksync,
    amdvce,
#endif
#ifdef __linux__
    vaapi,
#endif
#ifdef __APPLE__
    videotoolbox,
#endif
    software
  };
  static std::vector<encoder_t> encoders_backup = encoders;
  static std::vector<encoder_t> encoders2 = encoders;
  static std::vector<encoder_t> encoders2_backup = encoders2;
  void
  reset_display(std::shared_ptr<platf::display_t> &disp, AVHWDeviceType type, 
    int displayId,
    const std::string &display_name, const config_t &config) {
    // We try this twice, in case we still get an error on reinitialization
    for (int x = 0; x < 2; ++x) {
      disp.reset();
      disp = platf::display(map_base_dev_type(type),displayId,display_name, config);
      if (disp) {
        break;
      }

      // The capture code depends on us to sleep between failures
      std::this_thread::sleep_for(200ms);
    }
  }

  std::mutex fps_mutex;
  static double g_fps[2] = { 0, 0 };
  std::mutex fps_mutex_encode;
  static double g_fps_encode[2] = { 0, 0 };
  std::mutex frames_mutex;
  static long int g_frames[2] = { 0, 0 };
  std::mutex elapsed_time_mutex;
  static double g_elapsed_time = 0.0;  // In seconds

  static void
  calc_fps_capture(long long num_of_frames, std::chrono::time_point<std::chrono::high_resolution_clock> t_start, int displayIndex) {
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    if (elapsed_time_ms != 0) {
      //double fps_sample = (double) (num_of_frames / elapsed_time_ms) / 100.0;
      double fps_sample = (double) (num_of_frames / elapsed_time_ms) * 1000.0;
      fps_mutex.lock();
      g_fps[displayIndex - 1] = fps_sample;
      //BOOST_LOG(info) << "*************calc_fps_capture: index = "<< displayIndex <<", num_of_frames = " << num_of_frames << ", fps = " << g_fps[displayIndex-1] << std::endl;
      fps_mutex.unlock();
    }
  }

  static void
  calc_fps(long long num_of_frames, std::chrono::time_point<std::chrono::high_resolution_clock> t_start, int displayIndex) {
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    if (elapsed_time_ms != 0) {
      //double fps_sample = (double) (num_of_frames / elapsed_time_ms) / 100.0;
      double fps_sample = (double) (num_of_frames / elapsed_time_ms) * 1000.0;
      fps_mutex_encode.lock();
      g_fps_encode[displayIndex - 1] = fps_sample;
      //BOOST_LOG(info) << "*************calc_fps: index = "<< displayIndex <<", num_of_frames = " << num_of_frames << ", fps = " << g_fps_encode[displayIndex-1] << std::endl;
      fps_mutex_encode.unlock();
    }
  }

  static void
  calc_frames(long long num_of_frames, int displayIndex) {
    frames_mutex.lock();
    g_frames[displayIndex - 1] = num_of_frames;
    frames_mutex.unlock();
  }

  static void
  calc_elapsed_time(std::chrono::time_point<std::chrono::high_resolution_clock> t_start) {
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    elapsed_time_mutex.lock();
    g_elapsed_time = elapsed_time_ms / 1000.0;  //in seconds
    elapsed_time_mutex.unlock();
  }

  void
  captureThread(
    std::shared_ptr<safe::queue_t<capture_ctx_t>> capture_ctx_queue,
    sync_util::sync_t<std::weak_ptr<platf::display_t>> &display_wp,
    safe::signal_t &reinit_event,
    const encoder_t &encoder,
    int displayIndex) {
    BOOST_LOG(info) << "CaptureThread Started for Display:"sv << displayIndex;

    //videoInitThread.join();

    std::vector<capture_ctx_t> capture_ctxs;
    //(Shawn) if displayIndex is 2, output2_name can't be empty, if empty, means no dual display support
    //just return and finish this thread
    if (displayIndex == 2 && config::video.output2_name.empty()) {
      return;
    }
    std::string outputName = (displayIndex == 1) ? config::video.output_name : config::video.output2_name;
    auto fg = util::fail_guard([&]() {
      capture_ctx_queue->stop();

      // Stop all sessions listening to this thread
      for (auto &capture_ctx : capture_ctxs) {
        capture_ctx.images->stop();
      }
      for (auto &capture_ctx : capture_ctx_queue->unsafe()) {
        capture_ctx.images->stop();
      }
      MetricsMgr().reset_metrics();

      //if (config::sunshine.privacy_mode) {
      //  bool isConfigLoaded = AuroraMgr().loadDisplayConfig((char *)physical_display_data_file.c_str());
      //  if (!isConfigLoaded) {
      //    BOOST_LOG(info) << "*********** can not load display data from file " << std::endl;
      //  }
      //}

      //if (config::sunshine.privacy_mode) {
      //  BOOST_LOG(info) << "in captureThread, calling cleanup_privacy_setup";
      //  stream::cleanup_privacy_setup();
      //}

    });

    auto switch_display_event = mail::man->event<int>(mail::switch_display);

    // Get all the monitor names now, rather than at boot, to
    // get the most up-to-date list available monitors
    auto display_names = platf::display_names(map_base_dev_type(encoder.base_dev_type));
    int display_p = 0;

    if (display_names.empty()) {
      display_names.emplace_back(outputName);
    }

    for (int x = 0; x < display_names.size(); ++x) {
      if (display_names[x] == outputName) {
        display_p = x;

        break;
      }
    }

    if (auto capture_ctx = capture_ctx_queue->pop()) {
      capture_ctxs.emplace_back(std::move(*capture_ctx));
    }
    stream::session::Inc_Unsafe_stop();
    auto disp = platf::display(map_base_dev_type(encoder.base_dev_type), 
        displayIndex,outputName, capture_ctxs.front().config);
    stream::session::Dec_Unsafe_stop();
    if (!disp) {
      return;
    }
    display_wp = disp;

    std::vector<std::shared_ptr<platf::img_t>> imgs(12);
    auto round_robin = round_robin_util::make_round_robin<std::shared_ptr<platf::img_t>>(std::begin(imgs), std::end(imgs));

    for (auto &img : imgs) {
      img = disp->alloc_img();
      if (!img) {
        BOOST_LOG(error) << "Couldn't initialize an image"sv;
        return;
      }
    }

    // Capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::critical);
    if (displayIndex == 1) {
      MetricsMgr().register_metrics("FPS", 0, 1, [&]() {
        frames_mutex.lock();
        unsigned long long fps = (unsigned long long) (g_fps[0]);
        frames_mutex.unlock();
        return fps;
      }, 
      []() {
        frames_mutex.lock();
        g_fps[0] = 0.0;
        frames_mutex.unlock();
      }
      );
    }
    else if (displayIndex == 2) {
      MetricsMgr().register_metrics("FPS2", 0, 2, [&]() {
        frames_mutex.lock();
        unsigned long long fps1 = (unsigned long long) (g_fps[1]);
        frames_mutex.unlock();
        return fps1;
      },
      []() {
        frames_mutex.lock();
        g_fps[1] = 0.0;
        frames_mutex.unlock();
      }
      );
    }
    long long num_of_frames = 0L;
    auto t_start = std::chrono::high_resolution_clock::now();

    while (capture_ctx_queue->running()) {
      bool artificial_reinit = false;

      auto status = disp->capture([&](std::shared_ptr<platf::img_t> &img, bool frame_captured) -> std::shared_ptr<platf::img_t> {
        //BOOST_LOG(info) <<"Capture Callback:"<<frame_captured<<std::endl;
        auto capture_ctx = std::begin(capture_ctxs);
        while(capture_ctx != std::end(capture_ctxs)){
          if (!capture_ctx->images->running()) {
            capture_ctx = capture_ctxs.erase(capture_ctx);
            continue;
          }
          if (frame_captured) {
            //BOOST_LOG(info) <<"capture_ctx->images->raise(img)"<<std::endl;
            capture_ctx->images->raise(img);
          }
          num_of_frames++;
          calc_fps_capture(num_of_frames, t_start, displayIndex);
          ++capture_ctx;
        }

        if (!capture_ctx_queue->running()) {
          return nullptr;
        }
        while (capture_ctx_queue->peek()) {
          capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));
        }

        if (switch_display_event->peek()) {
          artificial_reinit = true;

          display_p = std::clamp(*switch_display_event->pop(), 0, (int) display_names.size() - 1);
          return nullptr;
        }

        auto &next_img = *round_robin++;
        while (next_img.use_count() > 1) {
          // Sleep a bit to avoid starving the encoder threads
          std::this_thread::sleep_for(2ms);
        }

        return next_img;
      },
        *round_robin++, &display_cursor);

      if (artificial_reinit && status != platf::capture_e::error) {
        status = platf::capture_e::reinit;

        artificial_reinit = false;
      }

      switch (status) {
        case platf::capture_e::reinit: {
          reinit_event.raise(true);

          // Some classes of images contain references to the display --> display won't delete unless img is deleted
          for (auto &img : imgs) {
            img.reset();
          }

          // display_wp is modified in this thread only
          // Wait for the other shared_ptr's of display to be destroyed.
          // New displays will only be created in this thread.
          while (display_wp->use_count() != 1) {
            // Free images that weren't consumed by the encoders. These can reference the display and prevent
            // the ref count from reaching 1. We do this here rather than on the encoder thread to avoid race
            // conditions where the encoding loop might free a good frame after reinitializing if we capture
            // a new frame here before the encoder has finished reinitializing.
            KITTY_WHILE_LOOP(auto capture_ctx = std::begin(capture_ctxs), capture_ctx != std::end(capture_ctxs), {
              if (!capture_ctx->images->running()) {
                capture_ctx = capture_ctxs.erase(capture_ctx);
                continue;
              }

              while (capture_ctx->images->peek()) {
                capture_ctx->images->pop();
              }

              ++capture_ctx;
            });

            std::this_thread::sleep_for(20ms);
          }

          while (capture_ctx_queue->running()) {
            // reset_display() will sleep between retries
            outputName = (displayIndex == 1) ? config::video.output_name : config::video.output2_name;
            reset_display(disp, encoder.base_dev_type,displayIndex, outputName, capture_ctxs.front().config);
            if (disp) {
              break;
            }
          }
          if (!disp) {
            return;
          }

          display_wp = disp;

          // Re-allocate images
          for (auto &img : imgs) {
            img = disp->alloc_img();
            if (!img) {
              BOOST_LOG(error) << "Couldn't initialize an image"sv;
              return;
            }
          }

          reinit_event.reset();
          continue;
        }
        case platf::capture_e::error:
        case platf::capture_e::ok:
        case platf::capture_e::timeout:
          return;
        default:
          BOOST_LOG(error) << "Unrecognized capture status ["sv << (int) status << ']';
          return;
      }
    }  // while
  }

  int
  encode(int displayIndex, int64_t frame_nr, session_t &session, frame_t::pointer frame,
    safe::mail_raw_t::queue_t<packet_t> &packets, void *channel_data) {
    frame->pts = frame_nr;

    auto &ctx = session.ctx;

    auto &sps = session.sps;
    auto &vps = session.vps;

    /* send the frame to the encoder */
    auto ret = avcodec_send_frame(ctx.get(), frame);
    if (ret < 0) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
      BOOST_LOG(error) << "Could not send a frame for encoding: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, ret);

      return -1;
    }

    while (ret >= 0) {
      auto packet = std::make_unique<packet_t::element_type>(nullptr);
      auto av_packet = packet.get()->av_packet;

      ret = avcodec_receive_packet(ctx.get(), av_packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
      }
      else if (ret < 0) {
        return ret;
      }

      if (session.inject) {
        if (session.inject == 1) {
          auto h264 = cbs::make_sps_h264(ctx.get(), av_packet);

          sps = std::move(h264.sps);
        }
        else {
          auto hevc = cbs::make_sps_hevc(ctx.get(), av_packet);

          sps = std::move(hevc.sps);
          vps = std::move(hevc.vps);

          session.replacements.emplace_back(
            std::string_view((char *) std::begin(vps.old), vps.old.size()),
            std::string_view((char *) std::begin(vps._new), vps._new.size()));
        }

        session.inject = 0;

        session.replacements.emplace_back(
          std::string_view((char *) std::begin(sps.old), sps.old.size()),
          std::string_view((char *) std::begin(sps._new), sps._new.size()));
      }

      packet->replacements = &session.replacements;
      packet->channel_data = channel_data;
      packet->displayIndex = displayIndex;
      packet->frameNo = frame_nr;
      //BOOST_LOG(info) << "Encode send out packet for displayIndex=" << displayIndex << ",frame:" << frame_nr << std::endl;
      packets->raise(std::move(packet));
    }

    return 0;
  }

  std::optional<session_t>
  make_session(platf::display_t *disp, const encoder_t &encoder, const config_t &config, int width, int height, std::shared_ptr<platf::hwdevice_t> &&hwdevice) {
    bool hardware = encoder.base_dev_type != AV_HWDEVICE_TYPE_NONE;

    auto &video_format = config.videoFormat == 0 ? encoder.h264 : encoder.hevc;
    if (!video_format[encoder_t::PASSED]) {
      BOOST_LOG(error) << encoder.name << ": "sv << video_format.name << " mode not supported"sv;
      return std::nullopt;
    }

    if (config.dynamicRange && !video_format[encoder_t::DYNAMIC_RANGE]) {
      BOOST_LOG(error) << video_format.name << ": dynamic range not supported"sv;
      return std::nullopt;
    }

    auto codec = avcodec_find_encoder_by_name(video_format.name.c_str());
    if (!codec) {
      BOOST_LOG(error) << "Couldn't open ["sv << video_format.name << ']';

      return std::nullopt;
    }

    ctx_t ctx { avcodec_alloc_context3(codec) };
    ctx->width = config.width;
    ctx->height = config.height;
    ctx->time_base = AVRational { 1, config.framerate };
    ctx->framerate = AVRational { config.framerate, 1 };

    if (config.videoFormat == 0) {
      ctx->profile = FF_PROFILE_H264_HIGH;
    }
    else if (config.dynamicRange == 0) {
      ctx->profile = FF_PROFILE_HEVC_MAIN;
    }
    else {
      ctx->profile = FF_PROFILE_HEVC_MAIN_10;
    }

    // B-frames delay decoder output, so never use them
    ctx->max_b_frames = 0;

    // Use an infinite GOP length since I-frames are generated on demand
    ctx->gop_size = encoder.flags & LIMITED_GOP_SIZE ?
                      std::numeric_limits<std::int16_t>::max() :
                      std::numeric_limits<int>::max();

    ctx->keyint_min = std::numeric_limits<int>::max();

    if (config.numRefFrames == 0) {
      ctx->refs = video_format[encoder_t::REF_FRAMES_AUTOSELECT] ? 0 : 16;
    }
    else {
      // Some client decoders have limits on the number of reference frames
      ctx->refs = video_format[encoder_t::REF_FRAMES_RESTRICT] ? config.numRefFrames : 0;
    }

    ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
    ctx->flags2 |= AV_CODEC_FLAG2_FAST;

    ctx->color_range = (config.encoderCscMode & 0x1) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    int sws_color_space;
    if (config.dynamicRange && disp->is_hdr()) {
      // When HDR is active, that overrides the colorspace the client requested
      BOOST_LOG(info) << "HDR color coding [Rec. 2020 + SMPTE 2084 PQ]"sv;
      ctx->color_primaries = AVCOL_PRI_BT2020;
      ctx->color_trc = AVCOL_TRC_SMPTE2084;
      ctx->colorspace = AVCOL_SPC_BT2020_NCL;
      sws_color_space = SWS_CS_BT2020;
    }
    else {
      switch (config.encoderCscMode >> 1) {
        case 0:
        default:
          // Rec. 601
          BOOST_LOG(info) << "SDR color coding [Rec. 601]"sv;
          ctx->color_primaries = AVCOL_PRI_SMPTE170M;
          ctx->color_trc = AVCOL_TRC_SMPTE170M;
          ctx->colorspace = AVCOL_SPC_SMPTE170M;
          sws_color_space = SWS_CS_SMPTE170M;
          break;

        case 1:
          // Rec. 709
          BOOST_LOG(info) << "SDR color coding [Rec. 709]"sv;
          ctx->color_primaries = AVCOL_PRI_BT709;
          ctx->color_trc = AVCOL_TRC_BT709;
          ctx->colorspace = AVCOL_SPC_BT709;
          sws_color_space = SWS_CS_ITU709;
          break;

        case 2:
          // Rec. 2020
          BOOST_LOG(info) << "SDR color coding [Rec. 2020]"sv;
          ctx->color_primaries = AVCOL_PRI_BT2020;
          ctx->color_trc = AVCOL_TRC_BT2020_10;
          ctx->colorspace = AVCOL_SPC_BT2020_NCL;
          sws_color_space = SWS_CS_BT2020;
          break;
      }
    }

    BOOST_LOG(info) << "Color range: ["sv << ((config.encoderCscMode & 0x1) ? "JPEG"sv : "MPEG"sv) << ']';

    AVPixelFormat sw_fmt;
    if (config.dynamicRange == 0) {
      sw_fmt = encoder.static_pix_fmt;
    }
    else {
      sw_fmt = encoder.dynamic_pix_fmt;
    }

    // Used by cbs::make_sps_hevc
    ctx->sw_pix_fmt = sw_fmt;

    if (hardware) {
      buffer_t hwdevice_ctx;

      ctx->pix_fmt = encoder.dev_pix_fmt;

      // Create the base hwdevice context
      //auto devPtr = hwdevice.get();
      //if ( devPtr == nullptr || devPtr->data == nullptr )     
      //{
      //  return std::nullopt;
      //}

      auto buf_or_error = encoder.make_hwdevice_ctx(hwdevice.get());
      if (buf_or_error.has_right()) {
        return std::nullopt;
      }
      hwdevice_ctx = std::move(buf_or_error.left());

      // If this encoder requires derivation from the base, derive the desired type
      if (encoder.derived_dev_type != AV_HWDEVICE_TYPE_NONE) {
        buffer_t derived_hwdevice_ctx;

        // Allow the hwdevice to prepare for this type of context to be derived
        if (hwdevice->prepare_to_derive_context(encoder.derived_dev_type)) {
          return std::nullopt;
        }

        auto err = av_hwdevice_ctx_create_derived(&derived_hwdevice_ctx, encoder.derived_dev_type, hwdevice_ctx.get(), 0);
        if (err) {
          char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
          BOOST_LOG(error) << "Failed to derive device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

          return std::nullopt;
        }

        hwdevice_ctx = std::move(derived_hwdevice_ctx);
      }

      if (hwframe_ctx(ctx, hwdevice.get(), hwdevice_ctx, sw_fmt)) {
        return std::nullopt;
      }

      ctx->slices = config.slicesPerFrame;
    }
    else /* software */ {
      ctx->pix_fmt = sw_fmt;

      // Clients will request for the fewest slices per frame to get the
      // most efficient encode, but we may want to provide more slices than
      // requested to ensure we have enough parallelism for good performance.
      ctx->slices = std::max(config.slicesPerFrame, config::video.min_threads);
    }

    if (!video_format[encoder_t::SLICE]) {
      ctx->slices = 1;
    }

    ctx->thread_type = FF_THREAD_SLICE;
    ctx->thread_count = ctx->slices;

    AVDictionary *options { nullptr };
    auto handle_option = [&options](const encoder_t::option_t &option) {
      std::visit(
        util::overloaded {
          [&](int v) { av_dict_set_int(&options, option.name.c_str(), v, 0); },
          [&](int *v) { av_dict_set_int(&options, option.name.c_str(), *v, 0); },
          [&](std::optional<int> *v) { if(*v) av_dict_set_int(&options, option.name.c_str(), **v, 0); },
          [&](const std::string &v) { av_dict_set(&options, option.name.c_str(), v.c_str(), 0); },
          [&](std::string *v) { if(!v->empty()) av_dict_set(&options, option.name.c_str(), v->c_str(), 0); } },
        option.value);
    };

    // Apply common options, then format-specific overrides
    for (auto &option : video_format.common_options) {
      handle_option(option);
    }
    for (auto &option : (config.dynamicRange ? video_format.hdr_options : video_format.sdr_options)) {
      handle_option(option);
    }

    if (video_format[encoder_t::CBR]) {
      auto bitrate = config.bitrate * 1000;
      ctx->rc_max_rate = bitrate;
      ctx->bit_rate = bitrate;

      if (encoder.flags & CBR_WITH_VBR) {
        // Ensure rc_max_bitrate != bit_rate to force VBR mode
        ctx->bit_rate--;
      }
      else {
        ctx->rc_min_rate = bitrate;
      }

      if (encoder.flags & RELAXED_COMPLIANCE) {
        ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
      }

      if (!(encoder.flags & NO_RC_BUF_LIMIT)) {
        if (!hardware && (ctx->slices > 1 || config.videoFormat != 0)) {
          // Use a larger rc_buffer_size for software encoding when slices are enabled,
          // because libx264 can severely degrade quality if the buffer is too small.
          // libx265 encounters this issue more frequently, so always scale the
          // buffer by 1.5x for software HEVC encoding.
          ctx->rc_buffer_size = bitrate / ((config.framerate * 10) / 15);
        }
        else {
          ctx->rc_buffer_size = bitrate / config.framerate;
        }
      }
    }
    else if (video_format.qp) {
      handle_option(*video_format.qp);
    }
    else {
      BOOST_LOG(error) << "Couldn't set video quality: encoder "sv << encoder.name << " doesn't support qp"sv;
      return std::nullopt;
    }

    if (auto status = avcodec_open2(ctx.get(), codec, &options)) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
      BOOST_LOG(error)
        << "Could not open codec ["sv
        << video_format.name << "]: "sv
        << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, status);

      return std::nullopt;
    }

    frame_t frame { av_frame_alloc() };
    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;

    // Attach HDR metadata to the AVFrame
    if (config.dynamicRange && disp->is_hdr()) {
      SS_HDR_METADATA hdr_metadata;
      if (disp->get_hdr_metadata(hdr_metadata)) {
        auto mdm = av_mastering_display_metadata_create_side_data(frame.get());

        mdm->display_primaries[0][0] = av_make_q(hdr_metadata.displayPrimaries[0].x, 50000);
        mdm->display_primaries[0][1] = av_make_q(hdr_metadata.displayPrimaries[0].y, 50000);
        mdm->display_primaries[1][0] = av_make_q(hdr_metadata.displayPrimaries[1].x, 50000);
        mdm->display_primaries[1][1] = av_make_q(hdr_metadata.displayPrimaries[1].y, 50000);
        mdm->display_primaries[2][0] = av_make_q(hdr_metadata.displayPrimaries[2].x, 50000);
        mdm->display_primaries[2][1] = av_make_q(hdr_metadata.displayPrimaries[2].y, 50000);

        mdm->white_point[0] = av_make_q(hdr_metadata.whitePoint.x, 50000);
        mdm->white_point[1] = av_make_q(hdr_metadata.whitePoint.y, 50000);

        mdm->min_luminance = av_make_q(hdr_metadata.minDisplayLuminance, 10000);
        mdm->max_luminance = av_make_q(hdr_metadata.maxDisplayLuminance, 1);

        mdm->has_luminance = hdr_metadata.maxDisplayLuminance != 0 ? 1 : 0;
        mdm->has_primaries = hdr_metadata.displayPrimaries[0].x != 0 ? 1 : 0;

        if (hdr_metadata.maxContentLightLevel != 0 || hdr_metadata.maxFrameAverageLightLevel != 0) {
          auto clm = av_content_light_metadata_create_side_data(frame.get());

          clm->MaxCLL = hdr_metadata.maxContentLightLevel;
          clm->MaxFALL = hdr_metadata.maxFrameAverageLightLevel;
        }
      }
    }

    std::shared_ptr<platf::hwdevice_t> device;

    if (!hwdevice->data) {
      auto device_tmp = std::make_unique<swdevice_t>();

      if (device_tmp->init(width, height, frame.get(), sw_fmt, hardware)) {
        return std::nullopt;
      }

      device = std::move(device_tmp);
    }
    else {
      device = std::move(hwdevice);
    }

    if (device->set_frame(frame.release(), ctx->hw_frames_ctx)) {
      return std::nullopt;
    }

    device->set_colorspace(sws_color_space, ctx->color_range);

    session_t session {
      std::move(ctx),
      std::move(device),

      // 0 ==> don't inject, 1 ==> inject for h264, 2 ==> inject for hevc
      (1 - (int) video_format[encoder_t::VUI_PARAMETERS]) * (1 + config.videoFormat),
    };

    if (!video_format[encoder_t::NALU_PREFIX_5b]) {
      auto nalu_prefix = config.videoFormat ? hevc_nalu : h264_nalu;

      session.replacements.emplace_back(nalu_prefix.substr(1), nalu_prefix);
    }

    return std::make_optional(std::move(session));
  }

  void
  encode_run(
    int displayIndex,
    int &frame_nr,  // Store progress of the frame number
    safe::mail_t mail,
    img_event_t images,
    config_t config,
    std::shared_ptr<platf::display_t> disp,
    std::shared_ptr<platf::hwdevice_t> &&hwdevice,
    safe::signal_t &reinit_event,
    const encoder_t &encoder,
    void *channel_data) {
    //BOOST_LOG(info) << "*************encode_run:" << std::endl;
    stream::session::Inc_Unsafe_stop();
    auto session = make_session(disp.get(), encoder, config, disp->width, disp->height, std::move(hwdevice));
    stream::session::Dec_Unsafe_stop();
    if (!session) {
      return;
    }
    //BOOST_LOG(info) << "*************encode_run: after make_session:displayIndex =" << displayIndex << std::endl;

    auto frame = session->device->frame;

    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = (displayIndex == 1) ? mail::man->queue<packet_t>(mail::video_packets) :
                                         mail::man->queue<packet_t>(mail::video_packets2);
    auto idr_events = (displayIndex == 1) ? mail->event<bool>(mail::idr) : mail->event<bool>(mail::idr2);

    // Load a dummy image into the AVFrame to ensure we have something to encode
    // even if we timeout waiting on the first frame.
    auto dummy_img = disp->alloc_img();
    if (!dummy_img || disp->dummy_img(dummy_img.get()) || session->device->convert(*dummy_img)) {
      return;
    }
    //BOOST_LOG(info) << "*************encode_run: after dummy_img:displayIndex =" << displayIndex << std::endl;
    if (displayIndex == 1) {
      MetricsMgr().register_metrics("FPS_ENCODE", 0, 3, [&]() {
        frames_mutex.lock();
        unsigned long long fps = (unsigned long long) (g_fps_encode[0]);
        frames_mutex.unlock();
        return fps;
      },
      []() {
        frames_mutex.lock();
        g_fps_encode[0] = 0.0;
        frames_mutex.unlock();
      }
      );
      MetricsMgr().register_metrics("NUMBER_OF_FRAMES", -1, -1, [&]() {
        frames_mutex.lock();
        long int frm = g_frames[0];
        frames_mutex.unlock();
        return frm;

      },
      []() {
        frames_mutex.lock();
        g_frames[0] = 0;
        frames_mutex.unlock();
      }
    );
    }
    else if (displayIndex == 2) {
      MetricsMgr().register_metrics("FPS_ENCODE2", 0, 4, [&]() {
        frames_mutex.lock();
        unsigned long long fps1 = (unsigned long long) (g_fps_encode[1]);
        frames_mutex.unlock();
        return fps1;
      },
      []() {
        frames_mutex.lock();
        g_fps_encode[1] = 0.0;
        frames_mutex.unlock();
      }
      );
      MetricsMgr().register_metrics("NUMBER_OF_FRAMES2", -1, -1, [&]() {
        frames_mutex.lock();
        long int frm1 = g_frames[1];
        frames_mutex.unlock();
        return frm1;
      },
      []() {
        frames_mutex.lock();
        g_frames[1] = 0;
        frames_mutex.unlock();
      }
      );
    }
    MetricsMgr().register_metrics("ELAPSED_TIME", -1, -1, [&]() {
      frames_mutex.lock();
      unsigned long long elapsed_time = (unsigned long long) (g_elapsed_time * 100);
      frames_mutex.unlock();
      return elapsed_time;
    },
    []() {
      frames_mutex.lock();
      g_elapsed_time = 0.0;
      frames_mutex.unlock();
    }
    );

    long long num_of_frames = 0L;
    auto t_start = std::chrono::high_resolution_clock::now();

    //BOOST_LOG(info) << "*************encode_run: before while loop:displayIndex =" << displayIndex << std::endl;
    while (true) {
      if (shutdown_event->peek() || reinit_event.peek() || !images->running()) {
        //BOOST_LOG(info) << "*************encode_run: in while loop & break:displayIndex =" << displayIndex << std::endl;
        break;
      }

      if (idr_events->peek()) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->key_frame = 1;

        idr_events->pop();
      }

      // Encode at a minimum of 10 FPS to avoid image quality issues with static content
      if (!frame->key_frame || images->peek()) {
        //BOOST_LOG(info) <<"!frame->key_frame || images->peek" <<std::endl;
        if (auto img = images->pop(100ms)) {
          //BOOST_LOG(info) <<"auto img = images->pop(100ms)" <<std::endl;
          if (session->device->convert(*img)) {
            BOOST_LOG(error) << "Could not convert image"sv;
            //BOOST_LOG(info) << "*************encode_run: in while loop & break2:displayIndex =" << displayIndex << std::endl;
            return;
          }
        }
        else if (!images->running()) {
          //BOOST_LOG(info) << "*************encode_run: in while loop & break3:displayIndex =" << displayIndex << std::endl;
          break;
        }
      }

      //BOOST_LOG(info) << "*************encode_run: in while loop:displayIndex =" << displayIndex << std::endl;
      num_of_frames++;
      calc_fps(num_of_frames, t_start, displayIndex);
      calc_frames(num_of_frames, displayIndex);
      calc_elapsed_time(t_start);

      //BOOST_LOG(info) << "encode:" << displayIndex << ",frame="<<(unsigned long long)frame <<",No:"<<frame_nr << std::endl;
      if (encode(displayIndex, frame_nr++, *session, frame, packets, channel_data)) {
        BOOST_LOG(error) << "Could not encode video packet"sv;
        //BOOST_LOG(info) << "*************encode_run: in while loop & break4:displayIndex =" << displayIndex << std::endl;
        return;
      }
      //BOOST_LOG(info) << "after encode:" << displayIndex << ",frame=" <<frame_nr << std::endl;
      frame->pict_type = AV_PICTURE_TYPE_NONE;
      frame->key_frame = 0;
    }

    MetricsMgr().reset_metrics();

    //if (config::sunshine.privacy_mode) {
    //  bool isConfigLoaded = AuroraMgr().loadDisplayConfig((char *)physical_display_data_file.c_str());
    //  if (!isConfigLoaded) {
    //    BOOST_LOG(info) << "*********** can not load display data from file " << std::endl;
    //  }
    //}

    //if (config::sunshine.privacy_mode) {
    //  BOOST_LOG(info) << "in encode_run, calling cleanup_privacy_setup";
    //  stream::cleanup_privacy_setup();
    //}

  }

  input::touch_port_t
  make_port(platf::display_t *display, const config_t &config) {
    float wd = display->width;
    float hd = display->height;

    float wt = config.width;
    float ht = config.height;

    auto scalar = std::fminf(wt / wd, ht / hd);

    auto w2 = scalar * wd;
    auto h2 = scalar * hd;

    auto offsetX = (config.width - w2) * 0.5f;
    auto offsetY = (config.height - h2) * 0.5f;

    return input::touch_port_t {
      display->offset_x,
      display->offset_y,
      config.width,
      config.height,
      display->env_width,
      display->env_height,
      offsetX,
      offsetY,
      1.0f / scalar,
    };
  }

  std::optional<sync_session_t>
  make_synced_session(platf::display_t *disp, const encoder_t &encoder, platf::img_t &img, sync_session_ctx_t &ctx) {
    sync_session_t encode_session;

    encode_session.ctx = &ctx;

    auto pix_fmt = ctx.config.dynamicRange == 0 ? map_pix_fmt(encoder.static_pix_fmt) : map_pix_fmt(encoder.dynamic_pix_fmt);
    auto hwdevice = disp->make_hwdevice(pix_fmt);
    if (!hwdevice) {
      return std::nullopt;
    }

    // absolute mouse coordinates require that the dimensions of the screen are known
    ctx.touch_port_events->raise(make_port(disp, ctx.config));

    // Update client with our current HDR display state
    hdr_info_t hdr_info = std::make_unique<hdr_info_raw_t>(false);
    if (ctx.config.dynamicRange && disp->is_hdr()) {
      disp->get_hdr_metadata(hdr_info->metadata);
      hdr_info->enabled = true;
    }
    ctx.hdr_events->raise(std::move(hdr_info));

    auto session = make_session(disp, encoder, ctx.config, img.width, img.height, std::move(hwdevice));
    if (!session) {
      return std::nullopt;
    }

    encode_session.session = std::move(*session);

    return std::move(encode_session);
  }

  encode_e
  encode_run_sync(
    std::vector<std::unique_ptr<sync_session_ctx_t>> &synced_session_ctxs,
    encode_session_ctx_queue_t &encode_session_ctx_queue) {
    const auto &encoder = encoders.front();
    auto display_names = platf::display_names(map_base_dev_type(encoder.base_dev_type));
    int display_p = 0;

    //BOOST_LOG(info) << "*************encode_run_sync:  =" << std::endl;

    if (display_names.empty()) {
      display_names.emplace_back(config::video.output_name);
    }

    for (int x = 0; x < display_names.size(); ++x) {
      if (display_names[x] == config::video.output_name) {
        display_p = x;

        break;
      }
    }

    std::shared_ptr<platf::display_t> disp;

    auto switch_display_event = mail::man->event<int>(mail::switch_display);

    if (synced_session_ctxs.empty()) {
      auto ctx = encode_session_ctx_queue.pop();
      if (!ctx) {
        return encode_e::ok;
      }

      synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*ctx)));
    }

    while (encode_session_ctx_queue.running()) {
      // reset_display() will sleep between retries
      reset_display(disp, encoder.base_dev_type, display_p,display_names[display_p], synced_session_ctxs.front()->config);
      if (disp) {
        break;
      }
    }

    if (!disp) {
      return encode_e::error;
    }

    auto img = disp->alloc_img();
    if (!img || disp->dummy_img(img.get())) {
      return encode_e::error;
    }

    std::vector<sync_session_t> synced_sessions;
    for (auto &ctx : synced_session_ctxs) {
      auto synced_session = make_synced_session(disp.get(), encoder, *img, *ctx);
      if (!synced_session) {
        return encode_e::error;
      }

      synced_sessions.emplace_back(std::move(*synced_session));
    }

    auto ec = platf::capture_e::ok;

    MetricsMgr().register_metrics("FPS_ENCODE", 0, 3, [&]() {
      return (unsigned long long) (g_fps_encode[0]);
    },    
    []() {
      g_fps_encode[0] = 0.0;
    });
    MetricsMgr().register_metrics("FPS_ENCODE2", 0, 4, [&]() {
      return (unsigned long long) (g_fps_encode[1]);
    },    
    []() {
      g_fps_encode[1] = 0.0;
    }
    );

    long long num_of_frames = 0L;
    auto t_start = std::chrono::high_resolution_clock::now();

    while (encode_session_ctx_queue.running()) {
      auto snapshot_cb = [&](std::shared_ptr<platf::img_t> &img, bool frame_captured) -> std::shared_ptr<platf::img_t> {
        while (encode_session_ctx_queue.peek()) {
          auto encode_session_ctx = encode_session_ctx_queue.pop();
          if (!encode_session_ctx) {
            return nullptr;
          }

          synced_session_ctxs.emplace_back(std::make_unique<sync_session_ctx_t>(std::move(*encode_session_ctx)));

          auto encode_session = make_synced_session(disp.get(), encoder, *img, *synced_session_ctxs.back());
          if (!encode_session) {
            ec = platf::capture_e::error;
            return nullptr;
          }

          synced_sessions.emplace_back(std::move(*encode_session));
        }

        KITTY_WHILE_LOOP(auto pos = std::begin(synced_sessions), pos != std::end(synced_sessions), {
          auto frame = pos->session.device->frame;
          auto ctx = pos->ctx;
          if (ctx->shutdown_event->peek()) {
            // Let waiting thread know it can delete shutdown_event
            ctx->join_event->raise(true);

            pos = synced_sessions.erase(pos);
            synced_session_ctxs.erase(std::find_if(std::begin(synced_session_ctxs), std::end(synced_session_ctxs), [&ctx_p = ctx](auto &ctx) {
              return ctx.get() == ctx_p;
            }));

            if (synced_sessions.empty()) {
              return nullptr;
            }

            continue;
          }
          //Shawn, here ctx->idr_events already points to right one relative to display_Index
          if (ctx->idr_events->peek()) {
            frame->pict_type = AV_PICTURE_TYPE_I;
            frame->key_frame = 1;

            ctx->idr_events->pop();
          }

          if (frame_captured && pos->session.device->convert(*img)) {
            BOOST_LOG(error) << "Could not convert image"sv;
            ctx->shutdown_event->raise(true);

            continue;
          }

          num_of_frames++;
          calc_fps(num_of_frames, t_start, display_p);
          calc_frames(num_of_frames, display_p);
          calc_elapsed_time(t_start);

          if (encode(1, ctx->frame_nr++, pos->session, frame, ctx->packets, ctx->channel_data)) {
            BOOST_LOG(error) << "Could not encode video packet"sv;
            ctx->shutdown_event->raise(true);

            continue;
          }

          frame->pict_type = AV_PICTURE_TYPE_NONE;
          frame->key_frame = 0;

          ++pos;
        })

        if (switch_display_event->peek()) {
          ec = platf::capture_e::reinit;

          display_p = std::clamp(*switch_display_event->pop(), 0, (int) display_names.size() - 1);
          return nullptr;
        }

        return img;
      };

      auto status = disp->capture(std::move(snapshot_cb), img, &display_cursor);
      switch (status) {
        case platf::capture_e::reinit:
        case platf::capture_e::error:
        case platf::capture_e::ok:
        case platf::capture_e::timeout:
          return ec != platf::capture_e::ok ? ec : status;
      }
    }

    return encode_e::ok;
  }

  void
  captureThreadSync() {
    //BOOST_LOG(info) << "*************captureThreadSync: =" << std::endl;

    auto ref = capture_thread_sync.ref();

    std::vector<std::unique_ptr<sync_session_ctx_t>> synced_session_ctxs;

    auto &ctx = ref->encode_session_ctx_queue;
    auto lg = util::fail_guard([&]() {
      ctx.stop();

      for (auto &ctx : synced_session_ctxs) {
        ctx->shutdown_event->raise(true);
        ctx->join_event->raise(true);
      }

      for (auto &ctx : ctx.unsafe()) {
        ctx.shutdown_event->raise(true);
        ctx.join_event->raise(true);
      }
    });

    // Encoding and capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    while (encode_run_sync(synced_session_ctxs, ctx) == encode_e::reinit) {}
  }

  void
  capture_async(
    safe::mail_t mail,
    config_t &config,
    void *channel_data,
    int displayIndex) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    //BOOST_LOG(info) << "*************capture_async:displayIndex =" << displayIndex << std::endl;

    auto images = std::make_shared<img_event_t::element_type>();
    auto lg = util::fail_guard([&]() {
      images->stop();
      shutdown_event->raise(true);
    });

    auto ref = (displayIndex == 1) ? capture_thread_async.ref() : capture_thread_async2.ref();
    if (!ref) {
      return;
    }

    ref->capture_ctx_queue->raise(capture_ctx_t { images, config });

    if (!ref->capture_ctx_queue->running()) {
      return;
    }

    int frame_nr = 1;

    auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);
    auto hdr_event = mail->event<hdr_info_t>(mail::hdr);

    // Encoding takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    while (!shutdown_event->peek() && images->running()) {
      // Wait for the main capture event when the display is being reinitialized
      if (ref->reinit_event.peek()) {
        std::this_thread::sleep_for(20ms);
        continue;
      }
      // Wait for the display to be ready
      std::shared_ptr<platf::display_t> display;
      {
        auto lg = ref->display_wp.lock();
        if (ref->display_wp->expired()) {
          continue;
        }

        display = ref->display_wp->lock();
      }
      //BOOST_LOG(info) << "capture_async:get display,displayIndex =" << displayIndex << std::endl;

      auto &encoder = (displayIndex == 1) ? encoders.front() : encoders2.front();
      auto pix_fmt = config.dynamicRange == 0 ? map_pix_fmt(encoder.static_pix_fmt) : map_pix_fmt(encoder.dynamic_pix_fmt);
      auto hwdevice = display->make_hwdevice(pix_fmt);
      if (!hwdevice) {
        return;
      }

      // absolute mouse coordinates require that the dimensions of the screen are known
      touch_port_event->raise(make_port(display.get(), config));

      // Update client with our current HDR display state
      hdr_info_t hdr_info = std::make_unique<hdr_info_raw_t>(false);
      if (config.dynamicRange && display->is_hdr()) {
        display->get_hdr_metadata(hdr_info->metadata);
        hdr_info->enabled = true;
      }
      hdr_event->raise(std::move(hdr_info));

      encode_run(
        displayIndex,
        frame_nr,
        mail, images,
        config, display,
        std::move(hwdevice),
        ref->reinit_event, *ref->encoder_p,
        channel_data);
    }
  }

  void
  capture(
    safe::mail_t mail,
    config_t config,
    void *channel_data,
    int displayIndex) {
    //wait for Encoder validation finished
    //videoInitThread.join();

    auto idr_events = (displayIndex == 1) ? mail->event<bool>(mail::idr) : mail->event<bool>(mail::idr2);

    idr_events->raise(true);
    if (encoders.front().flags & PARALLEL_ENCODING) {
      capture_async(std::move(mail), config, channel_data, displayIndex);
    }
    else {
      safe::signal_t join_event;
      auto ref = capture_thread_sync.ref();
      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {
        &join_event,
        mail->event<bool>(mail::shutdown),
        mail::man->queue<packet_t>(mail::video_packets),
        std::move(idr_events),
        mail->event<hdr_info_t>(mail::hdr),
        mail->event<input::touch_port_t>(mail::touch_port),
        config,
        1,
        channel_data,
      });

      // Wait for join signal
      join_event.view();
    }
  }

  enum validate_flag_e {
    VUI_PARAMS = 0x01,
    NALU_PREFIX_5b = 0x02,
  };

  int
  validate_config(int displayIndex, std::string &displayName, std::shared_ptr<platf::display_t> &disp,
    const encoder_t &encoder, const config_t &config) {
    //BOOST_LOG(info) << "*************validate_config" << std::endl;

    reset_display(disp, encoder.base_dev_type, displayIndex,displayName, config);
    if (!disp) {
      return -1;
    }

    auto pix_fmt = config.dynamicRange == 0 ? map_pix_fmt(encoder.static_pix_fmt) : map_pix_fmt(encoder.dynamic_pix_fmt);
    auto hwdevice = disp->make_hwdevice(pix_fmt);
    if (!hwdevice) {
      return -1;
    }

    auto session = make_session(disp.get(), encoder, config, disp->width, disp->height, std::move(hwdevice));
    if (!session) {
      BOOST_LOG(error) << "In validate_config(), failed to make session for display index "sv << displayIndex << ", displayName = "sv << displayName;
      return -1;
    }

    auto img = disp->alloc_img();
    if (!img || disp->dummy_img(img.get())) {
      return -1;
    }
    if (session->device->convert(*img)) {
      return -1;
    }

    auto frame = session->device->frame;

    frame->pict_type = AV_PICTURE_TYPE_I;

    auto packets = mail::man->queue<packet_t>((displayIndex == 1) ? mail::video_packets : mail::video_packets2);
    while (!packets->peek()) {
      if (encode(displayIndex, 1, *session, frame, packets, nullptr)) {
        return -1;
      }
    }

    auto packet = packets->pop();
    auto av_packet = packet->av_packet;
    if (!(av_packet->flags & AV_PKT_FLAG_KEY)) {
      BOOST_LOG(error) << "First packet type is not an IDR frame"sv;

      return -1;
    }

    int flag = 0;
    if (cbs::validate_sps(&*av_packet, config.videoFormat ? AV_CODEC_ID_H265 : AV_CODEC_ID_H264)) {
      flag |= VUI_PARAMS;
    }

    auto nalu_prefix = config.videoFormat ? hevc_nalu : h264_nalu;
    std::string_view payload { (char *) av_packet->data, (std::size_t) av_packet->size };
    if (std::search(std::begin(payload), std::end(payload), std::begin(nalu_prefix), std::end(nalu_prefix)) != std::end(payload)) {
      flag |= NALU_PREFIX_5b;
    }

    return flag;
  }

  bool
  validate_encoder(encoder_t &encoder, std::string &displayName, int displayIndex) {
    std::shared_ptr<platf::display_t> disp;

    BOOST_LOG(info) << "Trying encoder ["sv << encoder.name << ']';
    auto fg = util::fail_guard([&]() {
      BOOST_LOG(info) << "Encoder ["sv << encoder.name << "] failed"sv;
    });

    auto force_hevc = config::video.hevc_mode >= 2;
    auto test_hevc = force_hevc || (config::video.hevc_mode == 0 && !(encoder.flags & H264_ONLY));

    encoder.h264.capabilities.set();
    encoder.hevc.capabilities.set();

    encoder.hevc[encoder_t::PASSED] = test_hevc;

    // First, test encoder viability
    config_t config_max_ref_frames { 1920, 1080, 60, 1000, 1, 1, 1, 0, 0 };
    config_t config_autoselect { 1920, 1080, 60, 1000, 1, 0, 1, 0, 0 };

  retry:
    auto max_ref_frames_h264 = validate_config(displayIndex, displayName, disp, encoder, config_max_ref_frames);
    auto autoselect_h264 = validate_config(displayIndex, displayName, disp, encoder, config_autoselect);

    if (max_ref_frames_h264 < 0 && autoselect_h264 < 0) {
      if (encoder.h264.qp && encoder.h264[encoder_t::CBR]) {
        // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
        encoder.h264.capabilities.set();
        encoder.h264[encoder_t::CBR] = false;
        goto retry;
      }
      return false;
    }

    std::vector<std::pair<validate_flag_e, encoder_t::flag_e>> packet_deficiencies {
      { VUI_PARAMS, encoder_t::VUI_PARAMETERS },
      { NALU_PREFIX_5b, encoder_t::NALU_PREFIX_5b },
    };

    for (auto [validate_flag, encoder_flag] : packet_deficiencies) {
      encoder.h264[encoder_flag] = (max_ref_frames_h264 & validate_flag && autoselect_h264 & validate_flag);
    }

    encoder.h264[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_h264 >= 0;
    encoder.h264[encoder_t::REF_FRAMES_AUTOSELECT] = autoselect_h264 >= 0;
    encoder.h264[encoder_t::PASSED] = true;

    encoder.h264[encoder_t::SLICE] = validate_config(displayIndex, displayName, disp, encoder, config_max_ref_frames);
    if (test_hevc) {
      config_max_ref_frames.videoFormat = 1;
      config_autoselect.videoFormat = 1;

    retry_hevc:
      auto max_ref_frames_hevc = validate_config(displayIndex, displayName, disp, encoder, config_max_ref_frames);
      auto autoselect_hevc = validate_config(displayIndex, displayName, disp, encoder, config_autoselect);

      // If HEVC must be supported, but it is not supported
      if (max_ref_frames_hevc < 0 && autoselect_hevc < 0) {
        if (encoder.hevc.qp && encoder.hevc[encoder_t::CBR]) {
          // It's possible the encoder isn't accepting Constant Bit Rate. Turn off CBR and make another attempt
          encoder.hevc.capabilities.set();
          encoder.hevc[encoder_t::CBR] = false;
          goto retry_hevc;
        }

        if (force_hevc) {
          return false;
        }
      }

      for (auto [validate_flag, encoder_flag] : packet_deficiencies) {
        encoder.hevc[encoder_flag] = (max_ref_frames_hevc & validate_flag && autoselect_hevc & validate_flag);
      }

      encoder.hevc[encoder_t::REF_FRAMES_RESTRICT] = max_ref_frames_hevc >= 0;
      encoder.hevc[encoder_t::REF_FRAMES_AUTOSELECT] = autoselect_hevc >= 0;

      encoder.hevc[encoder_t::PASSED] = max_ref_frames_hevc >= 0 || autoselect_hevc >= 0;
    }

    std::vector<std::pair<encoder_t::flag_e, config_t>> configs {
      { encoder_t::DYNAMIC_RANGE, { 1920, 1080, 60, 1000, 1, 0, 3, 1, 1 } },
    };

    if (!(encoder.flags & SINGLE_SLICE_ONLY)) {
      configs.emplace_back(
        std::pair<encoder_t::flag_e, config_t> { encoder_t::SLICE, { 1920, 1080, 60, 1000, 2, 1, 1, 0, 0 } });
    }

    for (auto &[flag, config] : configs) {
      auto h264 = config;
      auto hevc = config;

      h264.videoFormat = 0;
      hevc.videoFormat = 1;

      encoder.h264[flag] = validate_config(displayIndex, displayName, disp, encoder, h264) >= 0;
      if (encoder.hevc[encoder_t::PASSED]) {
        encoder.hevc[flag] = validate_config(displayIndex, displayName, disp, encoder, hevc) >= 0;
      }
    }

    if (encoder.flags & SINGLE_SLICE_ONLY) {
      encoder.h264.capabilities[encoder_t::SLICE] = false;
      encoder.hevc.capabilities[encoder_t::SLICE] = false;
    }

    encoder.h264[encoder_t::VUI_PARAMETERS] = encoder.h264[encoder_t::VUI_PARAMETERS] && !config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];
    encoder.hevc[encoder_t::VUI_PARAMETERS] = encoder.hevc[encoder_t::VUI_PARAMETERS] && !config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE];

    if (!encoder.h264[encoder_t::VUI_PARAMETERS]) {
      BOOST_LOG(warning) << encoder.name << ": h264 missing sps->vui parameters"sv;
    }
    if (encoder.hevc[encoder_t::PASSED] && !encoder.hevc[encoder_t::VUI_PARAMETERS]) {
      BOOST_LOG(warning) << encoder.name << ": hevc missing sps->vui parameters"sv;
    }

    if (!encoder.h264[encoder_t::NALU_PREFIX_5b]) {
      BOOST_LOG(warning) << encoder.name << ": h264: replacing nalu prefix data"sv;
    }
    if (encoder.hevc[encoder_t::PASSED] && !encoder.hevc[encoder_t::NALU_PREFIX_5b]) {
      BOOST_LOG(warning) << encoder.name << ": hevc: replacing nalu prefix data"sv;
    }

    fg.disable();
    return true;
  }

  int
  initPerDisplay(std::vector<encoder_t> &encoders, std::string &defaultEncoder,
    std::string &outputDisplayName, int displayIndex) {
      BOOST_LOG(warning) << "initPerDisplay(), outputDisplayName = " << outputDisplayName <<", Display Index = "<< displayIndex;
    bool encoder_found = false;
    if (!defaultEncoder.empty()) {
      // If there is a specific encoder specified, use it if it passes validation
      KITTY_WHILE_LOOP(auto pos = std::begin(encoders), pos != std::end(encoders), {
        auto encoder = *pos;

        if (encoder.name == defaultEncoder) {
          // Remove the encoder from the list entirely if it fails validation
          if (!validate_encoder(encoder, outputDisplayName, displayIndex)) {
            pos = encoders.erase(pos);
            break;
          }

          // If we can't satisfy both the encoder and HDR requirement, prefer the encoder over HDR support
          if (config::video.hevc_mode == 3 && !encoder.hevc[encoder_t::DYNAMIC_RANGE]) {
            BOOST_LOG(warning) << "Encoder ["sv << defaultEncoder << "] does not support HDR on this system"sv;
            config::video.hevc_mode = 0;
          }
          encoders.clear();
          encoders.emplace_back(encoder);
          encoder_found = true;
          break;
        }

        pos++;
      });

      if (!encoder_found) {
        BOOST_LOG(error) << "Couldn't find any working encoder matching ["sv << defaultEncoder << ']';
        defaultEncoder.clear();
      }
    }

    BOOST_LOG(info) << "// Testing for available encoders, this may generate errors. You can safely ignore those errors. //"sv;

    // If we haven't found an encoder yet, but we want one with HDR support, search for that now.
    if (!encoder_found && config::video.hevc_mode == 3) {
      KITTY_WHILE_LOOP(auto pos = std::begin(encoders), pos != std::end(encoders), {
        auto encoder = *pos;

        // Remove the encoder from the list entirely if it fails validation
        if (!validate_encoder(encoder, outputDisplayName, displayIndex)) {
          pos = encoders.erase(pos);
          continue;
        }

        // Skip it if it doesn't support HDR
        if (!encoder.hevc[encoder_t::DYNAMIC_RANGE]) {
          pos++;
          continue;
        }

        encoders.clear();
        encoders.emplace_back(encoder);
        encoder_found = true;
        break;
      });

      if (!encoder_found) {
        BOOST_LOG(error) << "Couldn't find any working HDR-capable encoder"sv;
      }
    }

    // If no encoder was specified or the specified encoder was unusable, keep trying
    // the remaining encoders until we find one that passes validation.
    if (!encoder_found) {
      KITTY_WHILE_LOOP(auto pos = std::begin(encoders), pos != std::end(encoders), {
        if (!validate_encoder(*pos, outputDisplayName, displayIndex)) {
          pos = encoders.erase(pos);
          continue;
        }

        break;
      });
    }

    if (encoders.empty()) {
      BOOST_LOG(fatal) << "Couldn't find any working encoder"sv;
      return -1;
    }

    BOOST_LOG(info);
    BOOST_LOG(info) << "// Ignore any errors mentioned above, they are not relevant. //"sv;
    BOOST_LOG(info);

    auto &encoder = encoders.front();

    BOOST_LOG(debug) << "------  h264 ------"sv;
    for (int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
      auto flag = (encoder_t::flag_e) x;
      BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.h264[flag] ? ": supported"sv : ": unsupported"sv);
    }
    BOOST_LOG(debug) << "-------------------"sv;

    if (encoder.hevc[encoder_t::PASSED]) {
      BOOST_LOG(debug) << "------  hevc ------"sv;
      for (int x = 0; x < encoder_t::MAX_FLAGS; ++x) {
        auto flag = (encoder_t::flag_e) x;
        BOOST_LOG(debug) << encoder_t::from_flag(flag) << (encoder.hevc[flag] ? ": supported"sv : ": unsupported"sv);
      }
      BOOST_LOG(debug) << "-------------------"sv;

      BOOST_LOG(info) << "Found encoder "sv << encoder.name << ": ["sv << encoder.h264.name << ", "sv << encoder.hevc.name << ']';
    }
    else {
      BOOST_LOG(info) << "Found encoder "sv << encoder.name << ": ["sv << encoder.h264.name << ']';
    }

    if (config::video.hevc_mode == 0) {
      config::video.hevc_mode = encoder.hevc[encoder_t::PASSED] ? (encoder.hevc[encoder_t::DYNAMIC_RANGE] ? 3 : 2) : 1;
    }

    return 0;
  }
  std::vector<std::string>
  string_split(const std::string &str, const char *delim) {
    std::vector<std::string> list;
    std::string temp;
    size_t pos = 0;
    size_t pos2 = 0;
    size_t delim_size = strlen(delim);
    while ((pos2 = str.find(delim, pos)) != std::string::npos) {
      temp = str.substr(pos, pos2 - pos);
      list.push_back(temp);
      pos = pos2 + delim_size;
    }
    if (pos != std::string::npos) {
      temp = str.substr(pos);
      list.push_back(temp);
    }
    return list;
  }
#define TRIMOFF_CHARS " \t\n\r\f\v"

  // trim from end of string (right)
  std::string &
  rtrim(std::string &s) {
    s.erase(s.find_last_not_of(TRIMOFF_CHARS) + 1);
    return s;
  }

  // trim from beginning of string (left)
  std::string &
  ltrim(std::string &s) {
    s.erase(0, s.find_first_not_of(TRIMOFF_CHARS));
    return s;
  }

  // trim from both ends of string (right then left)
  std::string &
  trim(std::string &s) {
    return ltrim(rtrim(s));
  }

  struct DispInfo {
    std::string name;
    std::string fullName;
    int left, top, right, bottom;
  };

  bool VerifyOutputCnt (int cnt) {
    std::string strlists;
    int new_cnt = AuroraMgr().EnumDisplayOutputs(strlists);
    int loop_cnt =2000;
    while(new_cnt != cnt && loop_cnt>0) {
      std::this_thread::sleep_for(10ms);
      new_cnt = AuroraMgr().EnumDisplayOutputs(strlists);
      loop_cnt--;
    }
    if (new_cnt == cnt)
      return true;
    else
      return false;
  }

  bool VerifyOutputsOld(int prev_display_cnt){
    bool verifyResult = false;
    auto parseDispInfo = [](DispInfo& dispInfo,std::string &strInfo) {
      auto parts = string_split(strInfo, "--");
      if (parts.size() >= 2) {
        dispInfo.name = trim(parts[0]);
        if(dispInfo.name.empty()) {
          return false;
        }
        auto& part = trim(parts[1]);
        auto pos = part.find(" (");
        if(pos!=part.npos) {
          part = part.substr(0,pos);
        }
        int retVal = sscanf(part.c_str(), "[%d,%d,%d,%d]",
          &dispInfo.left, &dispInfo.top, &dispInfo.right, &dispInfo.bottom);
        if (retVal != 4) {
          dispInfo.left = 0;
          dispInfo.top = 0;
          dispInfo.right = 0;
          dispInfo.bottom = 0;
        }
        dispInfo.fullName = strInfo;
        return true;
      }
      return false;
    };
    std::string strlists;
    int new_cnt = AuroraMgr().EnumDisplayOutputs(strlists);
    int loop_cnt =2000;
    BOOST_LOG(info) <<"before loop, new_cnt = "<< new_cnt << ", prev_display_cnt = " << prev_display_cnt;

    while(new_cnt == prev_display_cnt && loop_cnt>0) {
      std::this_thread::sleep_for(100ms);
      new_cnt = AuroraMgr().EnumDisplayOutputs(strlists);
      loop_cnt--;
    }

    BOOST_LOG(info) <<"after loop, new_cnt = "<< new_cnt << ", prev_display_cnt = " << prev_display_cnt << ", loop_cnt = " << loop_cnt;

    if (new_cnt > prev_display_cnt)
      verifyResult = true;

    auto exclude_list = string_split(config::video.output_filter,";");
    auto isExcluded = [exclude_list](std::string& name) {
      for(auto& it:exclude_list) {
        if(it == name) {
          return true;
        }
      }
      return false;
    };
    //BOOST_LOG(info) <<"Outputs Detected:"<<strlists;
    auto lists = string_split(strlists, "\n");
    std::vector<DispInfo> dispList;
    for (auto& row : lists) {
      if (row.c_str()[0] != 0)
        BOOST_LOG(info) <<"Outputs Detected:"<<row;
      DispInfo dispInfo;
      if (parseDispInfo(dispInfo, row)) {
        if(!isExcluded(dispInfo.name)) {
          dispList.push_back(dispInfo);
        }
      }
    }
    //check first outout
    auto modifyOutput = [parseDispInfo,&dispList](std::string &outputName, 
        std::string &fullName, DispInfo* &skipItem) {
        for (auto& item : dispList) {
            if (&item != skipItem && item.name == outputName) {
              //find matched name, no change on outputName
              skipItem = &item;
              return true;
            }
        }//end for
        //then match with fullname
        DispInfo dispInfo;
        parseDispInfo(dispInfo,(std::string&)fullName);
        auto MaxNum = [](int x, int y) {
          return x > y ? x : y;
        };
        auto MinNum = [](int x, int y) {
          return x < y ? x : y;
        };
        auto iou = [MinNum, MaxNum](DispInfo &d1, DispInfo &d2) {
          return (MinNum(d1.right, d2.right) - MaxNum(d1.left, d2.left)) *
                 (MinNum(d1.bottom, d2.bottom) - MaxNum(d1.top, d2.top));
        };
        //search for largest area of intersection (IOU)
        DispInfo *pLargOne = nullptr;
        int area = 0;
        for (auto &item : dispList) {
            if (&item != skipItem && item.name == dispInfo.name) {
              skipItem = &item;
              outputName = dispInfo.name;
              return true;
            }
            int a0 = iou(dispInfo, item);
            if (a0 > 0 && area<a0) {
              pLargOne = &item;
              area = a0;
            }
        }//end for
        if (pLargOne) {
            outputName = pLargOne->name;
            skipItem = pLargOne;
            return true;
        }
      return false;
    };
    DispInfo *pMatchedOne = nullptr;
    bool bFirstOK = modifyOutput(config::video.output_name, config::video.output_fullname, pMatchedOne);
    bool bSecondOk = true;
    if (!config::video.output2_name.empty()) {
      bSecondOk = modifyOutput(config::video.output2_name, config::video.output2_fullname, pMatchedOne);
    }
    if(!bFirstOK) {
      for (auto &item : dispList) {
        if(&item!=pMatchedOne) {
            config::video.output_name = item.name;
            config::video.output_fullname = item.fullName;
            pMatchedOne =&item;
        }
      }
    }
    if(!bSecondOk) {
      for (auto &item : dispList) {
        if(&item!=pMatchedOne) {
            config::video.output2_name = item.name;
            config::video.output2_fullname = item.fullName;
        }
      }
    }
    BOOST_LOG(info) <<"Output:" <<config::video.output_name<<",Output2:"<<config::video.output2_name;
    return verifyResult;
  }

  bool ModifyOutputNames(){
    auto parseDispInfo = [](DispInfo& dispInfo,std::string &strInfo) {
      auto parts = string_split(strInfo, "--");
      if (parts.size() >= 2) {
        dispInfo.name = trim(parts[0]);
        if(dispInfo.name.empty()) {
          return false;
        }
        auto& part = trim(parts[1]);
        auto pos = part.find(" (");
        if(pos!=part.npos) {
          part = part.substr(0,pos);
        }
        int retVal = sscanf(part.c_str(), "[%d,%d,%d,%d]",
          &dispInfo.left, &dispInfo.top, &dispInfo.right, &dispInfo.bottom);
        if (retVal != 4) {
          dispInfo.left = 0;
          dispInfo.top = 0;
          dispInfo.right = 0;
          dispInfo.bottom = 0;
        }
        dispInfo.fullName = strInfo;
        return true;
      }
      return false;
    };
    std::string strlists;
    int new_cnt = AuroraMgr().EnumDisplayOutputs(strlists);
    auto exclude_list = string_split(config::video.output_filter,";");
    auto isExcluded = [exclude_list](std::string& name) {
      for(auto& it:exclude_list) {
        if(it == name) {
          return true;
        }
      }
      return false;
    };
    //BOOST_LOG(info) <<"Outputs Detected:"<<strlists;
    auto lists = string_split(strlists, "\n");
    std::vector<DispInfo> dispList;
    for (auto& row : lists) {
      if (row.c_str()[0] != 0)
        BOOST_LOG(info) <<"Outputs Detected:"<<row;
      DispInfo dispInfo;
      if (parseDispInfo(dispInfo, row)) {
        if(!isExcluded(dispInfo.name)) {
          dispList.push_back(dispInfo);
        }
      }
    }
    //check first outout
    auto modifyOutput = [parseDispInfo,&dispList](std::string &outputName, 
        std::string &fullName, DispInfo* &skipItem) {
        for (auto& item : dispList) {
            if (&item != skipItem && item.name == outputName) {
              //find matched name, no change on outputName
              skipItem = &item;
              return true;
            }
        }//end for
        //then match with fullname
        DispInfo dispInfo;
        parseDispInfo(dispInfo,(std::string&)fullName);
        auto MaxNum = [](int x, int y) {
          return x > y ? x : y;
        };
        auto MinNum = [](int x, int y) {
          return x < y ? x : y;
        };
        auto iou = [MinNum, MaxNum](DispInfo &d1, DispInfo &d2) {
          return (MinNum(d1.right, d2.right) - MaxNum(d1.left, d2.left)) *
                 (MinNum(d1.bottom, d2.bottom) - MaxNum(d1.top, d2.top));
        };
        //search for largest area of intersection (IOU)
        DispInfo *pLargOne = nullptr;
        int area = 0;
        for (auto &item : dispList) {
            if (&item != skipItem && item.name == dispInfo.name) {
              skipItem = &item;
              outputName = dispInfo.name;
              return true;
            }
            int a0 = iou(dispInfo, item);
            if (a0 > 0 && area<a0) {
              pLargOne = &item;
              area = a0;
            }
        }//end for
        if (pLargOne) {
            outputName = pLargOne->name;
            skipItem = pLargOne;
            return true;
        }
      return false;
    };
    DispInfo *pMatchedOne = nullptr;
    bool bFirstOK = modifyOutput(config::video.output_name, config::video.output_fullname, pMatchedOne);
    bool bSecondOk = true;
    if (!config::video.output2_name.empty()) {
      bSecondOk = modifyOutput(config::video.output2_name, config::video.output2_fullname, pMatchedOne);
    }
    if(!bFirstOK) {
      for (auto &item : dispList) {
        if(&item!=pMatchedOne) {
            config::video.output_name = item.name;
            config::video.output_fullname = item.fullName;
            pMatchedOne =&item;
        }
      }
    }
    if(!bSecondOk) {
      for (auto &item : dispList) {
        if(&item!=pMatchedOne) {
            config::video.output2_name = item.name;
            config::video.output2_fullname = item.fullName;
        }
      }
    }
    BOOST_LOG(info) <<"Output:" <<config::video.output_name<<",Output2:"<<config::video.output2_name;
    return true;
  }


template<typename T>
bool compare(std::vector<T>& v1, std::vector<T>& v2)
{
    std::sort(v1.begin(), v1.end());
    std::sort(v2.begin(), v2.end());
    return v1 == v2;
}

bool VerifyVirtualOutputs( int &prevNumVirtual, std::vector<std::string> &virtualDisplayNamesPrev, std::vector<std::string> &physicalDisplayNamesPrev){

    bool verifyResult = false;
    std::vector<std::string> displayNames;
    std::vector<std::string> virtualDisplayNames;
    std::vector<std::string> physicalDisplayNames;
    int numPhysical = 0;
    int numVirtual = 0;

    //int new_cnt = AuroraMgr().EnumDisplayOutputs(strlists);
    int numAllDisplays = stream::scanDisplays(
                              displayNames,
                              virtualDisplayNames,
                              physicalDisplayNames,
                              numPhysical,
                              numVirtual);
    int new_cnt =   numVirtual;

    int loop_cnt = 0;
    int loop_cnt_max = 1000;

    BOOST_LOG(info) <<"in VerifyVirtualOutputs(), before loop: will loop up to " << loop_cnt_max << " times to wait for the virtual display creation to be done.";
    if (virtualDisplayNamesPrev.size() == 0) {
      BOOST_LOG(info) <<"in VerifyVirtualOutputs(), no previous virtual display ";
    }
    else {
      for (int i = 0; i < virtualDisplayNamesPrev.size(); i++) {
          BOOST_LOG(info) <<"in VerifyVirtualOutputs(), previous virtual display " << i << ": " <<  virtualDisplayNamesPrev[i];
      }
    }

    while(compare (virtualDisplayNamesPrev, virtualDisplayNames) && loop_cnt++ < loop_cnt_max) {
      //BOOST_LOG(info) << "new_cnt = "<< new_cnt << ", prev_display_cnt = " << prev_display_cnt;

      std::this_thread::sleep_for(10ms);
      numAllDisplays = stream::scanDisplays(
                              displayNames,
                              virtualDisplayNames,
                              physicalDisplayNames,
                              numPhysical,
                              numVirtual);
    }

    BOOST_LOG(info) <<"in VerifyVirtualOutputs(), after loop: looped " << loop_cnt - 1<< " times. ";

    if (numVirtual== 0) {
      BOOST_LOG(info) <<"in VerifyVirtualOutputs(), no current virtual display ";
    }
    else {
      for (int i = 0; i < numVirtual; i++) {
          BOOST_LOG(info) <<"in VerifyVirtualOutputs(), current virtual display " << i << ": " <<  virtualDisplayNames[i];
      }
    }

    //to be deleted
    BOOST_LOG(info) << "in VerifyVirtualOutputs()" << std::endl;
    std::this_thread::sleep_for(10ms);
    //to be deleted

    if (loop_cnt >= loop_cnt_max) {
      BOOST_LOG(info) <<"in VerifyVirtualOutputs(), no current virtual display #2";
      verifyResult = false;  // creation is not completed
    }
    else {// creation is completed
      BOOST_LOG(info) <<"in VerifyVirtualOutputs(), creation is completed";
      if (compare (virtualDisplayNamesPrev, virtualDisplayNames)) {//identical names
        BOOST_LOG(info) <<"in VerifyVirtualOutputs(), current is same as previous";
        verifyResult = false;  // no new virtual is not created
      }
      else { //different names
        verifyResult = true;
        prevNumVirtual = numVirtual;
        BOOST_LOG(info) <<"in VerifyVirtualOutputs(), current is different from previous";
        //virtualDisplayNamesPrev = virtualDisplayNames; //update the list
        virtualDisplayNamesPrev.clear();
        virtualDisplayNamesPrev.insert(virtualDisplayNamesPrev.begin(), virtualDisplayNames.begin(), virtualDisplayNames.end() );
      }
    }
    return verifyResult;
  }

  static void
  CopyEncoder(std::vector<encoder_t> &from, std::vector<encoder_t> &to) {
    to.clear();
    for (auto &e : from) {
      to.push_back(e);
    }
  }

  void VirtualDisplayBuildOld(int prev_display_cnt){
    BOOST_LOG(info) << "In VirtualDisplayBuild(), Trying to create virtual display("sv <<config::video.virtualDisplayCount<<")"sv <<std::endl;
    int numCreated =0;
    bool verifyResult = false;
    for(int i=0;i<config::video.virtualDisplayCount;i++) {
      bool bOK = AuroraMgr().CreateDisplay();
      if(bOK) {
        numCreated++;
        verifyResult = VerifyOutputsOld(prev_display_cnt);
        if (verifyResult)
          prev_display_cnt ++;
      }
      else {
        break;
      }
    }
    if(numCreated < config::video.virtualDisplayCount) {
      BOOST_LOG(info) << "Only "sv << numCreated<<" virtual display Created\n"sv <<std::endl;
    }
    else {
      BOOST_LOG(info) << "Created "sv << numCreated<<" virtual display(s)\ns"sv <<std::endl;
    }
  }

  void VirtualDisplayBuild(int prev_display_cnt){
    BOOST_LOG(info) << "In VirtualDisplayBuild(), Trying to create virtual display("sv <<config::video.virtualDisplayCount<<")"sv <<std::endl;
    int numCreated =0;
    for(int i=0;i<config::video.virtualDisplayCount;i++) {
      bool bOK = AuroraMgr().CreateDisplayWait();
      if(bOK) {
        numCreated++;
      }
      else {
        break;
      }
    }

    if(numCreated < config::video.virtualDisplayCount) {
      BOOST_LOG(info) << "Only "sv << numCreated<<" virtual display Created"sv ;
    }
    else {
      ModifyOutputNames();
      BOOST_LOG(info) << "Created "sv << numCreated<<" virtual display(s)"sv 
                      << ", output_name = "sv << config::video.output_name 
                      << ", output2_name = "sv << config::video.output2_name;
    }
  }


  int
  init_only() {

    int prev_display_cnt =-1;//skip wait
    if(!config::sunshine.privacy_mode) {
      if(config::video.virtualDisplayCount>0){
        std::string strlists;
        prev_display_cnt = AuroraMgr().EnumDisplayOutputs(strlists,true);
        VirtualDisplayBuild(prev_display_cnt);
      }
    }
    else 
    {
      if(config::video.virtualDisplayCount < 1) 
      {
        config::video.virtualDisplayCount = 1;
      }

      if(config::video.virtualDisplayCount>1) 
      {
        config::video.haveSecondOutput = true;
      }
      else if (config::video.virtualDisplayCount == 1) 
      {
        config::video.haveSecondOutput = false;
      }
      BOOST_LOG(info) << "init_only(), privacy_mode is set, virtualDisplayCount =" << config::video.virtualDisplayCount;
    } 

    BOOST_LOG(info) << "init_only(), config::video.haveSecondOutput =" << config::video.haveSecondOutput
                    << ", config::video.virtualDisplayCount = " << config::video.virtualDisplayCount
                    << ", actual display count ( -1 for privacy mode in this stage) = " << prev_display_cnt
                    << ", privacy_mode = " << config::sunshine.privacy_mode;
    CopyEncoder(encoders_backup, encoders);
    int retValue = initPerDisplay(encoders, config::video.encoder, config::video.output_name, 1);
    if (retValue != 0) {
      BOOST_LOG(info) << "init_only(), initPerDisplay failed, output_name = " << config::video.output_name<<", encoder ="<< config::video.encoder;
      return retValue;
    }
    BOOST_LOG(info) << "init_only(), initPerDisplay successful, output_name = " << config::video.output_name<<", encoder ="<< config::video.encoder;

    if (config::video.haveSecondOutput) {
      BOOST_LOG(info) << "init_only(), output2_name =" << config::video.output2_name;
      CopyEncoder(encoders2_backup, encoders2);
      retValue = initPerDisplay(encoders2, config::video.encoder2, config::video.output2_name, 2);
      if (retValue != 0) {
        BOOST_LOG(info) << "init_only(), initPerDisplay failed, output2_name = " << config::video.output2_name<<", encoder2 ="<< config::video.encoder2;
        return retValue;
      }
      BOOST_LOG(info) << "init_only(), initPerDisplay successful, output2_name = " << config::video.output2_name<<", encoder2 ="<< config::video.encoder2;

    }
    return 0;
  }

  int
  init() 
  {
    BOOST_LOG(info) << "video init()";
    init_only();
    return 0;
  }
  //Shawn: used for (virtual) display changed
  //make prvious encoder stopped
  // then scan selected display
  int
  reinit() { 
      BOOST_LOG(info) << "video reinit()";
      auto ref = capture_thread_async.ref();
      if( ref) {
        ref->reinit_event.raise(true);
      }
      ref = capture_thread_async2.ref();
      if(ref) {
        ref->reinit_event.raise(true);
      }
      init_only();
      return 0;
  }
  int
  hwframe_ctx(ctx_t &ctx, platf::hwdevice_t *hwdevice, buffer_t &hwdevice_ctx, AVPixelFormat format) {
    buffer_t frame_ref { av_hwframe_ctx_alloc(hwdevice_ctx.get()) };

    auto frame_ctx = (AVHWFramesContext *) frame_ref->data;
    frame_ctx->format = ctx->pix_fmt;
    frame_ctx->sw_format = format;
    frame_ctx->height = ctx->height;
    frame_ctx->width = ctx->width;
    frame_ctx->initial_pool_size = 0;

    // Allow the hwdevice to modify hwframe context parameters
    hwdevice->init_hwframes(frame_ctx);

    if (auto err = av_hwframe_ctx_init(frame_ref.get()); err < 0) {
      return err;
    }

    ctx->hw_frames_ctx = av_buffer_ref(frame_ref.get());

    return 0;
  }

  // Linux only declaration
  typedef int (*vaapi_make_hwdevice_ctx_fn)(platf::hwdevice_t *base, AVBufferRef **hw_device_buf);

  util::Either<buffer_t, int>
  vaapi_make_hwdevice_ctx(platf::hwdevice_t *base) {
    buffer_t hw_device_buf;

    // If an egl hwdevice
    if (base->data) {
      if (((vaapi_make_hwdevice_ctx_fn) base->data)(base, &hw_device_buf)) {
        return -1;
      }

      return hw_device_buf;
    }

    auto render_device = config::video.adapter_name.empty() ? nullptr : config::video.adapter_name.c_str();

    auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_VAAPI, render_device, nullptr, 0);
    if (status < 0) {
      char string[AV_ERROR_MAX_STRING_SIZE];
      BOOST_LOG(error) << "Failed to create a VAAPI device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
      return -1;
    }

    return hw_device_buf;
  }

  util::Either<buffer_t, int>
  cuda_make_hwdevice_ctx(platf::hwdevice_t *base) {
    buffer_t hw_device_buf;

    auto status = av_hwdevice_ctx_create(&hw_device_buf, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 1 /* AV_CUDA_USE_PRIMARY_CONTEXT */);
    if (status < 0) {
      char string[AV_ERROR_MAX_STRING_SIZE];
      BOOST_LOG(error) << "Failed to create a CUDA device: "sv << av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, status);
      return -1;
    }

    return hw_device_buf;
  }

#ifdef _WIN32
}

void
do_nothing(void *) {}

namespace video {
  util::Either<buffer_t, int>
  dxgi_make_hwdevice_ctx(platf::hwdevice_t *hwdevice_ctx) {
    buffer_t ctx_buf { av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA) };
    auto ctx = (AVD3D11VADeviceContext *) ((AVHWDeviceContext *) ctx_buf->data)->hwctx;

    std::fill_n((std::uint8_t *) ctx, sizeof(AVD3D11VADeviceContext), 0);

    auto device = (ID3D11Device *) hwdevice_ctx->data;

    device->AddRef();
    ctx->device = device;

    ctx->lock_ctx = (void *) 1;
    ctx->lock = do_nothing;
    ctx->unlock = do_nothing;

    auto err = av_hwdevice_ctx_init(ctx_buf.get());
    if (err) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
      BOOST_LOG(error) << "Failed to create FFMpeg hardware device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

      return err;
    }

    return ctx_buf;
  }
#endif

  int
  start_capture_async(capture_thread_async_ctx_t &capture_thread_ctx, char* data) {
    capture_thread_ctx.encoder_p = &encoders.front();
    capture_thread_ctx.reinit_event.reset();

    capture_thread_ctx.capture_ctx_queue = std::make_shared<safe::queue_t<capture_ctx_t>>(30);

    capture_thread_ctx.capture_thread = std::thread {
      captureThread,
      capture_thread_ctx.capture_ctx_queue,
      std::ref(capture_thread_ctx.display_wp),
      std::ref(capture_thread_ctx.reinit_event),
      std::ref(*capture_thread_ctx.encoder_p),
      1
    };

    return 0;
  }
  int
  start_capture_async2(capture_thread_async_ctx_t &capture_thread_ctx, char* data) {
    capture_thread_ctx.encoder_p = &encoders2.front();
    capture_thread_ctx.reinit_event.reset();

    capture_thread_ctx.capture_ctx_queue = std::make_shared<safe::queue_t<capture_ctx_t>>(30);

    capture_thread_ctx.capture_thread = std::thread {
      captureThread,
      capture_thread_ctx.capture_ctx_queue,
      std::ref(capture_thread_ctx.display_wp),
      std::ref(capture_thread_ctx.reinit_event),
      std::ref(*capture_thread_ctx.encoder_p),
      2
    };

    return 0;
  }
  void
  end_capture_async(capture_thread_async_ctx_t &capture_thread_ctx) {
    capture_thread_ctx.capture_ctx_queue->stop();

    MetricsMgr().reset_metrics();
    capture_thread_ctx.capture_thread.join();
  }

  int
  start_capture_sync(capture_thread_sync_ctx_t &ctx, char* data) {
    std::thread { &captureThreadSync }.detach();
    return 0;
  }
  void
  end_capture_sync(capture_thread_sync_ctx_t &ctx) {

    MetricsMgr().reset_metrics();

  }

  platf::mem_type_e
  map_base_dev_type(AVHWDeviceType type) {
    switch (type) {
      case AV_HWDEVICE_TYPE_D3D11VA:
        return platf::mem_type_e::dxgi;
      case AV_HWDEVICE_TYPE_VAAPI:
        return platf::mem_type_e::vaapi;
      case AV_HWDEVICE_TYPE_CUDA:
        return platf::mem_type_e::cuda;
      case AV_HWDEVICE_TYPE_NONE:
        return platf::mem_type_e::system;
      default:
        return platf::mem_type_e::unknown;
    }

    return platf::mem_type_e::unknown;
  }

  platf::pix_fmt_e
  map_pix_fmt(AVPixelFormat fmt) {
    switch (fmt) {
      case AV_PIX_FMT_YUV420P10:
        return platf::pix_fmt_e::yuv420p10;
      case AV_PIX_FMT_YUV420P:
        return platf::pix_fmt_e::yuv420p;
      case AV_PIX_FMT_NV12:
        return platf::pix_fmt_e::nv12;
      case AV_PIX_FMT_P010:
        return platf::pix_fmt_e::p010;
      default:
        return platf::pix_fmt_e::unknown;
    }

    return platf::pix_fmt_e::unknown;
  }

  color_t
  make_color_matrix(float Cr, float Cb, const float2 &range_Y, const float2 &range_UV) {
    float Cg = 1.0f - Cr - Cb;

    float Cr_i = 1.0f - Cr;
    float Cb_i = 1.0f - Cb;

    float shift_y = range_Y[0] / 255.0f;
    float shift_uv = range_UV[0] / 255.0f;

    float scale_y = (range_Y[1] - range_Y[0]) / 255.0f;
    float scale_uv = (range_UV[1] - range_UV[0]) / 255.0f;
    return {
      { Cr, Cg, Cb, 0.0f },
      { -(Cr * 0.5f / Cb_i), -(Cg * 0.5f / Cb_i), 0.5f, 0.5f },
      { 0.5f, -(Cg * 0.5f / Cr_i), -(Cb * 0.5f / Cr_i), 0.5f },
      { scale_y, shift_y },
      { scale_uv, shift_uv },
    };
  }

  color_t colors[] {
    make_color_matrix(0.299f, 0.114f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),  // BT601 MPEG
    make_color_matrix(0.299f, 0.114f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),  // BT601 JPEG
    make_color_matrix(0.2126f, 0.0722f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),  // BT709 MPEG
    make_color_matrix(0.2126f, 0.0722f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),  // BT709 JPEG
    make_color_matrix(0.2627f, 0.0593f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),  // BT2020 MPEG
    make_color_matrix(0.2627f, 0.0593f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),  // BT2020 JPEG
  };
}  // namespace video
