// Created by loki on 6/5/19.

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include <boost/asio.hpp>

#include "audio.h"
#include "crypto.h"
#include "video.h"

namespace stream {
  constexpr auto VIDEO_STREAM_PORT = 9;
  constexpr auto CONTROL_PORT = 10;
  constexpr auto AUDIO_STREAM_PORT = 11;
  constexpr auto VIDEO_STREAM_PORT2 = 12;

  struct session_t;
  struct config_t {
    audio::config_t audio;
    video::config_t monitor;

    int packetsize;
    int minRequiredFecPackets;
    int featureFlags;
    int controlProtocolType;
    int audioQosType;
    int videoQosType;

    std::optional<int> gcmap;
  };

  namespace session {
    enum class state_e : int {
      STOPPED,
      STOPPING,
      STARTING,
      RUNNING,
    };

    std::shared_ptr<session_t> alloc(config_t &config, crypto::aes_t &gcm_key, crypto::aes_t &iv,char * AesKey);
    int
    start(session_t &session, const std::string &addr_string);
    void
    stop(session_t &session);
    void
    join(session_t &session);
    state_e
    state(session_t &session);

    void Inc_Unsafe_stop();
    void Dec_Unsafe_stop();
  }  // namespace session
}  // namespace stream

#endif  // SUNSHINE_STREAM_H
