#include "websocket.h"
#include <drogon/utils/Utilities.h>

CRenderer* CWebSocket::Renderer = nullptr;
CAudio* CWebSocket::Audio = nullptr;
cmilkplaylistdb* CWebSocket::milkPlaylistDB = nullptr;
CAV_ENCODER* CWebSocket::AV_ENCODER = nullptr;
cprojectm_wrapper* CWebSocket::m_projectm_wrapper = nullptr;
std::shared_ptr<WebRTCBroadcaster> CWebSocket::Broadcaster = nullptr;
cunimixer* CWebSocket::m_cunimixer = nullptr;

std::vector<WebSocketConnectionPtr> CWebSocket::clients;
std::mutex CWebSocket::clientsMutex;

CWebSocket::CWebSocket() {

	static std::once_flag once;
	std::call_once(once, [this]() {

		drogon::app().getLoop()->runEvery(1.0, [this]() {
			this->StartStatusTimer();
		});

		LOG_INFO << "Global Status Timer inizializzato correttamente";
	});

}

void CWebSocket::handleNewMessage(const WebSocketConnectionPtr &wsConnPtr,
                                  std::string &&message,
                                  const WebSocketMessageType &type)
{



	Json::Value json;
	Json::Reader reader;

	Json::Value root;
	if (reader.parse(message, root)) {

		if (root["type"] == "webrtc_signal") {
			auto payload = root["payload"];
			std::string subType = payload["type"].asString();
			std::string role = root.get("role", "audio").asString();  // default "audio" per compat

			if (role == "preview") {
				// === NUOVO: preview mixer (video+audio encoded) ===
				if (!Broadcaster) {
					LOG_ERROR << "preview signal received ma Broadcaster non inizializzato";
					return;
				}
				std::string sid = sessionIdOf(wsConnPtr);

				if (subType == "offer") {
					Broadcaster->handleOffer(sid, payload["sdp"].asString());
				}
				else if (subType == "candidate") {
					Broadcaster->handleIceCandidate(sid,
						payload["candidate"].asString(),
						payload.get("sdpMid", "").asString());
				}
				else if (subType == "close") {
					Broadcaster->closeSession(sid);
				}
			}
			else {
				// === ESISTENTE: audio RTCManager ===
				if (subType == "offer") {
					Audio->AUDIO_MIXER.RTCAudio = nullptr;
					if (rtc_session) {
						rtc_session->cleanup();
						rtc_session.reset();
					}
					std::weak_ptr<WebSocketConnection> weakWs = wsConnPtr;

					rtc_session = std::make_shared<RTCManager>([weakWs](const Json::Value& signal) {
						auto ws = weakWs.lock();
						if (ws && ws->connected()) {
							ws->sendJson(signal);
						}
					});

					Audio->AUDIO_MIXER.RTCAudio = rtc_session.get();
					Audio->AUDIO_MIXER.RTCAudio->mixer_sel_value.store(rtc_monitor_id);
					Audio->AUDIO_MIXER.RTCAudio->handleOffer(payload["sdp"].asString());
				}
				else if (subType == "candidate") {
					Audio->AUDIO_MIXER.RTCAudio->handleCandidate(
						payload["candidate"].asString(),
						payload["sdpMid"].asString()
					);
				}
			}
		}
	}
	if (reader.parse(message, json)) {


		if (json["msgid"] == 45) {
			Json::Value ad_json = Audio->getDevicesJson();
			ad_json["msgid"] = 45;
			wsConnPtr->send(ad_json.toStyledString());
		}

		if (json["msgid"] == 51) {

			int deviceId = json["deviceId"].asInt();
			uint32_t channels = json["channels"].asUInt();
			uint32_t samplerate = json["sampleRate"].asUInt();

			int outinit = Audio->startMixerInput(1,deviceId,channels,samplerate);

			Json::Value response;
			response["msgid"] = 51;
			response["status"] = outinit != -1 ? "success" : "error";
			if (outinit == -1) response["message"] = "Failed to initialize input device";

		}

		if (json["msgid"] == 52) {
			int deviceId = json["deviceId"].asInt();
			uint32_t channels = json["channels"].asUInt();
			uint32_t samplerate = json["sampleRate"].asUInt();

			Audio->m_penedingAudioDevLoad.deviceid = deviceId;
			Audio->m_penedingAudioDevLoad.samplerate = samplerate;
			Audio->m_penedingAudioDevLoad.channels = channels;
			Audio->m_penedingAudioDevLoad.shouldLoad.store(true);

		}
		if (json["msgid"] == 53) {
			//Audio->stopMixerInput(1);
		}
		if (json["msgid"] == 54) {
			Audio->stopMasterOut();
            Audio->startMasterDummy();
		}

        if (json["msgid"] == 200) {
            Renderer->reinit_gpuidx = json["gpu_id"].asUInt();
            Renderer->vsync = json["vsync"].asBool();
            Renderer->framelimiter = json["frameLimit"].asBool();
            Renderer->fpstarget = json["fpsValue"].asInt();

            Renderer->reinit = true;
        }

        if (json["msgid"] == 201) {
            Renderer->deinit = true;
        }

        if (json["msgid"] == 202) {


        }



		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_INPUT_VOLUME){
			int _inputidx = json["inputidx"].asInt();
			float _val = json["value"].asFloat();
			//Audio->AUDIO_MIXER.SetInputVolume(_inputidx,_val);
			m_cunimixer->SetInputVolume(_inputidx,_val);
		}

		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_INPUT_ID){
			int _inputidx = json["inputidx"].asInt();
			int _val = json["value"].asInt();
			//Audio->AUDIO_MIXER.SetInputMixerID(_inputidx,_val);
			m_cunimixer->SetInputMixerID(_inputidx,_val);
		}

		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_INPUT_GAIN){
			int _inputidx = json["inputidx"].asInt();
			int _val = json["value"].asInt();
			m_cunimixer->setInputGainFactor(_inputidx,static_cast<GainPreset>(_val));
			//Audio->AUDIO_MIXER.setGainFactor(_inputidx,static_cast<GainPreset>(_val));

		}

		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_INPUT_MUTE){
			int _inputidx = json["inputidx"].asInt();
			int _val = json["value"].asBool();
			//Audio->AUDIO_MIXER.SetInputMute(_inputidx,_val);
			m_cunimixer->SetInputMute(_inputidx,_val);
		}


		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_INPUT_SOLO){
			int _inputidx = json["inputidx"].asInt();
			int _val = json["value"].asBool();
			m_cunimixer->SetInputSolo(_inputidx,_val);

		}

		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_OUTPUT_VOLUME){
			int outputidx = json["outputidx"].asInt();
			float _val = json["value"].asFloat();
			if (outputidx == 0) {
				m_cunimixer->SetMasterVolume(_val);
				//Audio->AUDIO_MIXER.SetMasterVolume(_val);
			}
			if (outputidx == 1) {
				m_cunimixer->SetAuxVolume(_val);
				//Audio->AUDIO_MIXER.SetAuxVolume(_val);
			}
		}


		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_OUTPUT_PAN){
			int outputidx = json["outputidx"].asInt();
			float _val = json["value"].asFloat();
			if (outputidx == 0) {
				m_cunimixer->SetMasterPan(_val);
				//Audio->AUDIO_MIXER.SetMasterPan(_val);
			}
			if (outputidx == 1) {
				m_cunimixer->SetAuxPan(_val);
				//Audio->AUDIO_MIXER.SetAuxPan(_val);
			}
		}

		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_NDI_OUT_LOAD) {
			bool enable = json["value"].asBool();
			int mixoutputidx = json["outputidx"].asInt();

			Audio->AUDIO_MIXER.NDIOutLoad.shouldLoad = true;
			Audio->AUDIO_MIXER.NDIOutLoad.status = enable;
			Audio->AUDIO_MIXER.NDIOutLoad.mixerid = mixoutputidx;

		}

		if(json["msgid"] == 3023) {
			//bool enable = json["enable"].asBool();
			//Audio->AUDIO_MIXER.Mixer_NDI_Output(CAUDIO_MIXER::MIXER_OUTPUTS::OUTPUT_AUX,enable);
		}



		if(getMsgId(json) == DEJAVISUI_MSGID::MIXER_RTC_SOURCE_SEL){
			int sourceidx = json["source"].asInt();
			rtc_monitor_id.store(sourceidx);
			if (Audio->AUDIO_MIXER.RTCAudio) {
				Audio->AUDIO_MIXER.RTCAudio->mixer_sel_value.store(sourceidx);
			}
		}



		if (json["msgid"] == 4011) {
			std::string mypath = json["path"].asString();
			std::string filename = "file://" + mypath;
			//std::string filename = "idle://";
			Renderer->m_presetFileToLoad = mypath;
			Renderer->m_shouldLoadPresetFile = true;

		}


		if (json["msgid"] == 4016) {
			int category_id = json["category_id"].asInt();
			int preset_id = json["preset_id"].asInt();
			milkPlaylistDB->movePresetToCategory(preset_id,category_id);
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);

		}

		if (json["msgid"] == 4017) {
			Json::Value root;
			int category_id = json["category_id"].asInt();
			milkPlaylistDB->deleteCategory(category_id);
			Json::Value mycat = milkPlaylistDB->getCategoriesTreeJson();
			root["msgid"] = 4019;
			root["categories"] = mycat;
			wsConnPtr->sendJson(root);

		}

		if (json["msgid"] == 4018) {
			Json::Value root;
			std::string catname = json["name"].asString();
			int parentid = json["parent_id"].asInt();
			milkPlaylistDB->addCategory(catname,parentid);
			Json::Value mycat = milkPlaylistDB->getCategoriesTreeJson();
			root["msgid"] = 4019;
			root["categories"] = mycat;
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4019) {
			Json::Value root;
			Json::Value mycat = milkPlaylistDB->getCategoriesTreeJson();
			root["msgid"] = 4019;
			root["categories"] = mycat;
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4020) {
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4021) {
			std::string myname = json["name"].asString();
			std::string b64_content = json["b64_content"].asString();
			milkPlaylistDB->addPresetFromData(myname,b64_content);
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);

		}


		if (json["msgid"] == 4023) {
			uint32_t _presetid = json["presetid"].asUInt();
			uint32_t _star = json["star"].asUInt();
			milkPlaylistDB->setPresetStar(_presetid,_star);
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			Json::Value root;
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4024) {
			uint32_t _presetid = json["presetid"].asUInt();
			uint32_t _favorite = json["favorite"].asUInt();
			milkPlaylistDB->setPresetFavorite(_presetid,_favorite);
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4025) {
			int _presetid = json["presetid"].asInt();
			milkPlaylistDB->removePreset(_presetid);
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4026) {
			int _presetid = json["presetid"].asInt();
			Json::Value root;

			std::string presetdata = milkPlaylistDB->getPresetContent(_presetid);
			std::string preset_origname = milkPlaylistDB->getPresetOrigName(_presetid);

			m_projectm_wrapper->m_shouldLoadPresetID = _presetid;
			m_projectm_wrapper->m_presetDataToLoad = presetdata;
			m_projectm_wrapper->m_presetDataToLoadOrigFile = preset_origname;
			m_projectm_wrapper->m_shouldLoadPresetData = true;

		}

		if (getMsgId(json) == DEJAVISUI_MSGID::PRESET_RANDOM) {
			int randid = milkPlaylistDB->getRandom();
			std::string presetdata = milkPlaylistDB->getPresetContent(randid);
			std::string preset_origname = milkPlaylistDB->getPresetOrigName(randid);

			m_projectm_wrapper->m_shouldLoadPresetID = randid;
			m_projectm_wrapper->m_presetDataToLoad = presetdata;
			m_projectm_wrapper->m_presetDataToLoadOrigFile = preset_origname;
			m_projectm_wrapper->m_shouldLoadPresetData = true;
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::PRESET_PREV) {
			int previd = milkPlaylistDB->prevId(m_projectm_wrapper->preset_status.id);
			std::string presetdata = milkPlaylistDB->getPresetContent(previd);
			std::string preset_origname = milkPlaylistDB->getPresetOrigName(previd);

			m_projectm_wrapper->m_shouldLoadPresetID = previd;
			m_projectm_wrapper->m_presetDataToLoad = presetdata;
			m_projectm_wrapper->m_presetDataToLoadOrigFile = preset_origname;
			m_projectm_wrapper->m_shouldLoadPresetData = true;
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::PRESET_NEXT) {
			int nextid = milkPlaylistDB->nextId(m_projectm_wrapper->preset_status.id);
			std::string presetdata = milkPlaylistDB->getPresetContent(nextid);
			std::string preset_origname = milkPlaylistDB->getPresetOrigName(nextid);

			m_projectm_wrapper->m_shouldLoadPresetID = nextid;
			m_projectm_wrapper->m_presetDataToLoad = presetdata;
			m_projectm_wrapper->m_presetDataToLoadOrigFile = preset_origname;
			m_projectm_wrapper->m_shouldLoadPresetData = true;
		}


		if (json["msgid"] == 4040) {
			std::string playlistname = json["playlistname"].asCString();
			milkPlaylistDB->createPlaylist(playlistname);
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4041) {
			uint32_t _playlistid = json["playlistid"].asUInt();
			milkPlaylistDB->deletePlaylist(_playlistid);
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json();
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4042) {
			int _playlistid = json["playlistid"].asInt();
			Json::Value root;
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json(_playlistid);
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}

		if (json["msgid"] == 4043) {
			int _playlistid = json["playlistid"].asInt();
			int _presetid = json["presetid"].asInt();
			Json::Value root;
			milkPlaylistDB->addPresetToPlaylist(_presetid,_playlistid);
			Json::Value mylist = milkPlaylistDB->getAllPresets_Json(_playlistid);
			root["msgid"] = 4020;
			root["presets"] = mylist;
			root["playlists"] = milkPlaylistDB->getAllPlaylist_Json();
			wsConnPtr->sendJson(root);
		}



		if (getMsgId(json) == DEJAVISUI_MSGID::INPUT_LOAD) {
			int inputtype = json["type"].asInt();
			int _mixerid = json["inputidx"].asInt();
			if (inputtype ==  0) {
				m_cunimixer->p_audioplayerLoad.mixerid = _mixerid;
				m_cunimixer->p_audioplayerLoad.shouldLoad = true;
				//m_cunimixer->AddAudioPlayer(_mixerid);
				//Audio->AUDIO_MIXER.AddPlayerToMixer(Audio->fileplayers_basepath,_mixerid);
				//Audio->AUDIO_MIXER.getMixerInputItem(_mixerid)->fileplayer->LoadFile("C:\\msys64\\home\\ceco\\testaudio\\Queen\\Albums\\1984 - The Works (2CD)\\CD1\\09 - Is This The World We Created....flac");
				//Audio->AUDIO_MIXER.getMixerInputItem(_mixerid)->fileplayer->Play();
			}
			if (inputtype ==  3) {
				//Renderer->m_pendingInputLoad.url = Renderer->fileplayers_basepath;
				//Renderer->m_pendingInputLoad.mixerid = _mixerid;
				//Renderer->m_pendingInputLoad.shouldLoad = true;
				//Renderer->AddVideoFilePlayerToMixer(Audio->fileplayers_basepath,_mixerid);
				m_cunimixer->p_videoPlayerLoad.audio_mixerid = _mixerid;
				m_cunimixer->p_videoPlayerLoad.video_mixerid = -1;
				m_cunimixer->p_videoPlayerLoad.shouldLoad = true;


			}

			if (inputtype ==  4) {
				std::string url  = json["url"].asString();
				Renderer->m_pendingVideoUrlload.mixerid = _mixerid;
				Renderer->m_pendingVideoUrlload.url = url;
				Renderer->m_pendingVideoUrlload.shouldLoad = true;


			}
			if (inputtype == 5) {
				Renderer->m_pendingNDI.mixerid = _mixerid;
				Renderer->m_pendingNDI.shouldLoad = true;
				DEJAVISUI_LOG_DEBUG("NDI LOAD");

			}

		}

		if (getMsgId(json) == DEJAVISUI_MSGID::INPUT_UNLOAD) {
			int _mixerid = json["inputidx"].asInt();
			int type = json["value"].asInt();
			if (type == 0) {
				Audio->AUDIO_MIXER.RemoveFromMixer(_mixerid);
			}else if (type == 3) {
				//Renderer->m_peningUnload.mixerid = _mixerid;
				//Renderer->m_peningUnload.shouldUnLoad = true;
				m_cunimixer->p_videoMixerUnLoad.audio_mixerid = _mixerid;
				m_cunimixer->p_videoMixerUnLoad.shouldUnLoad = true;
			}else if (type == 5) {
				int videomixeridx = Audio->AUDIO_MIXER.getMixerInputItem(_mixerid)->videomixer_idx;
				Renderer->m_pendingNDI_Unload.mixerid = videomixeridx;
				Renderer->m_peningUnload.shouldUnLoad = true;
			}
		}


		if (getMsgId(json) == DEJAVISUI_MSGID::INPUT_BROWSE) {

			int _mixerid = json["idx"].asInt();
			int _type = json["type"].asInt();
			std::string mypath = json["path"].asString();

			if (_type == 0) {
				caudioplayer * player = m_cunimixer->audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid)->fileplayer;

				if (player) {
					Json::Value fb_json = player->FileBrowser.browse(mypath);
					fb_json["msgid"] = static_cast<int>(DEJAVISUI_MSGID::INPUT_BROWSE);
					fb_json["idx"] = _mixerid;
					wsConnPtr->sendJson(fb_json);
				}
			}
			if (_type == 3) {
				int videomixerid = m_cunimixer->audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid)->videomixer_idx;
				CAV_DECODER *avdecoder =  Renderer->videoMixerTextures[videomixerid].AV_DECODER;

				if (avdecoder) {
					Json::Value fb_json = avdecoder->FileBrowser.browse(mypath);
					fb_json["msgid"] = static_cast<int>(DEJAVISUI_MSGID::INPUT_BROWSE);
					fb_json["idx"] = _mixerid;
					wsConnPtr->sendJson(fb_json);
				}
			}
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::INPUT_LOAD_FILE) {
			printf("%s \r\n",json.toStyledString().c_str());
			int _mixerid = json["idx"].asInt();
			int _type = json["type"].asInt();

			if (_type == 0) {
				caudioplayer * player = m_cunimixer->audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid)->fileplayer;

				if (player) {
					std::string path = json["path"].asString();
					player->LoadFile(path);
					player->Play();
				}
			}
			if (_type == 3) {
				std::string path = json["path"].asString();

				m_cunimixer->p_videoPlayerFileLoad.audio_mixerid = _mixerid;
				m_cunimixer->p_videoPlayerFileLoad.video_mixerid = -1;
				m_cunimixer->p_videoPlayerFileLoad.path = path;
				m_cunimixer->p_videoPlayerFileLoad.shouldLoad = true;

			}

		}


		if (getMsgId(json) == DEJAVISUI_MSGID::INPUT_SEEK) {
			printf("%s \r\n",json.toStyledString().c_str());
			int _mixerid = json["idx"].asInt();
			int _type = json["type"].asInt();
			double secs = json["sec"].asDouble();
			if (_type == 0) {
				caudioplayer * player = m_cunimixer->audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid)->fileplayer;

				if (player) {
					player->Seek(secs);
				}
			}
			if (_type == 3) {
				int videoidx = m_cunimixer->audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid)->videomixer_idx;
				Renderer->videoMixerTextures[videoidx].AV_DECODER->seek(secs);
			}

		}

		if (json["msgid"] == 6000) {
			//Audio->refreshDevices();

			Json::Value ad_json = Audio->getDevicesJson();
			ad_json["msgid"] = 45;
			wsConnPtr->send(ad_json.toStyledString());
		}


		if (getMsgId(json) == DEJAVISUI_MSGID::AUDIO_MIXER_LIVE_INPUT_START) {
			int _mixerid = json["inputidx"].asInt();
			int _deviceid = json["devid"].asInt();
			int _samplerate = json["samplerate"].asInt();
			int _channels = json["channels"].asInt();

			m_cunimixer->audio_ref->startMixerInput(_mixerid,_deviceid,_channels,_samplerate);

		}
		if (getMsgId(json) == DEJAVISUI_MSGID::AUDIO_MIXER_LIVE_INPUT_STOP) {
			int _mixerid = json["inputidx"].asInt();
			m_cunimixer->audio_ref->stopMixerInput(_mixerid);

		}


		if (getMsgId(json) == DEJAVISUI_MSGID::AUDIO_MASTER_OUT_CTRL) {
			int mytype = json["type"].asInt();
			int myidx = json["outputidx"].asInt();
			int deviceId = json["devid"].asInt();
			uint32_t channels = json["channels"].asUInt();
			uint32_t samplerate = json["samplerate"].asUInt();
			m_cunimixer->audio_ref->m_penedingAudioDevLoad.deviceid = deviceId;
			m_cunimixer->audio_ref->m_penedingAudioDevLoad.outputtype = 0;
			if (mytype == 0 ) m_cunimixer->audio_ref->m_penedingAudioDevLoad.deviceid == -1;
			m_cunimixer->audio_ref->m_penedingAudioDevLoad.samplerate = samplerate;
			m_cunimixer->audio_ref->m_penedingAudioDevLoad.channels = channels;
			m_cunimixer->audio_ref->m_penedingAudioDevLoad.shouldLoad.store(true);

		}

		if (getMsgId(json) == DEJAVISUI_MSGID::VIDEO_VISIBLE) {
			int inputidx = json["input_index"].asInt();
			bool inputstatus = json["isVisible"].asBool();

			m_cunimixer->video_ref->videoMixerTextures[inputidx].isVisible = inputstatus;
		}



		if (getMsgId(json) == DEJAVISUI_MSGID::VIDEO_CHROMA_KEY) {
			KeyerPushConstants params;
			int video_mixer_idx = json["input_index"].asInt();
			const Json::Value& data = json["params"];

			params.v0      = data.get("v0", 0.0f).asFloat();
			params.v1      = data.get("v1", 1.0f).asFloat();
			params.v2      = data.get("v2", 0.0f).asFloat();
			params.threshold = data.get("threshold", 0.0f).asFloat();
			params.softness  = data.get("softness", 0.1f).asFloat();
			params.spill     = data.get("spill", 0.5f).asFloat();
			params.enabled   = data.get("enabled", 0.0f).asFloat();
			m_cunimixer->SetChromaKey(video_mixer_idx,params);

		}



		if (getMsgId(json) == DEJAVISUI_MSGID::VIDEO_LUMA_KEY) {
			LumaKeyParams params;
			int video_mixer_idx = json["input_index"].asInt();
			const Json::Value& data = json["params"];
			params.invert = data.get("invert", 0.0f).asFloat();
			params.lower = data.get("lower", 0.0f).asFloat();
			params.upper = data.get("upper", 1.0f).asFloat();
			params.softness  = data.get("softness", 0.1f).asFloat();
			params.enabled   = data.get("enabled", 0.0f).asFloat();
			m_cunimixer->SetLumaKey(video_mixer_idx,params);
		}


		if (getMsgId(json) == DEJAVISUI_MSGID::VIDEO_COLOR_CTRL) {
			ColorParams params;
			int video_mixer_idx = json["input_index"].asInt();
			const Json::Value& data = json["params"];
			params.brightness = data.get("brightness", 0.0f).asFloat();
			params.contrast   = data.get("contrast", 1.0f).asFloat();
			params.saturation = data.get("saturation", 1.0f).asFloat();
			params.gamma      = data.get("gamma", 1.0f).asFloat();
			params.hueShift   = data.get("hueShift", 0.0f).asFloat();
			params.blackLevel = data.get("blackLevel", 0.0f).asFloat();
			params.whiteLevel = data.get("whiteLevel", 1.0f).asFloat();
			params.enabled    = data.get("enabled", 0.0f).asFloat();
			m_cunimixer->SetColor(video_mixer_idx,params);
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::VIDEO_PROPS_UPDATE) {

			int idx = json["input_index"].asInt();

			videomixeritem tmpret =  Renderer->videoMixerTextures[idx];
			tmpret.pos_x = json["pos_x"].asFloat();
			tmpret.pos_y = json["pos_y"].asFloat();
			tmpret.scale_x= json["scale_x"].asFloat();
			tmpret.scale_y = json["scale_y"].asFloat();
			tmpret.alpha = json["alpha"].asFloat();
			tmpret.layer = json["layer"].asInt();
			m_cunimixer->SetVideoMixerProps(tmpret,idx);
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::NDI_SOURCE_LOAD) {
			Renderer->m_pendingNDISource.mixerid = json["input_index"].asInt();
			Renderer->m_pendingNDISource.url = json["source_name"].asString();
			Renderer->m_pendingNDISource.shouldLoad.store(true);
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::NDI_SOURCE_UNLOAD) {
			Renderer->m_pendingNDI_Unload.mixerid = json["video_index"].asInt();
			Renderer->m_pendingNDI_Unload.isvideomixer = true;
			Renderer->m_pendingNDI_Unload.shouldUnLoad.store(true);

		}

		if (json["command"].asString() == "stop_input_audio") {
			//Audio->stopMixerInput(1);
		}
		if (json["command"].asString() == "stop_output_audio") {
			m_cunimixer->audio_ref->stopMasterOut();
		}

		if (getMsgId(json) == DEJAVISUI_MSGID::VIDEO_IMAGE_UNLOAD) {
			int videomixeridx = json["input_index"].asInt();
			Renderer->m_pendingImageUnLoad.mixerid = videomixeridx;
			Renderer->m_pendingImageUnLoad.shouldUnLoad.store(true);
		}

	}
    DEJAVISUI_LOG_DEBUG("new websocket message: %s",message.c_str());

}

void CWebSocket::handleConnectionClosed(const WebSocketConnectionPtr &conn)
{
    DEJAVISUI_LOG_DEBUG("websocket closed!");
	if (Broadcaster) {
		Broadcaster->closeSession(sessionIdOf(conn));
	}
	std::lock_guard<std::mutex> lock(clientsMutex);
    clients.erase(std::remove(clients.begin(), clients.end(), conn), clients.end());
}

void CWebSocket::handleNewConnection(const HttpRequestPtr &req,
                                        const WebSocketConnectionPtr &conn)
{
    DEJAVISUI_LOG_DEBUG("new websocket connection!");
	std::lock_guard<std::mutex> lock(clientsMutex);
    clients.push_back(conn);
}

Json::Value CWebSocket::genStatusJson(){

	Json::Value audioData = Audio->getStatusJson();
	Json::Value videoData = Renderer->getRendererStatusJson();
	Json::Value root;
	root["audio"] = audioData;
	root["video"] = videoData;


	std::cout << root << std::endl;

	return root;
}

std::string CWebSocket::sessionIdOf(const WebSocketConnectionPtr& conn) {
	return std::to_string(reinterpret_cast<uintptr_t>(conn.get()));
}

void CWebSocket::initBroadcaster() {
	if (Broadcaster) return;
	if (!AV_ENCODER) {
		DEJAVISUI_LOG_ERROR("initBroadcaster: AV_ENCODER not inited");
		return;
	}

	auto signaling_cb = [](const std::string& sid,
						   const std::string& type,
						   const std::string& payload_str) {
		// payload_str è già JSON (vedi webrtc_session.cpp)
		Json::Value payload;
		Json::Reader reader;
		if (!reader.parse(payload_str, payload)) {
			LOG_ERROR << "signaling_cb: parse fallito";
			return;
		}

		Json::Value msg;
		msg["type"] = "webrtc_signal";
		msg["role"] = "preview";
		msg["session_id"] = sid;
		// type vale "answer" o "ice"
		// mappa a sub-type coerente col path "audio" già esistente
		if (type == "answer") {
			payload["type"] = "answer";
		} else {
			payload["type"] = "candidate";
		}
		msg["payload"] = payload;

		std::shared_ptr<std::string> sid_copy = std::make_shared<std::string>(sid);
		std::shared_ptr<Json::Value> msg_copy = std::make_shared<Json::Value>(std::move(msg));

		drogon::app().getLoop()->runInLoop([sid_copy, msg_copy]() {
			std::lock_guard<std::mutex> lock(CWebSocket::clientsMutex);
			for (auto& conn : CWebSocket::clients) {
				if (CWebSocket::sessionIdOf(conn) == *sid_copy) {
					if (conn->connected()) {
						conn->sendJson(*msg_copy);
					}
					break;
				}
			}
		});
	};

	Broadcaster = std::make_shared<WebRTCBroadcaster>(AV_ENCODER, signaling_cb);
	Broadcaster->start();
	DEJAVISUI_LOG_DEBUG("WebRTC preview broadcaster started");
}


void CWebSocket::initCallbacks() {

}


void CWebSocket::StartStatusTimer() {

		if (!this->Audio || !this->Renderer) return;
		if (this->Audio) {
			Json::Value audioData = Audio->getStatusJson();
			Json::Value videoData = Renderer->getRendererStatusJson();
			Json::Value av_encder_json = AV_ENCODER->getStatus();
			Json::Value projectm_json = m_projectm_wrapper->getStatusJson();
			Json::Value root;
			root["msgid"] = 1;
			root["audio"] = audioData;
			root["video"] = videoData;
			root["projectm"] = projectm_json;
			root["av_encoder"] = av_encder_json;

			std::lock_guard<std::mutex> lock(clientsMutex);
			for (auto &conn : clients) {
				if (conn->connected()) {
					conn->sendJson(root);
				}
			}
		}
}

void CWebSocket::broadcastAudioBinary() {

	std::vector<float> input_localBuffer = Audio->AUDIO_MIXER.GetInputVolumes();
	std::vector<float> output_localBuffer = Audio->AUDIO_MIXER.GetOutputVolumes();
	std::vector<float> combinedBuffer(54, 0.0f);

	size_t maxInput = std::min(input_localBuffer.size(), (size_t)46);
	if (maxInput > 0) {
		std::copy(input_localBuffer.begin(), input_localBuffer.begin() + maxInput, combinedBuffer.begin());
	}

	size_t outSize = std::min(output_localBuffer.size(), (size_t)8);
	if (outSize > 0) {

		std::copy(output_localBuffer.begin(), output_localBuffer.begin() + outSize, combinedBuffer.begin() + 46);
	}

    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto &conn : clients) {
        if (conn->connected()) {
            conn->send(reinterpret_cast<const char*>(combinedBuffer.data()),
                       combinedBuffer.size() * sizeof(float),
                       WebSocketMessageType::Binary);
        }
    }
}