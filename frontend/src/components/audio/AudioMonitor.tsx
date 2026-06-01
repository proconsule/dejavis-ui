"use client";

import { useCallback,useState } from 'react';
import { useWS } from '../../WebSocketContext';
import {FFTMeter, VUMeter} from "@/components/audio/AudioMonitor_components.tsx";

export const AudioMonitor = () => {
    const { monitorStatus, startAudioMonitor,stopAudioMonitor,sendMessage } = useWS();
    const [monitorSource, setMonitorSource] = useState<'master' | 'aux'>('master');

    const handleToggleMonitor = useCallback(() => {
        if (monitorStatus === 'off') {
            startAudioMonitor();
        } else {
            stopAudioMonitor();
        }
    }, [monitorStatus, startAudioMonitor]);

    const handleSourceChange = (source: 'master' | 'aux') => {
        setMonitorSource(source);

        if(source === 'master') {
            sendMessage({
                msgid: 3040,
                source: 0
            });
        }
        if(source === 'aux') {
            sendMessage({
                msgid: 3040,
                source: 1
            });
        }



    };

    return (
        <div className="flex flex-col p-4 bg-zinc-900 rounded-lg border border-zinc-700 w-64 shadow-xl">
            <h3 className="text-white text-[10px] font-bold mb-4 uppercase tracking-tighter opacity-50">
                Remote Audio Monitor (RTC PCM UNCOMPRESSED)
            </h3>

            {/* Selettore Sorgente */}
            <div className="flex gap-1 mb-4">
                {['master', 'aux'].map((src) => {
                    const isActive = monitorSource === src;

                    // Definiamo le classi dinamiche in base alla sorgente attiva
                    const activeClasses = src === 'master'
                        ? "border-t-green-500 bg-green-500/10 text-green-400"
                        : "border-t-red-600 bg-red-600/10 text-red-400";

                    const inactiveClasses = "border-t-transparent bg-black/40 text-gray-600";

                    return (
                        <button
                            key={src}
                            onClick={() => handleSourceChange(src as 'master' | 'aux')}
                            className={`flex-1 py-1 text-[9px] font-mono transition-all border-t-4 ${
                                isActive ? activeClasses : inactiveClasses
                            }`}
                        >
                            {src.toUpperCase()}
                        </button>
                    );
                })}
            </div>

            <div className="flex items-center justify-between mb-6">
            <span className={`text-[10px] font-bold px-2 py-0.5 rounded ${
                monitorStatus === 'on' ? 'bg-green-900 text-green-300' :
                    monitorStatus === 'connecting' ? 'bg-yellow-900 text-yellow-300' :
                        'bg-red-900 text-red-300'
            }`}>
                {monitorStatus?.toUpperCase()}
            </span>

                <button
                    onClick={handleToggleMonitor}
                    disabled={monitorStatus === 'connecting'}
                    className={`px-3 py-1.5 rounded font-bold text-[10px] transition-colors ${
                        monitorStatus === 'on'
                            ? 'bg-red-600 hover:bg-red-700 text-white'
                            : 'bg-blue-600 hover:bg-blue-700 text-white'
                    }`}
                >
                    {monitorStatus === 'on' ? 'STOP' : 'ASCOLTA'}
                </button>
            </div>

            {monitorStatus === 'on' && (
                <div className="space-y-4 animate-in fade-in duration-500">
                    <VUMeter />
                    <FFTMeter />
                </div>
            )}
        </div>
    );
};