#ifndef DEJAVIS_APP_AUDIO_RTC_H
#define DEJAVIS_APP_AUDIO_RTC_H

#include <rtc/rtc.hpp>
#include <json/json.h>
#include <memory>
#include <functional>
#include <string>


class RTCManager {
public:
    using SignalingCallback = std::function<void(const Json::Value&)>;

    RTCManager(SignalingCallback cb);
    ~RTCManager();

    void handleOffer(const std::string& sdp);

    void handleCandidate(const std::string& cand, const std::string& mid);

    void sendAudioFrame(const float* const* planar_data, size_t frames, int channels);

    bool isReady() const { return dataChannel && dataChannel->isOpen(); }
    void cleanup();
    void stopStreaming();

    std::atomic<int> mixer_sel_value{0};

private:
    SignalingCallback sendSignal;
    std::shared_ptr<rtc::PeerConnection> peerConnection;
    std::shared_ptr<rtc::DataChannel> dataChannel;
    std::atomic<bool> isStreaming{false};
    void setupPeerConnection();

    std::vector<float> m_interleaveBuffer;
};


#endif //DEJAVIS_APP_AUDIO_RTC_H