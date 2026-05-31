#ifndef DEJAVIS_UI_AUDIOPLAYER_H
#define DEJAVIS_UI_AUDIOPLAYER_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include "../multichannel_ringbuffer.h"
#include <functional>
#include "../audio/audio_resampler.h"
#include "../logger.h"
#include "../fs/localfilebrowser.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}


struct AudioFileMetadata {
    std::string title = "";
    std::string artist = "";
    std::string album = "";
    std::string codecName = "";
    int sampleRate = 0;
    int channels = 0;
    long long bitrate = 0;
    double duration = 0.0;
};

struct PlaylistItem {
    std::string filename;
    AudioFileMetadata metadata;
};


class caudioplayer {
public:
    caudioplayer();
    ~caudioplayer();
    void Init(int targetSampleRate, int targetChannels,MultiChannelRingBuffer *_dstbuffer);
    bool LoadFile(std::string _filename);
    void Play();
    void Stop();
    void Pause();

    void NextItem();
    void PrevItem();

    void Seek(double seconds);

    void LoadPlaylistID(int _playlistid);

    void AddToPlaylist(std::string _filename);
    void RemoveFromPlaylist(int _index);
    void SetRepeatMode(int _mode);
    MultiChannelRingBuffer* targetBuffer = nullptr;

    void SetFileBroswerBasePath(std::string _path) {
        FileBrowser.setRootPath(_path);
    }
    CLocalFileBrowser FileBrowser;

    Json::Value getJsonStatus();

private:
    bool m_initalized = false;
    int m_targetSampleRate;
    int m_targetChannels;
    int m_repeatMode = 0;

    std::atomic<bool> isPlaying{false};
    std::atomic<bool> shouldExit{false};
    std::atomic<double> currentPosition{0.0};

    std::atomic<bool> seekRequested{false};
    std::atomic<double> seekTarget{0.0};

    uint32_t m_sourceChannels;

    mutable std::mutex decoderMtx;

    /* AVCODEC */
    std::thread workerThread;
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    int audioStreamIndex = -1;
    AudioResampler Resampler;
    void FileRead_EOF();
    void DecodingLoop();
    void ExtractMetadataFromContext();
    bool ExtractMetadataFromFile(std::string &filename,AudioFileMetadata &data);

    AudioFileMetadata metadata;

    std::string m_currentfilename = "";

    std::vector<PlaylistItem> m_playlist;
    int current_playlist_index = 0;

};


#endif