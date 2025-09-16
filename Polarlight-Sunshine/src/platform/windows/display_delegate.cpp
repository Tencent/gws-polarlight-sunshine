//
// Created by Shawn Xiong on 5/7/2023 to support dual displays
//

#include "display_delegate.h"
#include <iostream>
#include "../../export_apis.h"
#include "../../rtsp.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/bprint.h>
#include <libavutil/avstring.h>
#include <libswscale/swscale.h>
}
namespace video
{
  extern bool IsDisplay_changed(int nDisplayID);
  extern std::string GetOutputName(int displayIndex);
  extern void get_output_names(std::string& output_Name,std::string& output2_Name);
  extern void set_output_names(int displayId,std::string& name);
  extern bool get_haveSecondOutput();
  extern void clear_output2_name();
}
namespace platf::dxgi {
  std::shared_ptr<display_base_t>
  MakeDisp(mem_type_e hwdevice_type,int displayId, const ::video::config_t &config,
    const std::string &display_name);

  struct img_delegate_t: public platf::img_t {
      std::shared_ptr<img_t> realImg;
  };

  void display_delegate_t::MakeRealImg(img_delegate_t* img)
  {
    if(!img->realImg)
    {
      img->realImg = m_disp->alloc_img();
      m_disp->complete_img(img->realImg.get(),true);
    }
  }
  capture_e
  display_delegate_t::snapshot(img_t *img_base, std::chrono::milliseconds timeout, bool cursor_visible) {
    auto img = (img_delegate_t*) img_base;
    capture_e e = capture_e::ok;
    
    int nDisplayid = m_displayId;
    std::vector<std::string> output_list; 
    //ALways check if changed or not
    bool isChanged = ::video::IsDisplay_changed(nDisplayid);
    if (isChanged)
    {
        BOOST_LOG(info) << "display_delegate_t::snapshot Changed nDisplayid = " << nDisplayid;
        e = capture_e::reinit;
        return e;
    }
    if (m_disp) 
    {
      MakeRealImg(img);
      e = m_disp->snapshot(img->realImg.get(), timeout, cursor_visible);
    }
    else 
    {
      //shawn: check if display changed, if yes, then modify
      //config::video's output name and return capture_e::reinit
      //to make upper caller in captureThread to re-init this display device
      //this way will make a completed new display device
      //with all status reseted
      //std::vector<std::string> output_list;
      bool isChanged = ::video::IsDisplay_changed(nDisplayid);
      if(isChanged)
      {
        BOOST_LOG(info) << "display_delegate_t::snapshot Changed";
        e = capture_e::reinit;
      }
      else {
        e = capture_e::ok;//do nothing
      }
    }
    return e;
  }
  std::shared_ptr<img_t>
  display_delegate_t::alloc_img() {
    auto img = std::make_shared<img_delegate_t>();
    img->width = width;
    img->height = height;

    if (m_disp) {
      img->realImg = m_disp->alloc_img();
    }
    return img;   
  }
  int
  display_delegate_t::dummy_img(img_t *img_base) {
    auto img = (img_delegate_t*) img_base;
    int retVal =0;
    if (m_disp) {
      retVal = m_disp->dummy_img(img->realImg.get());
    }
    else {
      if (complete_img(img, true)) {
        return -1;
      }
      std::fill_n((std::uint8_t *) img->data, height * img->row_pitch, 10);
    }
    return retVal;
  }
  int
  display_delegate_t::complete_img(img_t *img_base, bool dummy) {
    auto img = (img_delegate_t*) img_base;
    int retVal =0;
    if (m_disp) {
      retVal = m_disp->complete_img(img->realImg.get(),dummy);
    }
    img->pixel_pitch = get_pixel_pitch();
    if (dummy && !img->row_pitch) {
      // Assume our dummy image will have no padding
      img->row_pitch = img->pixel_pitch * img->width;
    }

    if (!img->data) {
      img->data = new std::uint8_t[img->row_pitch * height];
    }
    return retVal;
  }
  std::vector<DXGI_FORMAT>
  display_delegate_t::get_supported_sdr_capture_formats() {
    if (m_disp) {
      return m_disp->get_supported_sdr_capture_formats();
    }
    return std::vector<DXGI_FORMAT>();
  }
  std::vector<DXGI_FORMAT>
  display_delegate_t::get_supported_hdr_capture_formats() {
    if (m_disp) {
        return m_disp->get_supported_hdr_capture_formats();
    }
    return std::vector<DXGI_FORMAT>();
  }

  class hwdevice_delegate_t: public platf::hwdevice_t {
    public:
      std::shared_ptr<platf::hwdevice_t> realDevice;

      void
      DrawString(AVFrame* frame,int x, int y, const std::string &str) 
      {
        // Allocate an AVFrame for RGB data
        AVFrame *outputRGBFrame = av_frame_alloc();
        outputRGBFrame->format = AV_PIX_FMT_RGB24;
        outputRGBFrame->width = frame->width;
        outputRGBFrame->height = frame->height;
        av_frame_get_buffer(outputRGBFrame, 0);

        // Simulate drawing text on the RGB frame using Windows GDI
        HDC hdc = CreateCompatibleDC(NULL);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdc, frame->width, frame->height);
        SelectObject(hdc, hBitmap);

        // Set the font and color for text drawing
        HFONT hFont = CreateFont(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, "Arial");
        SelectObject(hdc, hFont);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0, 0, 0));
        HBRUSH hBK = CreateSolidBrush(RGB(255,0,0));
        RECT rc ={0,0,frame->width,frame->height};
        FillRect(hdc,&rc,hBK);
        TextOut(hdc, x, y, str.c_str(), str.size());

        // Copy the GDI-drawn bitmap to the RGB frame
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = frame->width;
        bmi.bmiHeader.biHeight = -frame->height;  // Negative height for top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        GetDIBits(hdc, hBitmap, 0, frame->height, outputRGBFrame->data[0], (BITMAPINFO *) &bmi, DIB_RGB_COLORS);
        DeleteObject(hFont);
        DeleteObject(hBitmap);
        DeleteDC(hdc);
        DeleteObject(hBK);
        // Convert RGB back to YUV
        SwsContext *swsCtx = sws_getContext(
          frame->width, frame->height, AV_PIX_FMT_RGB24,
          frame->width, frame->height, AV_PIX_FMT_YUV420P,
          SWS_BILINEAR, NULL, NULL, NULL);

        sws_scale(swsCtx, outputRGBFrame->data, outputRGBFrame->linesize, 
            0, frame->height, frame->data, frame->linesize);
        av_frame_free(&outputRGBFrame);
        sws_freeContext(swsCtx);
    }

      virtual int
      convert(platf::img_t &img_base) override {
        auto &img = (img_delegate_t &) img_base;
        int retVal = -1;
        if(realDevice) {
          retVal = realDevice->convert(*img.realImg);
        }
        else {
          std::string strInfo ="test string";
          DrawString(this->frame,100,100,strInfo);
          retVal =0;
        }
        return retVal;
      }
      virtual int
      set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override {
        int retVal = -1;
        if(realDevice) {
          this->frame = frame;
          retVal = realDevice->set_frame(frame,hw_frames_ctx);
        }
        else {
          // Populate this frame with a hardware buffer if one isn't there already
          if (!frame->buf[0]) {
            auto err = av_hwframe_get_buffer(hw_frames_ctx, frame, 0);
            if (err) {
              char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
              BOOST_LOG(error) << "Failed to get hwframe buffer: " << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);
              return -1;
            }
          }          
          this->frame = frame;
          retVal =0;     
        }
        return retVal;
      }
      virtual void
      set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
        if(realDevice) {
          realDevice->set_colorspace(colorspace,color_range);
        }
      }
      virtual void
      init_hwframes(AVHWFramesContext *frames) override {
        if(realDevice) {
          realDevice->init_hwframes(frames);
        }
      }
      virtual int
      prepare_to_derive_context(int hw_device_type) override {
        int retVal = -1;
        if(realDevice) {
          retVal = realDevice->prepare_to_derive_context(hw_device_type);
        }
        else {
          retVal =0;
        }
        return retVal;
      }
  };

  std::shared_ptr<platf::hwdevice_t>
  display_delegate_t::make_hwdevice(pix_fmt_e pix_fmt) {
    auto hwdevice = std::make_shared<hwdevice_delegate_t>();
    if (m_disp) {
      hwdevice->realDevice = m_disp->make_hwdevice(pix_fmt);
      hwdevice->data = hwdevice->realDevice->data;
    }
    else {
      hwdevice->data = device.get();
    }
    return hwdevice;
  }
  //help function to create display_t
  std::shared_ptr<display_base_t>
  MakeDisp(mem_type_e hwdevice_type, int displayId,const ::video::config_t &config,
    const std::string &display_name) {
    if (hwdevice_type == mem_type_e::dxgi) {
      auto disp = std::make_shared<dxgi::display_vram_t>();

      if (!disp->init(displayId,config, display_name)) {
        return disp;
      }
    }
    else if (hwdevice_type == mem_type_e::system) {
      auto disp = std::make_shared<dxgi::display_ram_t>();

      if (!disp->init(displayId,config, display_name)) {
        return disp;
      }
    }
    return std::shared_ptr<display_base_t>();
  }
  int display_delegate_t::InitForEncoderDevice() {
    D3D_FEATURE_LEVEL featureLevels[] {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1
    };
    auto status = CreateDXGIFactory1(IID_IDXGIFactory1, (void **) &factory);
    adapter_t::pointer adapter_p;
    if (!factory) {
      BOOST_LOG(info) << "display_delegate_t::InitForEncoderDevice(), can not create factory." << std::endl;
      return -1;
    }

    BOOST_LOG(info) << "display_delegate_t::InitForEncoderDevice(), factory created." << std::endl;
    factory->EnumAdapters1(0, &adapter_p);
    BOOST_LOG(info) << "display_delegate_t::InitForEncoderDevice(), EnumAdapters1 done." << std::endl;
    dxgi::adapter_t adapter_tmp { adapter_p };
    adapter = std::move(adapter_tmp);

    D3D_FEATURE_LEVEL feature_level;
    status = D3D11CreateDevice(
        adapter_p,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_FLAGS,
        featureLevels, sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
        D3D11_SDK_VERSION,
        &device,
        &feature_level,
        &device_ctx
    );
    BOOST_LOG(info) << "display_delegate_t::InitForEncoderDevice(), D3D11CreateDevice done." << std::endl;
    return 0;
  }
  static bool isDummyDisplay[3] = {false, false, false};
  int
  display_delegate_t::init(mem_type_e hwdevice_type, int displayId,const ::video::config_t &config,
    const std::string &display_name) {
    m_display_name = display_name;
    m_hwdevice_type = hwdevice_type;
    m_pConfig = &config;
    BOOST_LOG(info) << "display_delegate_t::init(), display_name = " << display_name << std::endl;
    m_disp = MakeDisp(hwdevice_type, displayId,config, display_name);
    BOOST_LOG(info) << "display_delegate_t::init(), after calling MakeDisp(), display_name = " << display_name << std::endl;
    if (m_disp) {
      if (!rtsp_stream::isIniting && isDummyDisplay[displayId])
      {
        isDummyDisplay[displayId] = false;
        OnDummyDisplay(displayId, false, display_name);
      }

      BOOST_LOG(info) << "display_delegate_t::init(), MakeDisp() successful, display_name = " << display_name << std::endl;
      width = m_disp->width;
      height = m_disp->height;
      width_after_rotation = m_disp->width_after_rotation;
      height_after_rotation = m_disp->height_after_rotation;
      env_height = m_disp->env_height;
      env_width = m_disp->env_width;
      delay = m_disp->delay;
      return 0;
    }
    else {
      if (displayId == -1) {//just to validate display
        BOOST_LOG(info) << "display_delegate_t::init(), MakeDisp() failed, display_name = " << display_name
                        << " , displayId = " << displayId
                        << ", return -1 as called from ValidateDisplay()"
                        << std::endl;
        return -1;
      }
      if (!rtsp_stream::isIniting && !isDummyDisplay[displayId])
      {
        isDummyDisplay[displayId] = true;
        OnDummyDisplay(displayId, true, display_name);
      }
      BOOST_LOG(info) << "display_delegate_t::init(), MakeDisp() failed, display_name = " << display_name << std::endl;
      InitForEncoderDevice();
      BOOST_LOG(info) << "display_delegate_t::init(), MakeDisp() failed, after InitForEncoderDevice(), display_name = "
                      << display_name
                      << std::endl;
    }
    //dummy mode with VGA
    width = 1920;
    height = 1080;
    return 0;
  }
}  // namespace platf::dxgi
