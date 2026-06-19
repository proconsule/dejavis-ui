"use client";

import {memo, useEffect, useMemo, useState} from 'react';
import {AudioBufferVertical} from "@/components/audio/audiobuffer_simple.tsx";
import { useWS} from "../../WebSocketContext.tsx"
import {MixerSlider,PanSlider} from "@/components/audio/MixerSlider.tsx";
import throttle from "lodash.throttle";
import {OutputSelectorDialog} from "@/components/audio/OutputSelector.tsx";
import {VuMeterCanvas} from "@/components/audio/VuMeterCanvas.tsx";


export type MixerOutputStatus = {
    name: string;
    audio_device_id: number;
    audio_dev_name: string;

    overflowCount: number;
    underflowCount: number;

    samplerate: number;
    channels: number;
    ndi_output: boolean;

    pan: number;
    volume: number;

    buffer_size: number;
    buffer_used: number;
}

interface MixerOutputProps {
    output: MixerOutputStatus;
    idx: number;
    onUpdateParam: (msgid: number, outputidx:number,value: number,param?: string) => void;
}

const OutputBufferStatus = ({ inputIdx }: { inputIdx: number }) => {
    const { lastJsonMessage } = useWS();

    const input = inputIdx == -1 ? null :inputIdx == 0 ? lastJsonMessage?.audio?.mixer?.master_output : lastJsonMessage?.audio?.mixer?.aux_output;
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

export const MixerOutput = memo(function MixerOutput({ idx,output, onUpdateParam }: MixerOutputProps) {

    const { audioRef } = useWS();
    const [localVolume, setLocalVolume] = useState(Math.cbrt(output.volume));
    const [localPan, setLocalPan] = useState(output.pan * 100);
    const [NDI_out,setNDI_out] = useState(output.ndi_output);

    // Il bordo superiore colorato ora definisce l'identità della colonna
    const getRoutingColor = () => {
        if (idx == 0) return "border-t-green-500 shadow-[0_-4px_10px_rgba(34,197,94,0.2)]";
        if (idx == 1) return "border-t-red-500 shadow-[0_-4px_10px_rgba(239,68,68,0.2)]";
        return "border-t-zinc-800";
    };

    const throttledUpdate = useMemo(
        () =>
            throttle((msgid: number, inputIdx: number, value: any, param?: string) => {
                onUpdateParam(msgid, inputIdx, value, param);
            }, 50, { leading: true, trailing: true }),
        [onUpdateParam]
    );


    useEffect(() => {
        const linearValue = Math.cbrt(output.volume);

        setLocalVolume(linearValue);
    }, [output.volume]);

    useEffect(() => {
        setNDI_out(output.ndi_output);
    }, [output.ndi_output]);

    useEffect(() => {
        setLocalPan(output.pan * 100);
    }, [output.pan]);

    useEffect(() => {
        return () => {
            throttledUpdate.cancel();
        };
    }, [throttledUpdate]);

    return (
        <div className={`flex rounded-lg flex-col w-40 shrink-0 bg-zinc-900/90 border-x border-b border-white/5 border-t-4 ${getRoutingColor()} transition-all`}>

            {/* 1. HEADER COMPATTO */}
            <div className="rounded-lg p-2 bg-black/40 flex flex-col items-center gap-1 border-b border-white/5">
                <span className="text-[10px] font-mono text-zinc-500">OUTPUT</span>
                <div className="w-full text-[9px] font-black uppercase tracking-tighter truncate text-center text-zinc-200 px-1">
                    {idx === 0 ? "MASTER" : "AUX"}
                </div>
            </div>
            <OutputSelectorDialog
                idx={idx}
            />
            {/* 3. CONTROLLI PAN E FX (Icone piccole) */}
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
                        throttledUpdate(3021, idx, v[0] / 100); // Update Server throttled
                    }}
                    onReset={() => {
                        setLocalPan(0);
                        onUpdateParam(3021, idx, 0);
                    }}
                    className="cursor-ew-resize"
                />
                {/* Feedback testuale opzionale sotto */}
                <div className="text-center text-[8px] font-mono text-zinc-400">
                    {localPan === 0 ? "C" : localPan < 0 ? `L${Math.abs(Math.round(localPan))}` : `R${Math.round(localPan)}`}
                </div>
                <div className="flex justify-center w-full pb-2">
                    <button
                        onClick={() => onUpdateParam(3022, idx, NDI_out ? 0 :1 )}
                        className="flex items-center justify-center gap-2 px-3 py-1 rounded-sm border border-white/10 bg-zinc-800 text-zinc-400 text-[10px] font-black transition-all active:scale-95 hover:bg-zinc-700 hover:text-zinc-200"
                    >
                        {/* Il "LED" che si illumina */}
                        <span className={`w-2 h-2 rounded-full transition-all duration-300 ${
                            NDI_out
                                ? 'bg-emerald-400 shadow-[0_0_8px_rgba(52,211,153,0.9)] animate-pulse'
                                : 'bg-zinc-600'
                        }`} />

                        <span className={NDI_out ? "text-zinc-100" : "text-zinc-500"}>
            NDI {NDI_out ? 'ON' : 'OFF'}
        </span>
                    </button>
                </div>

            </div>

            {/* 4. SEZIONE FADER E VU-METER (Verticale Alta) */}
            <div className="rounded-lg p-2 h-72 bg-black/40 flex items-stretch gap-4 justify-center">
                {/* Buffer & VU Meter affiancati stretti */}
                <div className="w-1"><OutputBufferStatus inputIdx={idx} /></div>
                {/*<div className="w-3"><VUMeterOutput inputIdx={idx} active={true} /></div>*/}
                <div className="flex gap-1 h-full">
                    <div className="w-4 h-full">
                        <VuMeterCanvas levels={idx === 0 ? audioRef.current.master : audioRef.current.aux} />
                    </div>
                    <div className="w-4 h-full">
                        <VuMeterCanvas levels={idx === 0 ? audioRef.current.master_prelimit : audioRef.current.aux_prelimit} />
                    </div>
                </div>
                {/* Fader Volume */}
                <div className="flex flex-col items-center gap-2 relative">
                    <MixerSlider
                        value={[localVolume]}
                        mixerout_idx={idx}
                        orientation="vertical"
                        onValueChange={(v) => {

                            const cubicGain = Math.pow(v[0], 3);
                            const cleanJsonValue = Math.round(cubicGain * 10000) / 10000;
                            setLocalVolume(v[0]);
                            throttledUpdate(3020, idx, cleanJsonValue);
                        }}
                        className="h-full py-2"
                    />
                </div>
            </div>
        </div>
    );
}, (prev, next) => {

    return (
        prev.output.volume === next.output.volume &&
        prev.output.pan === next.output.pan &&
        prev.output.ndi_output === next.output.ndi_output
    );
});