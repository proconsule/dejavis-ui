#ifndef DEJAVIS_APP_WEBRTC_BROADCASTER_H
#define DEJAVIS_APP_WEBRTC_BROADCASTER_H

#include <unordered_map>
#include <memory>
#include <mutex>
#include "webrtc_session.h"

class CAV_ENCODER;

class WebRTCBroadcaster {
public:
    using SignalingCallback = WebRTCSession::SignalingCallback;

    explicit WebRTCBroadcaster(CAV_ENCODER* encoder,
                               SignalingCallback signaling_cb);
    ~WebRTCBroadcaster();

    void start();
    void stop();

    void handleOffer(const std::string& session_id, const std::string& sdp);
    void handleIceCandidate(const std::string& session_id,
                            const std::string& cand, const std::string& mid);
    void closeSession(const std::string& session_id);

    size_t sessionCount() const;

private:
    void onEncodedPacket(const AVPacket* pkt, bool is_video, AVRational tb);

    CAV_ENCODER* m_encoder;
    SignalingCallback m_signaling_cb;

    std::unordered_map<std::string, std::shared_ptr<WebRTCSession>> m_sessions;
    mutable std::mutex m_sessions_mu;
};

#endif