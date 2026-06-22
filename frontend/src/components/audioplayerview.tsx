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
    sendSignal({msgid: 5005, type: 0, idx: idx});
  },[]);

  useEffect(() => {
    if (!lastJsonMessage) return;
    if (lastJsonMessage.msgid == 1) {
      setStatus(lastJsonMessage);
    }
  }, [lastJsonMessage]);

  console.log(input);


  return (
      <div className="p-6 max-w-7xl mx-auto animate-in fade-in duration-500">

        <div className="grid grid-cols-12 gap-6">
          <div className="col-span-12 lg:col-span-8 flex flex-col gap-6">

            <div className="bg-[#0f1115] text-white rounded-2xl p-6 shadow-2xl border-b-4 border-purple-600 relative overflow-hidden">
              <div className="absolute top-0 right-0 w-64 h-64 bg-purple-500/10 rounded-full blur-[80px] -mr-32 -mt-32 pointer-events-none" />

              <AudioPlayerHeader
                  title={fileplayer?.title}
                  filename={fileplayer?.filename}
                  codec={fileplayer?.codecName}
              />

              <div className="mt-8 shadow-sm border border-white/5 bg-white/[0.02] rounded-xl overflow-hidden">
                <AudioControls
                    inputidx={idx}
                    input={input}
                    repeatMode={0}
                    isShuffle={false}
                    sendSignal={sendSignal}
                />
              </div>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
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

          <div className="col-span-12 lg:col-span-4">
            <div className="bg-[#0f1115]/50 h-full rounded-2xl border border-white/5 overflow-hidden flex flex-col">
              {/* Placeholder for Playlist Manager or other content */}
              <div className="p-4 text-xs font-mono text-slate-500 uppercase tracking-widest text-center border-b border-white/5">
                Queue Manager
              </div>
              <div className="flex-1 p-4 flex items-center justify-center">
                <span className="text-[#333] text-xs italic">Waiting for queue data...</span>
              </div>
            </div>
          </div>
        </div>

        <div className="mt-8">
          <FileExplorer
              idx={idx}
              type={input?.type}
          />
        </div>
      </div>
  )
}