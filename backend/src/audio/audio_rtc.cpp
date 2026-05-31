#include "audio_rtc.h"
#include <iostream>

#include "backend/src/logger.h"

RTCManager::RTCManager(SignalingCallback cb) : sendSignal(cb) {
    setupPeerConnection();
}

RTCManager::~RTCManager() {
    if (dataChannel) dataChannel->close();
    if (peerConnection) peerConnection->close();
}

void RTCManager::setupPeerConnection() {
    rtc::Configuration config;
    config.iceServers.clear();
    peerConnection = std::make_shared<rtc::PeerConnection>(config);

    peerConnection->onLocalCandidate([this](rtc::Candidate candidate) {
        Json::Value signal;
        signal["type"] = "webrtc_signal";
        signal["payload"]["type"] = "candidate";
        signal["payload"]["candidate"] = std::string(candidate);
        signal["payload"]["sdpMid"] = candidate.mid();
        sendSignal(signal);
    });

    peerConnection->onStateChange([this](rtc::PeerConnection::State state) {
        DEJAVISUI_LOG_DEBUG("PeerConnection status: %d", (int)state);
        if (state == rtc::PeerConnection::State::Closed ||
            state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Disconnected) {

            DEJAVISUI_LOG_DEBUG("Connection lost, resetting stuff..");
            this->cleanup();
        }
    });


    peerConnection->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        if (dc->label() == "audio_monitor") {
            this->dataChannel = dc; // Salviamo il riferimento

            this->dataChannel->onOpen([this]() {
                this->isStreaming = true;
                DEJAVISUI_LOG_DEBUG("WebRTC pronto a ricevere dati dal browser");
            });

            this->dataChannel->onClosed([this]() {
                DEJAVISUI_LOG_DEBUG("DataChannel clsed");
                this->isStreaming = false;
            });

        }
    });
}

void RTCManager::handleOffer(const std::string& sdpStr) {
    try {
        DEJAVISUI_LOG_DEBUG("Received: %s", sdpStr.c_str());

        peerConnection->onLocalDescription([this](rtc::Description description) {
            std::string sdpAnswer = std::string(description);

            size_t pos = sdpAnswer.find("a=setup:actpass");
            while (pos != std::string::npos) {
                sdpAnswer.replace(pos, 15, "a=setup:passive");
                pos = sdpAnswer.find("a=setup:actpass", pos + 15);
            }

            Json::Value reply;
            reply["type"] = "webrtc_signal";
            reply["payload"]["type"] = "answer";
            reply["payload"]["sdp"] = sdpAnswer;

            DEJAVISUI_LOG_DEBUG("Inviando Answer Corretta (Passive)");
            this->sendSignal(reply);
        });

        peerConnection->setRemoteDescription(rtc::Description(sdpStr, "offer"));

        if (!dataChannel) {
            dataChannel = peerConnection->createDataChannel("audio_monitor");
        }

        peerConnection->setLocalDescription();

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("WebRTC handleOffer: %s", e.what());
    }
}

void RTCManager::handleCandidate(const std::string& cand, const std::string& mid) {
    DEJAVISUI_LOG_DEBUG("Ricevuto candidato dal browser: %s",cand.c_str());
    peerConnection->addRemoteCandidate(rtc::Candidate(cand, mid));
}

void RTCManager::sendAudioFrame(const float* const* planar_data, size_t frames, int channels) {
    if (!isStreaming) return;
    if (!dataChannel || !dataChannel->isOpen()) return;

    const size_t total = frames * (size_t)channels;
    if (m_interleaveBuffer.size() < total) m_interleaveBuffer.resize(total);

    float* dst = m_interleaveBuffer.data();
    for (size_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < channels; ch++) {
            dst[i * channels + ch] = planar_data[ch][i];
        }
    }

    try {
        dataChannel->send((const rtc::byte*)dst, total * sizeof(float));
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore invio DC: %s", e.what());
        stopStreaming();
    }
}

void RTCManager::cleanup() {
    this->isStreaming = false;

    if (dataChannel) {
        dataChannel->close();
        dataChannel.reset();
    }

    if (peerConnection) {
        peerConnection->close();
        peerConnection.reset();
    }

    DEJAVISUI_LOG_DEBUG("Backend RTC clened up");
}

void RTCManager::stopStreaming() {
    DEJAVISUI_LOG_DEBUG("RTC streaming stop request");
    isStreaming = false;
}