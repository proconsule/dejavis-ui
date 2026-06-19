"use client";

import {memo, useEffect, useMemo, useState} from 'react';
import {AudioBufferVertical} from "@/components/audio/audiobuffer_simple.tsx";
import { useWS } from "../../WebSocketContext.tsx"
import {Marquee} from "@/components/ui/marquee.tsx";
import {MixerSlider, PanSlider} from "@/components/audio/MixerSlider.tsx";
import throttle from "lodash.throttle";
import {InputSelectorDialog} from "@/components/audio/InputSelector.tsx";

import {formatTime} from "@/lib/dejavis_utils.ts"

import {
    AlertDialog,
    AlertDialogAction,
    AlertDialogCancel,
    AlertDialogContent,
    AlertDialogDescription,
    AlertDialogFooter,
    AlertDialogHeader,
    AlertDialogTitle,
    AlertDialogTrigger,
} from "@/components/ui/alert-dialog"

import {VuMeterCanvas} from "@/components/audio/VuMeterCanvas.tsx";


export type MixerInputType = {
    name: string;
    mixerout_idx: number;
    samplerate: number;
    type: number;
    active: boolean;
    live: boolean;
    muted: boolean;
    solo: boolean;
    pan: number;
    volume: number;
    buffer_size: number;
    buffer_used: number;
    videomixer_idx: number;
    gain_preset?: number;

    fileplayer?: FilePlayerStatus;
}

export type FilePlayerStatus = {
    isPlaying: boolean;
    position: number;
    duration: number;
    title: string;
    filename: string;
    bitrate: number;
    codecName: string;
    sampleRate: number;
    channels: number;
    isResampling: boolean;
}

interface MixerInputProps {
    input: MixerInputType;
    idx: number;
    onUpdateParam: (msgid: number, inputIdx: number, value: any, param?: string) => void;
}


const FileplayerStatus = ({ inputIdx }: { inputIdx: number }) => {
    const { lastJsonMessage } = useWS();

    const input = lastJsonMessage?.audio?.mixer?.inputs[inputIdx];
    const input_video = lastJsonMessage?.video?.videomixer?.inputs[input.videomixer_idx];

    const type = lastJsonMessage?.audio?.mixer?.inputs[inputIdx].type;
    const audio_fileplayer_duration = input?.fileplayer?.duration || 0.0;
    const audio_fileplayer_position = input?.fileplayer?.position || 0.0;
    const audio_fileplayer_timestring = `${formatTime(audio_fileplayer_position)} - ${formatTime(audio_fileplayer_duration)}`;
    const video_fileplayer_duration = input_video?.file_decoder?.duration || 0.0;
    const video_fileplayer_position = input_video?.file_decoder?.position || 0.0;
    const ndi_source_string = input_video?.ndi?.status?.source || "";
    //const ndi_source_running = input_video?.ndi?.status?.running || false;
    const video_fileplayer_timestring = `${formatTime(video_fileplayer_position)} - ${formatTime(video_fileplayer_duration)}`;



    return (
        <div>
            {type == 0 ?
                audio_fileplayer_timestring :
                type == 3 ?
                    video_fileplayer_timestring :
                    type == 5 ?
                        <Marquee>NDI: {ndi_source_string}</Marquee> :
                        "LIVE"
            }
        </div>

    );
};

const InputBufferStatus = ({ inputIdx }: { inputIdx: number }) => {
    const { lastJsonMessage } = useWS();


    useEffect(() => {
        if(lastJsonMessage.msgid == 1) {
            setInput(lastJsonMessage?.audio?.mixer?.inputs[inputIdx]);
        }
    }, [lastJsonMessage]);

    const [input, setInput] = useState<any|null>(null);

    //const input = lastJsonMessage?.audio?.mixer?.inputs[inputIdx];
    const fill:number = input?.buffer_used || 0;
    const max:number = input?.buffer_size || 1024;



    return (

            <AudioBufferVertical
                fill={fill}
                max={max}
                className="opacity-80"
            />

    );
};

export const MixerInput = memo(function MixerInput({ input, idx, onUpdateParam }: MixerInputProps) {

    const getRoutingColor = () => {
        if (input.mixerout_idx === 0) return "border-t-green-500 shadow-[0_-4px_10px_rgba(34,197,94,0.2)]";
        if (input.mixerout_idx === 1) return "border-t-red-500 shadow-[0_-4px_10px_rgba(239,68,68,0.2)]";
        return "border-t-zinc-800";
    };

    const getDisplayName = (path?: string) => {
        if (!path) return "";
        return path.split(/[\\/]/).pop() || path;
    };

    const { audioRef } = useWS();


    //const [levels, setLevels] = useState<Float32Array>();
/*
    useEffect(() => {
        // Funzione per aggiornare i livelli

        const updateAudio = () => {
            if (audioRef.current) {
                const currentLevels = audioRef.current.inputs[idx];
                //setLevels(currentLevels);
            }
            // Se è un'animazione continua, usa requestAnimationFrame
            requestAnimationFrame(updateAudio);
        };

        const animationId = requestAnimationFrame(updateAudio);

        // Pulizia quando il componente viene smontato
        return () => cancelAnimationFrame(animationId);
    }, audioRef.current.inputs[idx]);
*/
    const throttledUpdate = useMemo(
        () =>
            throttle((msgid: number, inputIdx: number, value: any, param?: string) => {
                onUpdateParam(msgid, inputIdx, value, param);
            }, 50, { leading: true, trailing: true }),
        [onUpdateParam]
    );

    const [localVolume, setLocalVolume] = useState(Math.cbrt(input.volume));
    const [localPan, setLocalPan] = useState(input.pan * 100);
    const [localMuted,setlocalMuted] = useState(input.muted);
    const [localSolo,setlocalSolo] = useState(input.solo);
    const [localGainPreset, setLocalGainPreset] = useState(input.gain_preset ?? 0);

    useEffect(() => {
        const linearValue = Math.cbrt(input.volume);
        setLocalVolume(linearValue);
    }, [input.volume]);

    useEffect(() => {
        setLocalGainPreset(input.gain_preset ?? 0);
    }, [input.gain_preset]);

    useEffect(() => {
        setLocalPan(input.pan * 100);
    }, [input.pan]);

    useEffect(() => {
        setlocalMuted(input.muted);
    }, [input.muted]);

    useEffect(() => {
        setlocalSolo(input.solo);
    }, [input.solo]);

    useEffect(() => {
        return () => {
            throttledUpdate.cancel();
        };
    }, [throttledUpdate]);


    const CloseMixerInput = (idx:number,type:number) => {
        if(type == 0 || type == 3 || type == 5){
            onUpdateParam(5002, idx, type);
        }else if(type == 2){
            onUpdateParam(6002, idx, true);
        }

    };

    const GAIN_PRESETS = [
        { value: 0, label: "0dB"  },
        { value: 1, label: "+6"   },
        { value: 2, label: "+12"  },
        { value: 3, label: "+20"  },
    ] as const;


    return (
        <div className={`flex flex-col w-28 shrink-0 bg-zinc-900/90 border-x border-b border-white/5 border-t-4 ${getRoutingColor()} transition-all`}>

            <div className="p-2 bg-black/40 flex flex-col items-center gap-1 border-b border-white/5">
                <span className="text-[10px] font-mono text-zinc-500">IN CH {idx.toString().padStart(2, '0')}</span>
                <div className="w-full text-[9px] font-black uppercase tracking-tighter truncate text-center text-zinc-200 px-1">
                    {input.active && input.fileplayer ?
                        <Marquee speed="fast">{getDisplayName(input.fileplayer.filename)}</Marquee>
                        :
                    <Marquee>{input.name}</Marquee>
                    }
                </div>

                <div className={`h-1.5 w-1.5 rounded-full ${input.active ? "bg-emerald-500 shadow-[0_0_5px_#10b981]" : "bg-zinc-700"}`} />
            </div>

            <div className="p-2 flex flex-col gap-1 bg-black/20">
                <div className="grid grid-cols-2 gap-1">
                    <button
                        onClick={() => onUpdateParam(3002, idx, 0)}
                        className={`text-[8px] font-bold py-1 border ${input.mixerout_idx === 0 ? 'bg-green-600 border-green-400 text-white' : 'bg-zinc-800 border-white/5 text-zinc-500'}`}
                    >MST</button>
                    <button
                        onClick={() => onUpdateParam(3002, idx, 1)}
                        className={`text-[8px] font-bold py-1 border ${input.mixerout_idx === 1 ? 'bg-red-600 border-red-400 text-white' : 'bg-zinc-800 border-white/5 text-zinc-500'}`}
                    >AUX</button>
                </div>
                <button
                    onClick={() => onUpdateParam(3002, idx, -1)}
                    className={`text-[8px] font-bold py-1 border ${input.mixerout_idx === -1 ? 'bg-zinc-600 border-zinc-400 text-white' : 'bg-zinc-800 border-white/5 text-zinc-500'}`}
                >OFF</button>
            </div>

            <div className="p-2 bg-black/40 flex flex-col items-center gap-1 border-b border-white/5">

                {!input.active ? (
                <InputSelectorDialog
                    idx={idx}
                    currentInput={input.name}
                />
                ):<AlertDialog>
                    <AlertDialogTrigger asChild>
                        <button
                            className={`
                                ${input.active ? "bg-emerald-500 shadow-[0_0_15px_rgba(16,185,129,0.6)] animate-pulse" : "bg-emerald-500/10"}
                                group flex items-center justify-center w-full px-3 py-1.5 rounded 
                                border border-emerald-500/30 hover:bg-red-500/20 
                                hover:border-red-500/50 hover:text-red-400 transition-all active:scale-95
                                text-xs font-medium whitespace-nowrap
                              `}
                            title="Disconnect Source"
                        >
                            <span className="truncate"><FileplayerStatus inputIdx={idx}></FileplayerStatus></span>
                        </button>
                    </AlertDialogTrigger>

                    <AlertDialogContent>
                        <AlertDialogHeader>
                            <AlertDialogTitle>Are you sure?</AlertDialogTitle>
                            <AlertDialogDescription>
                                Disconnect and close source {input.name} @ {input.type}
                            </AlertDialogDescription>
                        </AlertDialogHeader>
                        <AlertDialogFooter>
                            <AlertDialogCancel>Annulla</AlertDialogCancel>
                            <AlertDialogAction
                                onClick={() => CloseMixerInput(idx, input.type)}
                                className="bg-red-500 hover:bg-red-600"
                            >
                                Conferma
                            </AlertDialogAction>
                        </AlertDialogFooter>
                    </AlertDialogContent>
                </AlertDialog>
                    }
            </div>

            {/* 3. CONTROLLI PAN E FX */}
            <div className="p-2 space-y-3 flex-1 border-b border-white/5">

                <div className="space-y-1 px-2">
                    <div className="flex justify-between text-[8px] font-mono text-zinc-500 uppercase tracking-widest">
                        <span>L</span>
                        <span className="text-[7px] text-zinc-600">Center</span>
                        <span>R</span>
                    </div>

                    <PanSlider
                        value={[localPan]}
                        min={-100}
                        max={100}
                        step={1}
                        onValueChange={(v) => {
                            setLocalPan(v[0]); // Update UI immediato
                            throttledUpdate(3001, idx, v[0] / 100); // Update Server throttled
                        }}
                        onReset={() => {
                            setLocalPan(0);
                            onUpdateParam(3001, idx, 0);
                        }}
                        className="cursor-ew-resize"
                    />

                    {/* Feedback testuale opzionale sotto */}
                    <div className="text-center text-[8px] font-mono text-zinc-400">
                        {localPan === 0 ? "C" : localPan < 0 ? `L${Math.abs(Math.round(localPan))}` : `R${Math.round(localPan)}`}
                    </div>
                </div>

                <div className="px-2 pt-2">
                    <div className="text-[7px] font-mono text-zinc-500 uppercase tracking-widest text-center mb-1">
                        Gain
                    </div>
                    <div className="grid grid-cols-4 gap-0.5">
                        {GAIN_PRESETS.map((p) => (
                            <button
                                key={p.value}
                                onClick={() => {
                                    setLocalGainPreset(p.value);
                                    onUpdateParam(3003, idx, p.value);
                                }}
                                className={`text-[8px] font-bold py-1 border transition-all active:scale-95 ${
                                    localGainPreset === p.value
                                        ? 'bg-violet-600 border-violet-400 text-white shadow-[0_0_8px_rgba(139,92,246,0.6)]'
                                        : 'bg-zinc-800 border-white/5 text-zinc-500 hover:bg-zinc-700'
                                }`}
                                title={`Input gain ${p.label}`}
                            >
                                {p.label}
                            </button>
                        ))}
                    </div>
                </div>

                {/* 3. CONTROLLI SOLO E MUTE */}
                <div className="flex gap-1 justify-center pt-2 px-2">
                    <button
                        onClick={() => onUpdateParam(3005, idx, !localMuted)}
                        className={`flex-1 py-1 rounded-sm border text-[9px] font-bold transition-all active:scale-95 ${
                            localMuted
                                ? 'bg-amber-500 border-amber-300 text-black shadow-[0_0_15px_rgba(245,158,11,0.8)] animate-pulse'
                                : 'bg-zinc-800 border-white/10 text-zinc-500 hover:bg-zinc-700'
                        }`}
                    >
                        MUTE
                    </button>
                    <button
                        onClick={() => onUpdateParam(3006, idx, !localSolo)}
                        className={`flex-1 py-1 rounded-sm border text-[9px] font-bold transition-all active:scale-95 ${
                            localSolo
                                ? 'bg-blue-500 border-blue-300 text-white shadow-[0_0_15px_rgba(59,130,246,0.8)] animate-pulse'
                                : 'bg-zinc-800 border-white/10 text-zinc-500 hover:bg-zinc-700'
                        }`}
                    >
                        SOLO
                    </button>
                </div>
            </div>

            {/* 4. SEZIONE FADER E VU-METER (Verticale Alta) */}
            <div className="p-2 h-72 bg-black/40 flex items-stretch gap-4 justify-center">
                {/* Buffer & VU Meter affiancati stretti */}
                <div className="w-1"><InputBufferStatus inputIdx={idx} /></div>
                {/* <div className="w-3"><VUMeter inputIdx={idx} active={input.active} /></div> */}
                <div className="w-10"> {/* Aggiungi un'altezza esplicita qui! */}
                    <VuMeterCanvas levels={audioRef.current.inputs[idx]} />
                </div>

                {/* Fader Volume */}
                <div className="flex flex-col items-center gap-2 relative">
                    <MixerSlider
                        value={[localVolume]}
                        mixerout_idx={input.mixerout_idx}
                        orientation="vertical"
                        onValueChange={(v) => {

                            const cubicGain = Math.pow(v[0], 3);
                            const cleanJsonValue = Math.round(cubicGain * 10000) / 10000;
                            setLocalVolume(v[0]);
                            throttledUpdate(3000, idx, cleanJsonValue);
                        }}
                        className="h-full py-2"
                    />
                </div>
            </div>
        </div>
    );
}, (prev, next) => {

    return (
        prev.input.muted === next.input.muted &&
        prev.input.solo === next.input.solo &&
        prev.input.volume === next.input.volume &&
        prev.input.pan === next.input.pan &&
        prev.input.active === next.input.active &&
        prev.input.mixerout_idx === next.input.mixerout_idx &&
        prev.input.gain_preset === next.input.gain_preset
    );
});