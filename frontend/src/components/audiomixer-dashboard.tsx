"use client";

import { Activity, AlertCircle, Radio } from 'lucide-react';
import { useWS } from '../WebSocketContext';
import { MixerOutput , type MixerOutputStatus} from './audio/MixerOutput2';
import { MixerInput ,type MixerInputType} from './audio/MixerInput';
import  {AudioMonitor} from "@/components/audio/AudioMonitor.tsx";
import {useEffect, useState} from "react";
import MixerPreviewBackground from "@/components/video/videomixerpreview_borderless.tsx";
import {useGlobalWebRTC} from "@/components/rtc/WebRTCProvider.tsx";
import {useWorkletAnalysis} from "@/hooks/workletaudioanalysis.ts";
import {FFTCanvas} from "@/components/audio/FFTCanvas.tsx";


interface MixerData {
    inputs: MixerInputType[];
    master_output: MixerOutputStatus;
    aux_output:MixerOutputStatus;
}

export function AudioMixerDashboard() {

    const {lastJsonMessage, sendMessage, readyState} = useWS();
    const { stream } = useGlobalWebRTC();
    const fftRef = useWorkletAnalysis(stream);

    const [mixer, setMixer] = useState<MixerData | null>(null);

    const handleInputUpdate = (msgid: number, inputidx: number, value: any, param?: string) => {
        sendMessage({
            msgid,
            inputidx,
            value,
            ...(param && {param})
        });
    };

    useEffect(() => {
        if (!lastJsonMessage) return;
        if (lastJsonMessage.msgid == 1) {
            setMixer(lastJsonMessage?.audio?.mixer);
        }
    }, [lastJsonMessage]);


    const handleOutputUpdate = (msgid: number, outputidx: number, value: number|boolean, param?: string) => {
        sendMessage({
            msgid,
            outputidx,
            value,
            ...(param && {param})
        });
    };


    if (readyState !== 1) {
        return (
            <div
                className="flex flex-col items-center justify-center h-96 gap-4 border-2 border-dashed border-red-500/20 rounded-xl bg-red-500/5">
                <Radio className="h-10 w-10 text-red-500 animate-pulse"/>
                <div className="text-center">
                    <h3 className="text-lg font-bold text-red-500 uppercase tracking-tighter">Connection Lost</h3>
                    <p className="text-[10px] font-mono opacity-60">Reconnecting to ws://host:8848/ws...</p>
                </div>
            </div>
        );
    }


    if (!mixer) {
        return (
            <div className="flex flex-col items-center justify-center h-96 gap-6 p-8">
                <div className="relative">
                    <Activity className="h-12 w-12 text-primary animate-spin opacity-20"/>
                    <AlertCircle className="h-6 w-6 text-yellow-500 absolute -top-1 -right-1 animate-bounce"/>
                </div>
                <div className="text-center space-y-2">
                    <h3 className="text-sm font-black uppercase tracking-widest text-white">Engine Link Active</h3>
                    <p className="text-[10px] font-mono text-muted-foreground max-w-xs">
                        Connected to WebSocket, but the JSON state structure is missing or invalid.
                    </p>
                </div>


            </div>
        );
    }


    return (
        <div className="flex flex-col gap-8 p-6 max-w-[1650px] mx-auto animate-in fade-in zoom-in-95 duration-700">

            {/* Sezione Superiore: Audio Mixer + Video Preview */}
            <div className="flex gap-6 items-start">

                {/* Contenitore Mixer Audio */}
                <div className="flex gap-0 overflow-x-auto pb-4 pt-2
                scrollbar-thin scrollbar-thumb-primary/30 scrollbar-track-transparent
                hover:scrollbar-thumb-primary/50 transition-all duration-300">
                    <AudioMonitor/>
                    <MixerOutput
                        output={mixer.master_output}
                        key={`output-ch-0`}
                        idx={0}
                        onUpdateParam={handleOutputUpdate}
                    />
                    <MixerOutput
                        output={mixer.aux_output}
                        key={`output-ch-1`}
                        idx={1}
                        onUpdateParam={handleOutputUpdate}
                    />
                </div>

                {/* Video Preview + Audio Meter Stack */}
                <div className="flex-1 flex flex-col gap-2 p-3 bg-slate-900/50 rounded-2xl border border-slate-800 shadow-2xl">

                    {/* PARTE SUPERIORE: Video */}
                    <div className="relative w-full aspect-video bg-black rounded-lg overflow-hidden border border-slate-700">
                        <MixerPreviewBackground allowControls={true} className="w-full h-full" />
                    </div>

                    {/* PARTE INFERIORE: FFT Visualizer */}
                    <div className="w-full bg-black/40 rounded-lg p-2 border border-slate-800/50">
                        <div className="flex items-center justify-between mb-2 px-1">
                            <div className="flex gap-1">
                                <div className={`w-1.5 h-1.5 rounded-full ${stream ? 'bg-emerald-500' : 'bg-slate-700'}`} />
                            </div>
                        </div>

                        {/* Il tuo componente FFT */}
                        <FFTCanvas fftRef={fftRef} />
                    </div>

                </div>
            </div>

            {/* Sezione Inferiore: Input Fader */}
            <div className="flex gap-0 overflow-x-auto pb-8 pt-2
            scrollbar-thin scrollbar-thumb-primary/30 scrollbar-track-transparent
            hover:scrollbar-thumb-primary/50 transition-all duration-300">
                {mixer.inputs?.map((input: MixerInputType, idx: number) => (
                    <MixerInput
                        key={`input-ch-${idx}`}
                        idx={idx}
                        input={input}
                        onUpdateParam={handleInputUpdate}
                    />
                ))}
            </div>
        </div>
    );
}