import { Disc } from "lucide-react"
import { AudioBufferCard } from "./audioplayer/audiobuffercard"
import {FileExplorer} from "@/components/fileexplorer.tsx";
import {VideoPlayerHeader} from "@/components/videoplayer/videoplayerheader.tsx";
import {useWS} from "@/WebSocketContext.tsx";
import {useEffect, useState} from "react";
import {VideoPlayerControls} from "@/components/videoplayer/videoplayercontrols.tsx";
import {BitrateStatCard} from "@/components/videoplayer/videoplayercards.tsx";
import type {MixerInputType} from "@/components/audio/MixerInput.tsx";


interface VideoPlayerViewProps {
  audio_mixer_idx:number;
  video_mixer_idx: number;
  sendSignal: (msg: any) => void;
}

export function VideoPlayerView({ audio_mixer_idx,video_mixer_idx, sendSignal }: VideoPlayerViewProps) {

  const {lastJsonMessage} = useWS();
  const [Audio,setAudio] = useState<any>(null);
  const [Video,setVideo] = useState<any>(null);
  const [decoder,setDecoder] = useState<any>(null);

  const audio = Audio;
  const video = Video;
  const audio_input:MixerInputType = audio?.mixer?.inputs[audio_mixer_idx];
  const video_input = video?.videomixer.inputs[video_mixer_idx];

  //const decoder = video_input?.file_decoder;
  //const kbps = fileplayer?.bitrate ? Math.round(fileplayer?.bitrate / 1000) : 0;


  console.log(audio_mixer_idx);
  console.log(video_mixer_idx);
  console.log(decoder);


  useEffect(() => {
    sendSignal({msgid: 5003,type: 3, idx: audio_mixer_idx});
  },[]);

  useEffect(() => {
    if (lastJsonMessage?.msgid !== 1) return;
    setAudio(lastJsonMessage.audio);
    setVideo(lastJsonMessage.video);
    if(lastJsonMessage?.video?.videomixer?.inputs[video_mixer_idx].file_decoder) {
      setDecoder(lastJsonMessage?.video?.videomixer?.inputs[video_mixer_idx].file_decoder)
    }
    console.log(lastJsonMessage);
    //sendMessage({msgid: 5003, idx: mixerId});
  }, [lastJsonMessage]);

  return (
    <div className="p-6 max-w-7xl mx-auto animate-in fade-in duration-500">

      <div className="grid grid-cols-12 gap-1">
        <div className="col-span-12 lg:col-span-7 flex flex-col gap-1">

          <div className="bg-[#0f1115] text-white rounded-2xl p-6 shadow-2xl border-b-4 border-purple-600 relative overflow-hidden">
            <div className="absolute top-0 right-0 w-64 h-64 bg-purple-500/10 rounded-full blur-[80px] -mr-32 -mt-32 pointer-events-none" />



            <VideoPlayerHeader
               decoder={decoder}
            />

            <VideoPlayerControls
                audio_input_idx={audio_mixer_idx}
                audio_mixer_input={audio_input}
                video_mixer_input={video_input}

              repeatMode={0}
              isShuffle={false}
              sendSignal={sendSignal}
            />
          </div>

          <div className="grid grid-cols-2 gap-3">

            <BitrateStatCard
              icon={Disc} 
              label="Bitrate"
              videoRaw={decoder?.video_bitrate}
              audioRaw={decoder?.audio_bitrate}
              //unit="kbps"

            />
            <AudioBufferCard 
              label="Decoder Buffer" 
              fill={audio_input?.buffer_used}
              max={audio_input?.buffer_size}
			  sampleRate={decoder?.audio_sampleRate || 44100}
              className=""
            />
          </div>
        </div>
        <div className="col-span-12 lg:col-span-4 flex flex-col">

        </div>
        <div className="col-span-12 lg:col-span-4 flex flex-col h-full">
          <div className="bg-[#0f1115]/50 rounded-2xl border border-white/5 overflow-hidden flex flex-col flex-1">
            {/*
            <PlaylistManager
              queue={playlist?.queue || []} 
              currentTrackId={audio?.currentTrackId ?? playlist?.currentTrackId ?? -1}
              status={status}
              sendSignal={sendSignal}
            />
            */}
          </div>
        </div>

      </div>
      <FileExplorer
          idx={audio_mixer_idx}
          type={audio_input?.type}

      />
    </div>

  )
}