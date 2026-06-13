"use client";

import { useState} from 'react';
import { useWS } from '../WebSocketContext';
import {AudioPlayerView} from "@/components/audioplayerview.tsx";
import {VideoPlayerView} from "@/components/videoplayerview.tsx";
import {Playersmall_Card} from "@/components/helpers/playersmall_cards.tsx";
import {ChevronLeft} from "lucide-react"; // Importato ChevronLeft



export function AudioPlayerDashboard() {
    const { lastJsonMessage, sendMessage } = useWS();
    const [mixerId, setMixerId] = useState<number>(-1);
    const [inputType, setInputType] = useState<number>(-1);

    const mixerInputs = lastJsonMessage?.audio?.mixer?.inputs || [];

    return (
        <div className="flex flex-col h-full p-4">
            {mixerId === -1 ? (
                <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 lg:grid-cols-4 gap-4 animate-in fade-in duration-300">
                    {mixerInputs
                        .filter((input: any) => input.active)
                        .map((input: any, index: number) => (
                            <Playersmall_Card
                                key={index}
                                audioIdx={index}
                                video_mixer_idx={input.videomixer_idx}
                                onHit={() => {
                                    setMixerId(index);
                                    setInputType(input.type);
                                    sendMessage({msgid: 5003, idx: index});
                                }}
                            />
                        ))
                    }
                    {mixerInputs.filter((i: any) => i.active).length === 0 && (
                        <div className="col-span-full flex flex-col items-center justify-center py-20 opacity-30 uppercase tracking-widest font-mono text-sm italic">
                            All channels idle.
                        </div>
                    )}
                </div>
            ) : (
                <div className="flex flex-col h-full animate-in zoom-in-95 duration-200">
                    <button
                        onClick={() => { setMixerId(-1); setInputType(-1); }}
                        className="group flex items-center gap-2 text-xs font-mono text-slate-500 hover:text-purple-400 mb-6 w-fit p-2 rounded-lg border border-white/10 hover:bg-white/5 transition-all"
                    >
                        <ChevronLeft className="h-4 w-4 group-hover:-translate-x-1 transition-transform" />
                        BACK TO PLAYER SELECTION
                    </button>

                    <div className="flex-1 overflow-hidden relative">
                        {inputType === 0 && (
                            <AudioPlayerView
                                idx={mixerId}
                                sendSignal={sendMessage}
                            />
                        )}
                        {inputType === 3 && (
                            <VideoPlayerView
                                audio_mixer_idx={mixerId}
                                video_mixer_idx={mixerInputs[mixerId]?.videomixer_idx}
                                sendSignal={sendMessage}
                            />
                        )}
                    </div>
                </div>
            )}
        </div>
    );
}