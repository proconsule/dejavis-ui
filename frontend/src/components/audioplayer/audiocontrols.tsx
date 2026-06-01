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
    <div className="flex flex-col gap-4 pt-6 pb-2 px-2 border-t border-white/10 bg-black/20 rounded-b-xl">
      
      {/* 1. Top Row: Progress Bar & Timestamps */}
      <div className="space-y-1.5">
        <Slider 
          // Il valore dello slider è ora la posizione attuale in secondi
          value={[position]} 
          // Il massimo è la durata totale del brano
          max={duration > 0 ? duration : 100} 
          step={0.1}
          className="cursor-pointer"
          // Invia il valore in secondi (double)
          onValueChange={handleSeek}
        />
        <div className="flex justify-between px-1 font-mono text-[10px] font-black tracking-tighter text-slate-500">
          <span className={isPlaying ? "text-purple-400" : ""}>{formatTime(position)}</span>
          <span>{formatTime(duration)}</span>
        </div>
      </div>

      {/* 2. Bottom Row: Centralized Controls */}
      <div className="flex items-center justify-between">
        
        {/* Lato Sinistro: Engine Status */}
        <div className="w-1/4">
          <div className="flex items-center gap-2">
            <div className={cn(
              "h-1.5 w-1.5 rounded-full",
              isPlaying ? "bg-green-500 animate-pulse shadow-[0_0_8px_#22c55e]" : "bg-slate-600"
            )} />
            <span className="text-[9px] font-black uppercase tracking-[0.2em] text-slate-500">
              {isPlaying ? 'Active' : 'Idle'}
            </span>
          </div>
        </div>

        {/* Centro: Main Control Hub */}
        <div className="flex items-center gap-4 bg-white/[0.03] p-1.5 rounded-2xl border border-white/[0.05]">



          <Button
              variant="ghost"
              size="icon"
              className={cn(
                  "h-6 w-6 rounded-full transition-all relative",
                  repeatMode > 0 ? "text-purple-400 bg-purple-500/10" : "text-slate-500 opacity-50"
              )}
              onClick={() => sendSignal({ msgid: 5007, idx: inputidx })}
          >
            <ArrowLeftToLine className="h-4 w-4" />
          </Button>

          <Button 
            variant="ghost"
            size="icon" 
            className="h-8 w-8 rounded-full hover:bg-red-500/10 text-slate-400 hover:text-red-500"
            onClick={() => handleStop()}
          >
            <Square className="h-4 w-4 fill-current" />
          </Button>

          <Button 
            size="icon" 
            className="h-8 w-8 rounded-full bg-purple-600 hover:bg-purple-500 text-white shadow-[0_0_20px_rgba(168,85,247,0.3)] transform hover:scale-105 transition-all"
            onClick={() => sendSignal({ command: isPlaying ? "pausefile" : "playfile" })}
          >
            {isPlaying ? <Pause className="h-6 w-6 fill-current" /> : <Play className="h-6 w-6 fill-current ml-1" />}
          </Button>

          <Button
              variant="ghost"
              size="icon"
              className={cn(
                  "h-6 w-6 rounded-full transition-all relative",
                  repeatMode > 0 ? "text-purple-400 bg-purple-500/10" : "text-slate-500 opacity-50"
              )}
              onClick={() => sendSignal({ msgid: 5006, idx: inputidx })}
          >
            <ArrowRightToLine className="h-4 w-4" />
          </Button>

          <Button
            variant="ghost"
            size="icon"
            className={cn(
              "h-6 w-6 rounded-full transition-all relative",
              repeatMode > 0 ? "text-purple-400 bg-purple-500/10" : "text-slate-500 opacity-50"
            )}
            onClick={() => sendSignal({ command: "set_repeat", value: (repeatMode + 1) % 3 })}
          >
            {repeatMode === 1 ? <Repeat1 className="h-4 w-4" /> : <Repeat className="h-4 w-4" />}
            {repeatMode > 0 && (
              <span className="absolute -bottom-1 right-0 text-[7px] font-bold bg-purple-500 text-white px-1 rounded-full border border-black">
                {repeatMode === 1 ? '1' : 'A'}
              </span>
            )}
          </Button>

          <Button
              variant="ghost"
              size="icon"
              className={cn(
                  "h-6 w-6 rounded-full transition-all",
                  isShuffle ? "text-purple-400 bg-purple-500/10" : "text-slate-500 opacity-50"
              )}
              onClick={() => sendSignal({ command: "set_shuffle", value: !isShuffle })}
          >
            <Shuffle className="h-4 w-4" />
          </Button>
        </div>

        {/* Lato Destro: Volume o Placeholder */}
        <div className="w-1/4 flex justify-end">
           <span className="text-[8px] font-mono text-slate-700 uppercase tracking-widest">DejaVu Logic v3</span>
        </div>

      </div>
    </div>
  )
}