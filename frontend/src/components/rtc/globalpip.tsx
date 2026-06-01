import { useRef, useEffect } from 'react';
import { useGlobalWebRTC } from './WebRTCProvider';
import { Rnd } from "react-rnd";

export function GlobalPiP() {
    const { stream, state } = useGlobalWebRTC();
    const videoRef = useRef<HTMLVideoElement>(null);

    const isVisible = state === 'connected';

    useEffect(() => {
        if (videoRef.current && stream) {
            videoRef.current.srcObject = stream;
        }
    }, [stream, isVisible]);

    if (!isVisible) return null;

    return (
        <Rnd
            default={{
                x: window.innerWidth - 320 - 24,
                y: window.innerHeight - 180 - 24,
                width: 320,
                height: 180,
            }}
            minWidth={160}
            minHeight={90}
            lockAspectRatio={16/9}
            bounds="window"
            className="z-[9999] group shadow-2xl"
            dragHandleClassName="handle"
        >
            <div className="relative w-full h-full bg-black border-2 border-emerald-500 rounded-lg overflow-hidden flex flex-col">

                <div className="handle absolute inset-0 cursor-move z-10" />

                <video
                    ref={videoRef}
                    autoPlay
                    playsInline
                    muted={false}
                    className="w-full h-full object-cover"
                />

                <div className="absolute top-2 left-2 z-20 bg-emerald-500 text-black text-[9px] font-bold px-1.5 py-0.5 rounded uppercase pointer-events-none">
                    Live PiP
                </div>

            </div>
        </Rnd>
    );
}