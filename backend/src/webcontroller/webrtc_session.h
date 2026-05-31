#ifndef DEJAVIS_APP_WEBRTC_SESSION_H
#define DEJAVIS_APP_WEBRTC_SESSION_H

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <functional>
#include <rtc/rtc.hpp>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "../logger.h"

struct ClientTrackData {
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::RtcpSrReporter> sender;

    ClientTrackData(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sender);
};


class WebRTCSession {
public:
    using SignalingCallback =
        std::function<void(const std::string& session_id,
                           const std::string& msg_type,   // "answer" | "ice"
                           const std::string& payload_json)>;

    WebRTCSession(std::string session_id,
                  bool include_audio,
                  SignalingCallback signaling_cb);

    ~WebRTCSession();

    bool start();                           // crea PeerConnection e tracks
    void close();                           // chiude tutto
    bool isOpen() const { return m_open.load(); }
    const std::string& id() const { return m_id; }

    void onRemoteOffer(const std::string& sdp);
    void onRemoteIceCandidate(const std::string& cand, const std::string& mid);

    void pushVideo(const AVPacket* pkt, AVRational tb);
    void pushAudio(const AVPacket* pkt, AVRational tb);

    using KeyframeRequestCallback = std::function<void()>;
    void setKeyframeRequestCallback(KeyframeRequestCallback cb) {
        m_keyframe_cb = std::move(cb);
    }

    std::shared_ptr<ClientTrackData> addVideo(const std::shared_ptr<rtc::PeerConnection> pc, const uint8_t payloadType, const uint32_t ssrc, const std::string cname, const std::string msid, const std::function<void (void)> onOpen);
    std::shared_ptr<ClientTrackData> addAudio(const std::shared_ptr<rtc::PeerConnection> pc, const uint8_t payloadType, const uint32_t ssrc, const std::string cname, const std::string msid, const std::function<void (void)> onOpen);
private:
    std::string m_id;
    bool m_include_audio;
    SignalingCallback m_signaling_cb;
    KeyframeRequestCallback m_keyframe_cb;

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::Track> m_video_track;
    std::shared_ptr<rtc::Track> m_audio_track;

    std::shared_ptr<rtc::H264RtpPacketizer> m_video_packetizer;
    std::shared_ptr<rtc::OpusRtpPacketizer> m_audio_packetizer;
    std::shared_ptr<rtc::RtpPacketizationConfig> m_video_cfg;
    std::shared_ptr<rtc::RtpPacketizationConfig> m_audio_cfg;

    std::atomic<bool> m_open{false};
    std::mutex m_mu;
};

#endif