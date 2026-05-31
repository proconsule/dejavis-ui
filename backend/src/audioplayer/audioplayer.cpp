#include "audioplayer.h"

caudioplayer::caudioplayer() {

}

caudioplayer::~caudioplayer() {
    if (codecContext) { avcodec_free_context(&codecContext); codecContext = nullptr; }
    if (formatContext) { avformat_close_input(&formatContext); formatContext = nullptr; }
}


void caudioplayer::Init(int targetSample, int targetChannels,MultiChannelRingBuffer *_dstbuffer) {
    m_targetSampleRate = targetSample;
    targetBuffer = _dstbuffer;
    m_targetChannels = 2;
    m_initalized = true;
}

void caudioplayer::Play() {
    if (isPlaying) return;
    isPlaying = true;
    shouldExit = false;
    if (!workerThread.joinable()) {
        workerThread = std::thread(&caudioplayer::DecodingLoop, this);
    }
}

void caudioplayer::Stop() {

    shouldExit = true;
    isPlaying = false;
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

bool caudioplayer::LoadFile(std::string _filename) {

    Stop();
    if (codecContext) { avcodec_free_context(&codecContext); codecContext = nullptr; }
    if (formatContext) { avformat_close_input(&formatContext); formatContext = nullptr; }

    audioStreamIndex = -1;
    currentPosition.store(0.0);
    seekRequested.store(false);

    metadata = AudioFileMetadata();


    if (avformat_open_input(&formatContext, _filename.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(formatContext, nullptr) < 0) return false;

    const AVCodec* codec = nullptr;
    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (audioStreamIndex < 0) return false;

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        DEJAVISUI_LOG_ERROR("Impossibile allocare codec context");
        return false;
    }

    int ret = avcodec_parameters_to_context(codecContext, formatContext->streams[audioStreamIndex]->codecpar);
    if (ret < 0) {
        DEJAVISUI_LOG_ERROR("avcodec_parameters_to_context fallito: %s",std::to_string(ret).c_str());
        return false;
    }

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        DEJAVISUI_LOG_ERROR("avcodec_open2 fallito");
        return false;
    }

    AVChannelLayout inLayout;
    av_channel_layout_copy(&inLayout, &codecContext->ch_layout);
    m_sourceChannels = inLayout.nb_channels;
    bool resamplerOk = Resampler.init(codecContext->sample_rate, m_targetSampleRate,
                                          inLayout.nb_channels, m_targetChannels,
                                          codecContext->sample_fmt,AV_SAMPLE_FMT_FLTP);
    av_channel_layout_uninit(&inLayout);
    if (!resamplerOk) {
        DEJAVISUI_LOG_ERROR("Errore inizializzazione resampler input");
        return false;
    }

    m_currentfilename = _filename;
    ExtractMetadataFromContext();

    return true;
}


void caudioplayer::DecodingLoop() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (!shouldExit) {
        std::unique_lock<std::mutex> lock(decoderMtx);

        if (!isPlaying.load() || !formatContext || !codecContext /*|| !swrContext*/) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (seekRequested.load()) {
            int64_t targetPts = static_cast<int64_t>(seekTarget.load() / av_q2d(formatContext->streams[audioStreamIndex]->time_base));
            if (av_seek_frame(formatContext, audioStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD) >= 0) {
                avcodec_flush_buffers(codecContext);
            }
            seekRequested.store(false);
        }

        int readRet = av_read_frame(formatContext, packet);

        if (readRet < 0) {
            if (readRet == AVERROR_EOF) {
                isPlaying = false;
                lock.unlock();
                break;

            } else {
                isPlaying = false;
                lock.unlock();
            }
            continue;
        }

        if (packet->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(codecContext, packet) >= 0) {
                while (avcodec_receive_frame(codecContext, frame) >= 0) {

                    AVFrame* out = Resampler.processFromAVFrame(frame);
                    if (out && targetBuffer) {
                        size_t frames = out->nb_samples;

                        double pts = (frame->pts == AV_NOPTS_VALUE) ? 0.0 :
                            frame->pts * av_q2d(formatContext->streams[audioStreamIndex]->time_base);

                        lock.unlock();


                        while (isPlaying.load() && !shouldExit) {
                            if (targetBuffer->write(
                                    reinterpret_cast<const float* const*>(out->data),
                                    frames)) {
                                break;
                                    }
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }

                        currentPosition.store(pts);
                        lock.lock();
                    }

                }
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    FileRead_EOF();
}

void caudioplayer::ExtractMetadataFromContext() {
    AVDictionaryEntry *tag = nullptr;
    if ((tag = av_dict_get(formatContext->metadata, "title", nullptr, 0))) metadata.title = tag->value;
    if ((tag = av_dict_get(formatContext->metadata, "artist", nullptr, 0))) metadata.artist = tag->value;
    if ((tag = av_dict_get(formatContext->metadata, "album", nullptr, 0))) metadata.album = tag->value;

    metadata.codecName = codecContext->codec->long_name;
    metadata.sampleRate = codecContext->sample_rate;
    metadata.channels = codecContext->ch_layout.nb_channels;
    metadata.bitrate = formatContext->bit_rate;
    metadata.duration = static_cast<double>(formatContext->duration) / AV_TIME_BASE;
}

void caudioplayer::Seek(double seconds) {
    std::lock_guard<std::mutex> lock(decoderMtx);
    if (formatContext && audioStreamIndex != -1) {
        seekTarget.store(seconds);
        seekRequested.store(true);
    }
}

void caudioplayer::LoadPlaylistID(int _playlistid) {
    LoadFile(m_playlist[_playlistid].filename);
    Play();
}

void caudioplayer::NextItem() {
    if (!m_playlist.empty()) {
        Stop();
        if (current_playlist_index < m_playlist.size() - 1) {
            current_playlist_index++;
            LoadFile(m_playlist[current_playlist_index].filename);
            Play();
        } else if (current_playlist_index == m_playlist.size() - 1) {
            current_playlist_index = 0;
            LoadFile(m_playlist[current_playlist_index].filename);
            Play();
        }
    }
}

void caudioplayer::PrevItem() {
    if (!m_playlist.empty()) {
        Stop();
        if (current_playlist_index > 0) {
            current_playlist_index--;
            LoadFile(m_playlist[current_playlist_index].filename);
            Play();
        }else if (current_playlist_index == 0) {
            current_playlist_index = m_playlist.size() - 1;
            LoadFile(m_playlist[current_playlist_index].filename);
        }
    }
}

bool caudioplayer::ExtractMetadataFromFile(std::string &filename, AudioFileMetadata &data) {

    AVFormatContext* tmp_formatContext = nullptr;
    AVCodecContext* tmp_codecContext = nullptr;
    const AVCodec* codec = nullptr;
    AVDictionaryEntry *tag = nullptr;
    int tmp_audioStreamIndex = -1;
    bool success = false;

    if (avformat_open_input(&tmp_formatContext, filename.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(tmp_formatContext, nullptr) < 0) {
        goto cleanup;
    }

    tmp_audioStreamIndex = av_find_best_stream(tmp_formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (tmp_audioStreamIndex < 0) {
        goto cleanup;
    }

    tmp_codecContext = avcodec_alloc_context3(codec);
    if (!tmp_codecContext) {
        DEJAVISUI_LOG_ERROR("Impossibile allocare codec context");
        goto cleanup;
    }

    if (avcodec_parameters_to_context(tmp_codecContext, tmp_formatContext->streams[tmp_audioStreamIndex]->codecpar) < 0) {
        goto cleanup;
    }

    if (avcodec_open2(tmp_codecContext, codec, nullptr) < 0) {
        goto cleanup;
    }

    if ((tag = av_dict_get(tmp_formatContext->metadata, "title", nullptr, 0))) data.title = tag->value;
    if ((tag = av_dict_get(tmp_formatContext->metadata, "artist", nullptr, 0))) data.artist = tag->value;
    if ((tag = av_dict_get(tmp_formatContext->metadata, "album", nullptr, 0))) data.album = tag->value;

    if (tmp_codecContext->codec) {
        data.codecName = tmp_codecContext->codec->long_name;
    }

    data.sampleRate = tmp_codecContext->sample_rate;
    data.channels = tmp_codecContext->ch_layout.nb_channels;
    data.bitrate = tmp_codecContext->bit_rate;

    data.duration = static_cast<double>(tmp_formatContext->duration) / AV_TIME_BASE;

    success = true;

cleanup:
    if (tmp_codecContext) {
        avcodec_free_context(&tmp_codecContext);
    }
    if (tmp_formatContext) {
        avformat_close_input(&tmp_formatContext);
    }

    return success;
}


void caudioplayer::FileRead_EOF() {
    if (m_repeatMode == 1) {
        LoadFile(m_currentfilename);
        Play();
    }else if (m_repeatMode == 2) {
        NextItem();
    }
}


void caudioplayer::AddToPlaylist(std::string _filename) {
    PlaylistItem item;
    item.filename = _filename;
    if (!ExtractMetadataFromFile(_filename, item.metadata)) {
        DEJAVISUI_LOG_ERROR("Unable to get metadata from %s", _filename.c_str());
    }
}

void caudioplayer::RemoveFromPlaylist(int index) {
    if (index >= 0 && index < (int)m_playlist.size()) {
        if (index >= 0 && index < static_cast<int>(m_playlist.size())) {
            if (index < current_playlist_index) {
                current_playlist_index--;
            }
            m_playlist.erase(m_playlist.begin() + index);
            if (current_playlist_index >= m_playlist.size()) {
                current_playlist_index = m_playlist.size() - 1;
            }
        }
    }
}

Json::Value caudioplayer::getJsonStatus() {

    Json::Value root;

    root["isPlaying"] = isPlaying.load();
    root["position"] = currentPosition.load();
    root["duration"] = metadata.duration;
    root["title"] = metadata.title;
    root["filename"] = m_currentfilename;
    root["bitrate"] = static_cast<Json::Value::Int64>(metadata.bitrate);
    root["codecName"] = metadata.codecName;
    root["sampleRate"] = metadata.sampleRate;
    root["channels"] = metadata.channels;
    root["isResampling"] = true;
    return root;
}
