#include "webrtc_session.h"

#include <json/json.h>

extern "C" {
#include <libavutil/mathematics.h>
}

#include <random>



static uint32_t make_ssrc() {
    std::random_device rd;
    return (uint32_t)((rd() & 0x7FFFFFFF) | 0x10000000);
}

WebRTCSession::WebRTCSession(std::string id, bool include_audio,
                             bool use_hevc, SignalingCallback cb)
    : m_id(std::move(id))
    , m_include_audio(include_audio)
    , m_use_hevc(use_hevc)
    , m_signaling_cb(std::move(cb))
{}

WebRTCSession::~WebRTCSession() {
    close();
}

bool WebRTCSession::start() {
    rtc::Configuration config;

    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    m_pc = std::make_shared<rtc::PeerConnection>(config);

    m_pc->onLocalDescription([this](rtc::Description desc) {
        DEJAVISUI_LOG_DEBUG("[WebRTC] Answer generata, impacchetto in JSON...");

        std::string sdp(desc);


        size_t pos = 0;
        while ((pos = sdp.find("a=setup:actpass", pos)) != std::string::npos) {
            sdp.replace(pos, 15, "a=setup:passive");
            pos += 15; // Avanza oltre la sostituzione
        }

        DEJAVISUI_LOG_DEBUG("[WebRTC] SDP corretto (setup:passive) pronto per l'invio.");

        Json::Value root;
        root["type"] = "answer";
        root["sdp"] = sdp;

        Json::FastWriter writer;
        std::string payload = writer.write(root);

        // Ora il payload è un JSON valido e signaling_cb non fallirà il parse
        m_signaling_cb(m_id, "answer", payload);
    });

    m_pc->onLocalCandidate([this](rtc::Candidate cand) {
        Json::Value ice;
        ice["candidate"] = cand.candidate();
        ice["mid"] = cand.mid();
        Json::FastWriter writer;
        m_signaling_cb(m_id, "ice", writer.write(ice));
    });

    m_pc->onStateChange([this](rtc::PeerConnection::State state) {
        DEJAVISUI_LOG_DEBUG("[WebRTC] Stato cambiato in: %d", (int)state);
        if (state == rtc::PeerConnection::State::Connected) {
            m_open.store(true);
        } else if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) {
            m_open.store(false);
        }
    });

    auto onVideoOpen = [this]() {
        DEJAVISUI_LOG_DEBUG("[WebRTC] Canale Video Aperto. Richiedo Keyframe.");
        if (m_keyframe_cb) m_keyframe_cb();
    };

    auto onAudioOpen = [this]() {
        DEJAVISUI_LOG_DEBUG("[WebRTC] Canale Audio Aperto.");
    };

    uint8_t payload = m_use_hevc ? 49 : 103;
    auto videoData = addVideo(m_pc, payload, make_ssrc(), "0", "stream0", onVideoOpen);
    m_video_track = videoData->track;
    m_video_cfg = videoData->sender->rtpConfig;

    if (m_include_audio) {
        auto audioData = addAudio(m_pc, 111, make_ssrc(), "1", "stream0", onAudioOpen);
        m_audio_track = audioData->track;
        m_audio_cfg = audioData->sender->rtpConfig;
    }

    return true;
}

void WebRTCSession::close() {
    std::lock_guard<std::mutex> lock(m_mu);
    if (m_pc) {
        try { m_pc->close(); } catch (...) {}
        m_pc.reset();
    }
    m_video_track.reset();
    m_audio_track.reset();
    m_video_packetizer.reset();
    m_audio_packetizer.reset();
    m_video_cfg.reset();
    m_audio_cfg.reset();
    m_open.store(false);
}

void WebRTCSession::onRemoteOffer(const std::string& sdp) {
    DEJAVISUI_LOG_DEBUG("[WebRTC %p] Remote offer set. Generating local answer...", this);
    try {

       m_pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));


        m_pc->setLocalDescription();

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("[WebRTC] Errore durante setRemoteDescription: %s", e.what());
    }
}

void WebRTCSession::onRemoteIceCandidate(const std::string& cand,
                                         const std::string& mid) {
    if (!m_pc) return;
    try {
        m_pc->addRemoteCandidate(rtc::Candidate(cand, mid));
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("[WebRTC %s] addRemoteCandidate failed: %s",
                            m_id.c_str(), e.what());
    }
}

void WebRTCSession::pushVideo(const AVPacket* pkt, AVRational tb) {
    if (!m_video_track || !m_video_track->isOpen()) return;

    uint8_t nalu_type = pkt->data[4] & 0x1F;

    static auto last_push = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_push).count();
    last_push = now;

    try {

        uint32_t rtp_ts = (uint32_t)av_rescale_q(pkt->pts, tb, {1, 90000});

        m_video_cfg->timestamp = rtp_ts;

        bool success = m_video_track->send((const std::byte*)pkt->data, pkt->size);
        if (!success) {
            DEJAVISUI_LOG_DEBUG("[WebRTC] Invio fallito - Buffer pieno?");
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("[WebRTC] Errore send: %s", e.what());
    }
}

void WebRTCSession::pushAudio(const AVPacket* pkt, AVRational tb) {
    if (!m_open.load() || !m_audio_track || !m_audio_track->isOpen()) return;
    if (!pkt || pkt->size <= 0 || !pkt->data) return;

    int64_t rtp_ts = av_rescale_q(pkt->pts, tb, {1, 48000});
    if (m_audio_cfg) m_audio_cfg->timestamp = (uint32_t)rtp_ts;

    rtc::binary data(reinterpret_cast<const std::byte*>(pkt->data),
                     reinterpret_cast<const std::byte*>(pkt->data) + pkt->size);
    try {
        m_audio_track->send(data);
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("[WebRTC %s] audio send failed: %s",
                            m_id.c_str(), e.what());
    }
}

ClientTrackData::ClientTrackData(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sender) {
    this->track = track;
    this->sender = sender;
}

std::shared_ptr<ClientTrackData> WebRTCSession::addVideo(const std::shared_ptr<rtc::PeerConnection> pc,
                                                        const uint8_t payloadType, const uint32_t ssrc,
                                                        const std::string cname, const std::string msid,
                                                        const std::function<void (void)> onOpen) {
    auto video = rtc::Description::Video(cname);
    if (m_use_hevc) {
        video.addH265Codec(payloadType);
    } else {
        video.addH264Codec(payloadType);
    }
    video.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(video);

    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, 90000);

    std::shared_ptr<rtc::MediaHandler> packetizer;
    if (m_use_hevc) {
        packetizer = std::make_shared<rtc::H265RtpPacketizer>(rtc::NalUnit::Separator::LongStartSequence, rtpConfig);
    } else {
        packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::LongStartSequence, rtpConfig);
    }

    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);

    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);

    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<ClientTrackData>(track, srReporter);
}

std::shared_ptr<ClientTrackData> WebRTCSession::addAudio(const std::shared_ptr<rtc::PeerConnection> pc, const uint8_t payloadType, const uint32_t ssrc, const std::string cname, const std::string msid, const std::function<void (void)> onOpen) {
    auto audio = rtc::Description::Audio(cname);
    audio.addOpusCodec(payloadType);
    audio.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(audio);
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, rtc::OpusRtpPacketizer::DefaultClockRate);
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);
    auto trackData = std::make_shared<ClientTrackData>(track, srReporter);
    return trackData;
}
