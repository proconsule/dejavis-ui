#include "webrtc_broadcaster.h"
#include "../av_encoder.h"

WebRTCBroadcaster::WebRTCBroadcaster(CAV_ENCODER* encoder,
                                     SignalingCallback signaling_cb)
    : m_encoder(encoder), m_signaling_cb(std::move(signaling_cb)) {}

WebRTCBroadcaster::~WebRTCBroadcaster() {
    stop();
}

void WebRTCBroadcaster::start() {
    if (!m_encoder) return;
    m_encoder->setEncodedPacketCallback(
        [this](const AVPacket* pkt, bool is_video, AVRational tb) {
            onEncodedPacket(pkt, is_video, tb);
        });
}

void WebRTCBroadcaster::stop() {
    if (m_encoder) m_encoder->setEncodedPacketCallback(nullptr);

    std::lock_guard<std::mutex> lock(m_sessions_mu);
    for (auto& [_, sess] : m_sessions) sess->close();
    m_sessions.clear();
}

void WebRTCBroadcaster::onEncodedPacket(const AVPacket* pkt, bool is_video,
                                        AVRational tb) {
    // Snapshot della lista sessioni sotto lock breve, replica fuori dal lock
    std::vector<std::shared_ptr<WebRTCSession>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_sessions_mu);
        snapshot.reserve(m_sessions.size());
        for (auto& [_, s] : m_sessions) snapshot.push_back(s);
    }
    for (auto& s : snapshot) {
        if (is_video) s->pushVideo(pkt, tb);
        else          s->pushAudio(pkt, tb);
    }
}

void WebRTCBroadcaster::handleOffer(const std::string& session_id, const std::string& sdp) {
    std::shared_ptr<WebRTCSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessions_mu);
        auto it = m_sessions.find(session_id);
        if (it != m_sessions.end()) {
            session = it->second;
        } else {
            session = std::make_shared<WebRTCSession>(session_id, true, m_signaling_cb);

            // Registriamo la richiesta di keyframe
            session->setKeyframeRequestCallback([this]() {
                if (m_encoder) {
                    DEJAVISUI_LOG_DEBUG("[Broadcaster] Forza Keyframe per nuova connessione");
                    m_encoder->requestKeyframe();
                }
            });

            session->start();
            m_sessions[session_id] = session;
        }
    }
    // Avvia la negoziazione SDP
    session->onRemoteOffer(sdp);
}

void WebRTCBroadcaster::handleIceCandidate(const std::string& session_id,
                                           const std::string& cand,
                                           const std::string& mid) {
    std::shared_ptr<WebRTCSession> sess;
    {
        std::lock_guard<std::mutex> lock(m_sessions_mu);
        auto it = m_sessions.find(session_id);
        if (it != m_sessions.end()) sess = it->second;
    }
    if (sess) sess->onRemoteIceCandidate(cand, mid);
}

void WebRTCBroadcaster::closeSession(const std::string& session_id) {
    std::shared_ptr<WebRTCSession> sess;
    {
        std::lock_guard<std::mutex> lock(m_sessions_mu);
        auto it = m_sessions.find(session_id);
        if (it != m_sessions.end()) {
            sess = it->second;
            m_sessions.erase(it);
        }
    }
    if (sess) sess->close();
}

size_t WebRTCBroadcaster::sessionCount() const {
    std::lock_guard<std::mutex> lock(m_sessions_mu);
    return m_sessions.size();
}