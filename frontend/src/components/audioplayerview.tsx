import { Zap, Disc } from "lucide-react"
import { AudioControls } from "./audioplayer/audiocontrols"
import { AudioPlayerHeader } from "./audioplayer/audioplayerheader"
import { AudioStatCard } from "./audioplayer/audiostatcard"
import { AudioBufferCard } from "./audioplayer/audiobuffercard"
import {FileExplorer} from "@/components/fileexplorer.tsx";
import {useWS} from "@/WebSocketContext.tsx";
import {useEffect, useState} from "react";
import {type FilePlayerStatus, type MixerInputType} from "@/components/audio/MixerInput.tsx";


interface AudioPlayerViewProps {
  idx:number;
  //status: any;
  sendSignal: (msg: any) => void;
}

export function AudioPlayerView({ idx, sendSignal }: AudioPlayerViewProps) {


  const {lastJsonMessage} = useWS();
  const [status,setStatus] = useState<any>(null);
  const audio = status?.audio;
  const input:MixerInputType = audio?.mixer?.inputs[idx];



  const fileplayer:FilePlayerStatus = audio?.mixer?.inputs[idx].fileplayer ? audio?.mixer?.inputs[idx].fileplayer:null;
  const kbps = fileplayer?.bitrate ? Math.round(fileplayer?.bitrate / 1000) : 0;


  useEffect(() => {
    if (!lastJsonMessage) return;
    if (lastJsonMessage.msgid == 1) {
      setStatus(lastJsonMessage);
    }
  }, [lastJsonMessage]);

  console.log(input);


  return (
    <div className="p-6 max-w-7xl mx-auto animate-in fade-in duration-500">

      <div className="grid grid-cols-12 gap-1">
        <div className="col-span-12 lg:col-span-7 flex flex-col gap-1">

          <div className="bg-[#0f1115] text-white rounded-2xl p-6 shadow-2xl border-b-4 border-purple-600 relative overflow-hidden">
            <div className="absolute top-0 right-0 w-64 h-64 bg-purple-500/10 rounded-full blur-[80px] -mr-32 -mt-32 pointer-events-none" />



            <AudioPlayerHeader
                title={fileplayer?.title}
                filename={fileplayer?.filename}
                codec={fileplayer?.codecName}
            />

            <AudioControls
                inputidx={idx}
                input={input}
              repeatMode={0}
              isShuffle={false}
              sendSignal={sendSignal}
            />
          </div>

          <div className="grid grid-cols-4 gap-3">
            <AudioStatCard 
              icon={Zap} 
              iconColor="text-amber-400" 
              label="Bitrate" 
              value={kbps} 
              unit="kbps" 
            />
            <AudioStatCard 
              icon={Disc} 
              iconColor="text-cyan-400" 
              label="Sample" 
              value={fileplayer?.sampleRate ? fileplayer.sampleRate / 1000 : 0}
              unit="kHz" 
            />
            <AudioBufferCard 
              label="Decoder Buffer" 
              fill={input?.buffer_used}
              max={input?.buffer_size}
			  sampleRate={audio?.input?.sampleRate || 44100}
              className="col-span-2" 
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
          idx={idx}
          type={input?.type}

      />
    </div>

  )
}