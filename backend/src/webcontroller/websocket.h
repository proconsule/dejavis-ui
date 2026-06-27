#ifndef DEJAVISWEBSOCKET_H
#define DEJAVISWEBSOCKET_H

#include <drogon/WebSocketController.h>
#include <drogon/PubSubService.h>
#include <drogon/HttpAppFramework.h>


#include "../video/renderer.h"
#include "../audio/audio.h"
#include "../fs/localfilebrowser.h"
#include "../av_encoder.h"
#include "../db/milkplaylistdb.h"

#include "../audio/audio_rtc.h"
#include "../video/projectm_wrapper.h"

#include "webrtc_broadcaster.h"
#include "backend/src/unimixer.h"

using namespace drogon;

enum class DEJAVISUI_MSGID {
        // General/System
        STATUS = 1,
        GET_SOURCES = 45,

        // Audio Device Management
        AUDIO_INPUT_INIT = 51,
        AUDIO_OUTPUT_INIT = 52,
        AUDIO_INPUT_STOP = 53,
        AUDIO_OUTPUT_STOP = 54,

        // Renderer/GPU
        RENDERER_REINIT = 200,
        RENDERER_DEINIT = 201,
        RENDERER_EMPTY = 202,

        // Audio Mixer Controls
        MIXER_INPUT_VOLUME = 3000,
        MIXER_INPUT_ID = 3002,
        MIXER_INPUT_GAIN = 3003,
        MIXER_INPUT_MUTE = 3005,
        MIXER_INPUT_SOLO = 3006,
        MIXER_OUTPUT_VOLUME = 3020,
        MIXER_OUTPUT_PAN = 3021,
        MIXER_NDI_OUT_LOAD = 3022,
        MIXER_RTC_SOURCE_SEL = 3040,

		MIXER_INPUT_EFFECTADD = 3041,
		MIXER_INPUT_EFFECTSET = 3044,
		MIXER_INPUT_EFFECTDEL = 3042,
		MIXER_INPUT_CLEARBANK = 3043,


        // Preset & Library Management
        PRESET_LOAD_FILE = 4011,
        PRESET_MOVE_CATEGORY = 4016,
        CATEGORY_DELETE = 4017,
        CATEGORY_ADD = 4018,
        CATEGORY_LIST_REQ = 4019,
        PRESET_LIST_REQ = 4020,
        PRESET_ADD_DATA = 4021,
        PRESET_SET_STAR = 4023,
        PRESET_SET_FAVORITE = 4024,
        PRESET_REMOVE = 4025,
        PRESET_LOAD_DATA = 4026,
		PRESET_RANDOM = 4027,
		PRESET_PREV = 4028,
		PRESET_NEXT = 4029,


        // Playlist Management
        PLAYLIST_CREATE = 4040,
        PLAYLIST_DELETE = 4041,
        PLAYLIST_GET_PRESETS = 4042,
        PLAYLIST_ADD_PRESET = 4043,

        // Mixer Input Management (Loading/Unloading)
        INPUT_LOAD = 5001,
        INPUT_UNLOAD = 5002,
        INPUT_BROWSE = 5003,
        INPUT_LOAD_FILE = 5004,
		INPUT_BROWSE_CURRENT = 5005,
        INPUT_SEEK = 5008,

        // Audio Device Extensions
        AUDIO_REFRESH_DEVICES = 6000,
        AUDIO_MIXER_LIVE_INPUT_START = 6001,
        AUDIO_MIXER_LIVE_INPUT_STOP = 6002,
        AUDIO_MASTER_OUT_CTRL = 7000,

        // Video Mixer Properties
        VIDEO_VISIBLE = 8002,
        VIDEO_CHROMA_KEY = 8003,
        VIDEO_LUMA_KEY = 8004,
        VIDEO_COLOR_CTRL = 8005,
        VIDEO_PROPS_UPDATE = 8001,
        VIDEO_IMAGE_UNLOAD = 8011,
		VIDEO_IMAGE_DOWNSCALE_BICUBIC = 8020,

        // NDI
        NDI_SOURCE_LOAD = 9000,
		NDI_SOURCE_UNLOAD = 9001,

        // Legacy/Originals
        FILELIST = 41,
        AUDIOINPUTLIST = 42,
        AUDIOOUTPUTLIST = 43,
        PLAYLIST = 44
    };


class CWebSocket : public drogon::WebSocketController<CWebSocket>
{
  public:
	CWebSocket();

    static CRenderer *Renderer;
	static CAudio *Audio;

	static cmilkplaylistdb *milkPlaylistDB;

	static CAV_ENCODER *AV_ENCODER;

	static cprojectm_wrapper *m_projectm_wrapper;

	static cunimixer *m_cunimixer;

	static std::shared_ptr<WebRTCBroadcaster> Broadcaster;
    void handleNewMessage(const WebSocketConnectionPtr &,
                          std::string &&,
                          const WebSocketMessageType &) override;
    void handleConnectionClosed(const WebSocketConnectionPtr &) override;
    void handleNewConnection(const HttpRequestPtr &,
                             const WebSocketConnectionPtr &) override;
							 
	
	Json::Value genStatusJson();
	static void initCallbacks();

	static void sendToConnection(const WebSocketConnectionPtr& conn,
								 const Json::Value& msg);


	static void initBroadcaster();

	void StartStatusTimer();
	
	static std::vector<WebSocketConnectionPtr> clients;
	static std::mutex clientsMutex; // Protegge l'accesso al vettore
    static void broadcastAudioBinary();
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/dist", Get);
    WS_ADD_PATH_VIA_REGEX("/[^/]*", Get);
    WS_PATH_LIST_END
	
	enum class MSGID { FILELIST = 41, AUDIOINPUTLIST = 42, AUDIOOUTPUTLIST = 43,PLAYLIST = 44, GETSOURCES = 45 };

	std::shared_ptr<RTCManager> rtc_session;
	std::atomic<int> rtc_monitor_id{0};

	static std::unordered_map<std::string, WebSocketConnectionPtr> ws_connections;
	static std::mutex ws_connections_mutex;

	static std::string sessionIdOf(const WebSocketConnectionPtr& conn);

	inline DEJAVISUI_MSGID getMsgId(const Json::Value& json) {
		return static_cast<DEJAVISUI_MSGID>(json["msgid"].asInt());
	}
};

#endif