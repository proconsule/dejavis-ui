import { useEffect, useRef, useState } from 'react';
import {useGlobalWebRTC} from "@/components/rtc/WebRTCProvider.tsx";
import { Play, Square, Volume2, VolumeX, Maximize2, AlertCircle, Loader2, Wifi } from 'lucide-react';

type Props = {
    /** Avvia automaticamente al mount (default: false, l'utente clicca play) */
    /** Mostra controlli audio (default: true) */
    showAudioControls?: boolean;
    /** Aspect ratio target del player (default: 16/9) */
    aspectRatio?: number;
    /** Classi extra per il wrapper */
    className?: string;
};

export function MixerPreview({
                                 showAudioControls = true,
                                 aspectRatio = 16 / 9,
                                 className = '',
                             }: Props) {
    const videoRef = useRef<HTMLVideoElement | null>(null);
    const containerRef = useRef<HTMLDivElement | null>(null);
    const { stream, state, error, start, stop } = useGlobalWebRTC();
    const [muted, setMuted] = useState(false);

    // Aggancia lo stream all'<video> quando cambia
    useEffect(() => {
        if (!videoRef.current) return;
        if (videoRef.current.srcObject !== stream) {
            videoRef.current.srcObject = stream;
        }
    }, [stream]);

    const toggleFullscreen = () => {
        if (!containerRef.current) return;
        if (document.fullscreenElement) {
            document.exitFullscreen();
        } else {
            containerRef.current.requestFullscreen();
        }
    };

    // Lo stato del bottone principale dipende dal connection state
    const isRunning = state === 'connecting' || state === 'connected';

    return (
        <div className={`bg-zinc-900 border border-slate-700 rounded-xl overflow-hidden flex flex-col ${className}`}>
            {/* Header */}
            <div className="flex items-center justify-between px-4 py-2 border-b border-slate-700 bg-slate-800/50">
                <div className="flex items-center gap-2">
                    <h3 className="text-emerald-400 font-bold text-sm">RTC Video Monitor</h3>
                    <StateBadge state={state} />
                </div>
                <div className="flex items-center gap-1">
                    {showAudioControls && stream && (
                        <button
                            onClick={() => setMuted(m => !m)}
                            className="p-1.5 rounded hover:bg-slate-700 text-slate-400 hover:text-emerald-400"
                            title={muted ? 'Unmute' : 'Mute'}
                        >
                            {muted ? <VolumeX size={14} /> : <Volume2 size={14} />}
                        </button>
                    )}
                    <button
                        onClick={toggleFullscreen}
                        className="p-1.5 rounded hover:bg-slate-700 text-slate-400 hover:text-emerald-400"
                        title="Fullscreen"
                    >
                        <Maximize2 size={14} />
                    </button>
                </div>
            </div>

            {/* Video area */}
            <div
                ref={containerRef}
                className="relative bg-black flex items-center justify-center"
                style={{ aspectRatio: `${aspectRatio}` }}
            >
                <video
                    ref={videoRef}
                    autoPlay
                    playsInline
                    muted={muted}
                    className="w-full h-full object-contain"
                />

                {/* Overlay quando non c'è stream */}
                {!stream && (
                    <div className="absolute inset-0 flex flex-col items-center justify-center text-slate-400">
                        {state === 'idle' && (
                            <>
                                <Play size={40} className="text-slate-600 mb-2" />
                                <p className="text-xs uppercase font-bold tracking-wider">Press start</p>
                            </>
                        )}
                        {state === 'connecting' && (
                            <>
                                <Loader2 size={40} className="text-emerald-400 animate-spin mb-2" />
                                <p className="text-xs uppercase font-bold tracking-wider text-emerald-400">Connecting...</p>
                            </>
                        )}
                        {state === 'disconnected' && (
                            <>
                                <Wifi size={40} className="text-slate-600 mb-2" />
                                <p className="text-xs uppercase font-bold tracking-wider">Disconnected</p>
                            </>
                        )}
                        {state === 'error' && (
                            <>
                                <AlertCircle size={40} className="text-rose-400 mb-2" />
                                <p className="text-xs uppercase font-bold tracking-wider text-rose-400">Error</p>
                                {error && <p className="text-[10px] text-rose-300/70 mt-1 max-w-xs text-center">{error}</p>}
                            </>
                        )}
                    </div>
                )}

                {/* LIVE badge in alto a destra quando connesso */}
                {state === 'connected' && (
                    <div className="absolute top-2 right-2 flex items-center gap-1.5 px-2 py-1 bg-rose-500/90 rounded text-[10px] font-bold uppercase tracking-wide">
                        <span className="w-1.5 h-1.5 rounded-full bg-white animate-pulse" />
                        Live
                    </div>
                )}
            </div>

            {/* Footer con controlli */}
            <div className="flex items-center justify-between px-4 py-2 border-t border-slate-700 bg-slate-800/30">
                {!isRunning ? (
                    <button
                        onClick={start}
                        className="flex items-center gap-2 px-3 py-1.5 bg-emerald-500/20 hover:bg-emerald-500/30 border border-emerald-500 text-emerald-400 rounded text-xs font-bold uppercase tracking-wide"
                    >
                        <Play size={12} />
                        Start
                    </button>
                ) : (
                    <button
                        onClick={stop}
                        className="flex items-center gap-2 px-3 py-1.5 bg-rose-500/20 hover:bg-rose-500/30 border border-rose-500 text-rose-400 rounded text-xs font-bold uppercase tracking-wide"
                    >
                        <Square size={12} />
                        Stop
                    </button>
                )}

                {muted && stream && (
                    <span className="text-[10px] text-amber-400 italic flex items-center gap-1">
                        <VolumeX size={10} /> Muted (browser autoplay)
                    </span>
                )}
            </div>
        </div>
    );
}

// ===================================
//  STATE BADGE
// ===================================
function StateBadge({ state }: { state: string }) {
    const meta: Record<string, { label: string; cls: string }> = {
        idle:         { label: 'Idle',         cls: 'bg-slate-700 text-slate-400' },
        connecting:   { label: 'Connecting',   cls: 'bg-amber-500/20 text-amber-400' },
        connected:    { label: 'Connected',    cls: 'bg-emerald-500/20 text-emerald-400' },
        disconnected: { label: 'Disconnected', cls: 'bg-slate-700 text-slate-400' },
        error:        { label: 'Error',        cls: 'bg-rose-500/20 text-rose-400' },
    };
    const m = meta[state] ?? meta.idle;
    return (
        <span className={`text-[9px] px-1.5 py-0.5 rounded font-bold uppercase tracking-wide ${m.cls}`}>
            {m.label}
        </span>
    );
}

export default MixerPreview;