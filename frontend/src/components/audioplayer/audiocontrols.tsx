import { Button } from "@/components/ui/button"
import { Slider } from "@/components/ui/slider"
import {
  Play, Pause, Square, Repeat, Repeat1, Shuffle, ArrowRightToLine, ArrowLeftToLine
} from "lucide-react"
import { cn } from "@/lib/utils"
import {formatTime} from "@/lib/dejavis_utils.ts"
import {useWS} from "@/WebSocketContext.tsx";

interface AudioControlsProps {
  input:any;
  inputidx:number;
  repeatMode: number; 
  isShuffle: boolean;
  sendSignal: (command: any) => void;
}

export function AudioControls({ 
  input,inputidx, repeatMode, isShuffle, sendSignal
}: AudioControlsProps) {

  const isPlaying = input?.fileplayer?.isPlaying || false;
  const position = input?.fileplayer?.position || 0;
  const duration = input?.fileplayer?.duration || 0;
  const {sendMessage  } = useWS();


  // Funzione per gestire il cambio valore dello slider
  const handleSeek = (val: number[]) => {
    console.log({ msgid: 5008, idx: inputidx, sec: val[0] });
    sendMessage({ msgid: 5008, idx: inputidx, sec: val[0] });
  };

  const handleStop = () => {

    sendMessage({ msgid: 5005, idx: inputidx });
  };

  return (
      <div className="flex flex-col up-tracking-wider">

        {/* 1. Timeline & Progress - Rendering as a distinct section */}
        <div className="px-6 py-4 bg-black/40 border-b border-white/5 w-full">
          <div className="flex items-center gap-4 mb-2">
              <span className={cn("font-mono text-[10px] font-medium", isPlaying ? "text-purple-400" : "text-slate-500")}>
                {formatTime(position)}
              </span>
            <Slider
                value={[position]}
                max={duration > 0 ? duration : 100}
                step={0.1}
                className="flex-1 h-2 cursor-pointer selection:bg-purple-500"
                onValueChange={handleSeek}
            />
            <span className="font-mono text-[10px] font-medium text-slate-500">
                {formatTime(duration)}
              </span>
          </div>
        </div>

        {/* 2. Main Controls - Integrated surface */}
        <div className="flex items-center justify-center gap-6 p-8 relative">

          {/* Integrating "Engine Status" more subtly */}
          <div className="absolute left-6 flex items-center gap-2 opacity-40 hover:opacity-100 transition-opacity uppercase">
            <div className={cn(
                "h-1.5 w-1.5 rounded-full",
                isPlaying ? "bg-green-500 animate-pulse ring-4 ring-green-500/20" : "bg-slate-700"
            )} />
            <span className="text-[8px] font-black tracking-widest text-slate-400">
                {isPlaying ? 'Playing' : 'Stopped'}
              </span>
          </div>

          {/* Previous/Next block */}
          <div className="flex items-center gap-2 scale-90">
            <Button
                variant="ghost"
                size="icon"
                className="h-8 w-8 rounded-full text-slate-400 hover:text-purple-500"
                onClick={() => sendSignal({ msgid: 5007, idx: inputidx })}
            >
              <ArrowLeftToLine className="h-4 w-4" />
            </Button>
          </div>

          {/* Center Play/Pause Group */}
          <div className="flex items-center gap-4">
            <Button
                variant="ghost"
                className="h-10 w-10 rounded-full border border-white/5 text-slate-500 hover:text-red-500 hover:bg-red-500/10 transition-colors"
                onClick={() => handleStop()}
            >
              <Square className="h-4 w-4 fill-current" />
            </Button>

            <Button
                className="h-14 w-14 rounded-full bg-purple-600 text-white hover:bg-purple-500 shadow-xl hover:shadow-purple-500/20 transition-all active:scale-95"
                onClick={() => sendSignal({ command: isPlaying ? "pausefile" : "playfile" })}
            >
              {isPlaying ? <Pause className="h-7 w-7 fill-current" /> : <Play className="h-7 w-7 fill-current ml-1" />}
            </Button>

            <div className="flex items-center justify-center w-10" /> {/* Balance internal spacing */}
          </div>

          {/* Next/Shuffle/Repeat block */}
          <div className="flex items-center gap-2 scale-90">
            <Button
                variant="ghost"
                size="icon"
                className="h-8 w-8 rounded-full text-slate-400 hover:text-purple-500"
                onClick={() => sendSignal({ msgid: 5006, idx: inputidx })}
            >
              <ArrowRightToLine className="h-4 w-4" />
            </Button>
          </div>
        </div>

        {/* 3. Tertiary Advanced Controls row with borders */}
        <div className="flex items-center justify-center gap-4 p-3 border-t border-white/10 bg-white/[0.03] text-slate-500">
          <Button
              variant="ghost"
              className={cn("h-7 px-2 text-[10px] rounded-full border border-white/10 hover:border-purple-500",
                  repeatMode > 0 ? "text-purple-400 border-purple-500/50" : ""
              )}
              onClick={() => sendSignal({ command: "set_repeat", value: (repeatMode + 1) % 3 })}
          >
            {repeatMode === 1 ? <Repeat1 className="h-3 w-3 mr-1" /> : <Repeat className="h-3 w-3 mr-1" />}
            Repeat {repeatMode === 1 ? '1' : 'All'}
          </Button>

          <Button
              variant="ghost"
              className={cn("h-7 px-2 text-[10px] rounded-full border border-white/10 hover:border-purple-500",
                  isShuffle ? "text-purple-400 border-purple-500/50" : ""
              )}
              onClick={() => sendSignal({ command: "set_shuffle", value: !isShuffle })}
          >
            <Shuffle className="h-3 w-3 mr-1" />
            Shuffle
          </Button>

          <div className="pl-4 border-l border-white/10 flex items-center gap-1.5 opacity-30 group hover:opacity-100 transition-opacity">
            <span className="text-[8px] font-mono uppercase tracking-tighter">System Update Loaded</span>
            <span className="text-[8px] font-mono uppercase tracking-tighter">v3.0.1-rc</span>
          </div>
        </div>
      </div>
  )
}