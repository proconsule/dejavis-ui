import MixerPreview from "@/components/video/videomixerpreview.tsx";
import { useGlobalWebRTC } from "@/components/rtc/WebRTCProvider.tsx";
import { Play, Square, Loader2, Signal } from 'lucide-react';
import {useWorkletAnalysis} from "@/hooks/workletaudioanalysis.ts";
import {FFTCanvas} from "@/components/audio/FFTCanvas.tsx";



export function SystemOverview() {
  const { stream, state, start, stop } = useGlobalWebRTC();
  const isConnected = state === 'connected';
  const isConnecting = state === 'connecting';
    const fftRef = useWorkletAnalysis(stream);

  return (
      <div className="w-full max-w-[800px] mx-auto flex flex-col gap-3 animate-in fade-in duration-500">

        {/* 1. MONITOR VIDEO */}
        <div className="w-full relative bg-black rounded-xl border border-slate-800 overflow-hidden shadow-2xl">
          <MixerPreview
              className="w-full aspect-video"
          />

          {/* Overlay informativo se disconnesso */}
          {!isConnected && !isConnecting && (
              <div className="absolute inset-0 flex items-center justify-center bg-slate-900/60 backdrop-blur-sm">
                <span className="text-slate-400 text-xs font-mono uppercase tracking-widest">Signal Offline</span>
              </div>
          )}
        </div>

        {/* 2. TOOLBAR CONTROLLI */}
        <div className="flex items-center justify-between px-4 py-2 bg-slate-900/80 rounded-xl border border-slate-800 shadow-sm">
          <div className="flex items-center gap-3">
              <button
                  onClick={isConnected || isConnecting ? stop : start}
                  // Rimosso il disabled per permettere il clic durante isConnecting
                  className={`flex items-center gap-2 px-4 py-1.5 rounded-lg font-bold text-[11px] uppercase tracking-wider transition-all active:scale-95 ${
                      isConnected || isConnecting
                          ? 'bg-rose-500/10 text-rose-500 border border-rose-500/20 hover:bg-rose-500/20'
                          : 'bg-emerald-500/10 text-emerald-500 border border-emerald-500/20 hover:bg-emerald-500/20'
                  }`}
              >
                  {isConnecting ? (
                      <Loader2 size={14} className="animate-spin" />
                  ) : isConnected ? (
                      <Square size={14} fill="currentColor" />
                  ) : (
                      <Play size={14} fill="currentColor" />
                  )}

                  {isConnecting
                      ? 'Stop Connecting'
                      : isConnected
                          ? 'Stop Session'
                          : 'Start Session'
                  }
              </button>

            <div className="h-4 w-[1px] bg-slate-700" />

            {/* Status Badge */}
            <div className="flex items-center gap-2">
              <Signal size={12} className={isConnected ? "text-emerald-500" : "text-slate-600"} />
              <span className={`text-[10px] font-mono ${isConnected ? "text-emerald-500" : "text-slate-500"}`}>
              {state.toUpperCase()}
            </span>
            </div>
          </div>
        </div>

        {/* 3. MONITOR AUDIO (FFT) */}
        <div className="w-full bg-slate-900/40 rounded-xl border border-slate-800 p-2">
          <div className="flex items-center justify-between mb-2 px-1">
              <div className="flex items-center justify-between mb-2 px-1">
                  <div className="flex gap-1">
                      <div className={`w-1.5 h-1.5 rounded-full ${stream ? 'bg-emerald-500' : 'bg-slate-700'}`} />
                  </div>
              </div>
              <span className="text-[9px] font-mono text-slate-600">20Hz - 20kHz</span>
          </div>


            {/*<WorkletFFTVisualizer stream={stream} /> */}

            <div className="h-40">
            <FFTCanvas fftRef={fftRef} />
            </div>
        </div>

      </div>
  );
}