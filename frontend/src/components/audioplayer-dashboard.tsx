"use client";

import {useEffect, useState} from 'react';
import { useWS } from '../WebSocketContext';
import {AudioPlayerView} from "@/components/audioplayerview.tsx";
import {VideoPlayerView} from "@/components/videoplayerview.tsx";



export function AudioPlayerDashboard() {
    const { lastJsonMessage, sendMessage } = useWS();
    const [mixerId, setMixerId] = useState<number>(0);
    const [inputType,setinputType] = useState<number>(-1);

    const mixerInputs = lastJsonMessage?.audio?.mixer?.inputs || [];

    const handleSelectChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
        if(parseInt(e.target.value)==-1)return;
        const id:number = parseInt(e.target.value);
        setMixerId(id);
        setinputType(mixerInputs[id].type);
        sendMessage({msgid: 5003, idx: id ,type: mixerInputs[id].type});

    };

    useEffect(() => {
        // Sostituisci mixerId con il valore iniziale desiderato (es. 0)
        setMixerId(-1);
        setinputType(-1);
    }, []); // L'array vuoto garantisce che venga eseguito solo all'apertura

    useEffect(() => {
        if (lastJsonMessage?.msgid !== 1) return;
        //setStatusmsg(lastJsonMessage);

        console.log(lastJsonMessage);
        //sendMessage({msgid: 5003, idx: mixerId});
    }, [lastJsonMessage]);

    return (
        <div>
        <select
            value={mixerId}
            onChange={handleSelectChange}
            className="bg-zinc-800 text-zinc-200 border border-white/10 rounded px-2 py-1 text-xs focus:outline-none focus:ring-1 focus:ring-emerald-500"
        >
            <option value="-1" selected>Select Player</option>
            {mixerInputs.map((input: any, index: number) => (

                <option key={index} value={index} disabled={input.type != 0&&input.type != 3}>
                    CH {index} - {input.type == 0 ? "FILE PLAYER": input.type == 3 ? "VIDEO PLAYER" :""}
                </option>
            ))}
        </select>


            {(
                mixerId>=0 && inputType == 0?
                <AudioPlayerView
                    idx={mixerId}
                    sendSignal={sendMessage}
                    //status={statusMSG}
                />:mixerId>=0 && inputType == 3? <VideoPlayerView
                        audio_mixer_idx={mixerId}
                        video_mixer_idx={mixerInputs[mixerId]?.videomixer_idx}
                        sendSignal={sendMessage}
                        //status={statusMSG}
                    /> :"" )}
        </div>

    );
}