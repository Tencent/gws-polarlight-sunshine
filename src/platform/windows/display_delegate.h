//
// Created by Shawn Xiong on 5/7/2023 to support dual displays
// change to display delegate: wrap real display and delegate to it
// if the display is not ready, delegate with fake frames and keep polling
// until the real display is ready
//

#ifndef SUNSHINE_DELEGATE_DISPLAY_H
#define SUNSHINE_DELEGATE_DISPLAY_H

#include "display.h"

namespace platf::dxgi {
  struct img_delegate_t;
  class display_delegate_t: public display_base_t, public std::enable_shared_from_this<display_delegate_t> {
  protected:
    std::shared_ptr<display_base_t> m_disp;
    std::string m_display_name;
    int m_displayId =1;// 1 or 2
    const ::video::config_t* m_pConfig = nullptr;
    mem_type_e m_hwdevice_type = mem_type_e::dxgi;
    void CheckDisplayNameChangedAndModify(std::vector<std::string>& output_list);
    void MakeRealImg(img_delegate_t* img);
  public:
    void setDisplayId(int id) {m_displayId =id;}
    std::shared_ptr<display_base_t> GetRealDisp() {return m_disp;}
#if 0    
    capture_e
    capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<img_t> img, bool *cursor) override {
      if(m_disp) {
        return m_disp->capture(std::move(snapshot_cb),img,cursor);
      }
      else {
        return capture_e::ok;
      }
    }
#endif
    virtual factory1_t& getFactory()
    {
      if(m_disp) {
      return m_disp->getFactory();
      }
      else {
        return factory;
      }
    }
    virtual bool is_hdr() override {
      if(m_disp) {
        return m_disp->is_hdr();
      }
      else {
        return false;
      }
    }
    
    virtual bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
      if(m_disp) {
        return m_disp->get_hdr_metadata(metadata);
      }
      else {
        return false;
      }   
    }

    virtual capture_e
    snapshot(img_t *img_base, std::chrono::milliseconds timeout, bool cursor_visible) override;
    std::shared_ptr<img_t>
    alloc_img() override;
    int
    dummy_img(img_t *img_base) override;
    int
    complete_img(img_t *img_base, bool dummy) override;
    std::vector<DXGI_FORMAT>
    get_supported_sdr_capture_formats() override;
    std::vector<DXGI_FORMAT>
    get_supported_hdr_capture_formats() override;
    std::shared_ptr<platf::hwdevice_t>
    make_hwdevice(pix_fmt_e pix_fmt) override;
    int
    init(mem_type_e hwdevice_type, const ::video::config_t &config, const std::string &display_name);
    int InitForEncoderDevice();
  };
}  // namespace platf::dxgi
#endif  //SUNSHINE_DELEGATE_DISPLAY_H