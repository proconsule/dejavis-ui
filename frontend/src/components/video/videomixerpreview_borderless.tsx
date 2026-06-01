import { useEffect, useRef, useState, forwardRef, useImperativeHandle } from 'react';
import { useGlobalWebRTC } from "@/components/rtc/WebRTCProvider.tsx";
import { Volume2, VolumeX } from 'lucide-react';

type Props = {
    className?: string;
    objectFit?: 'contain' | 'cover' | 'fill';
    allowControls?: boolean;
};

// Usiamo forwardRef per esporre il canvas al padre
export const MixerPreviewBackground = forwardRef<HTMLCanvasElement, Props>(({
                                                                                className = '',
                                                                                objectFit = 'contain',
                                                                                allowControls = false
                                                                            }, ref) => {

    const videoRef = useRef<HTMLVideoElement | null>(null);
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const { stream, state } = useGlobalWebRTC();

    const [isMuted, setIsMuted] = useState(false);
    const [isHovered, setIsHovered] = useState(false);

    // Colleghiamo il ref esterno al nostro canvas interno
    useImperativeHandle(ref, () => canvasRef.current!);

    useEffect(() => {
        if (!videoRef.current || !stream) return;
        if (videoRef.current.srcObject !== stream) {
            videoRef.current.srcObject = stream;
        }

        // Loop per disegnare il video sul canvas (necessario per il contagocce)
        let raf: number;
        const ctx = canvasRef.current?.getContext('2d', { willReadFrequently: true });

        const draw = () => {
            if (videoRef.current && canvasRef.current && ctx) {
                const v = videoRef.current;
                const c = canvasRef.current;

                // Sincronizza dimensioni canvas con sorgente video
                if (c.width !== v.videoWidth) {
                    c.width = v.videoWidth;
                    c.height = v.videoHeight;
                }

                ctx.drawImage(v, 0, 0, c.width, c.height);
            }
            raf = requestAnimationFrame(draw);
        };

        draw();
        return () => cancelAnimationFrame(raf);
    }, [stream]);

    if (state !== 'connected' || !stream) return null;

    return (
        <div
            className={`relative overflow-hidden group ${className}`}
            onMouseEnter={() => setIsHovered(true)}
            onMouseLeave={() => setIsHovered(false)}
        >
            {/* Video nascosto (serve solo come sorgente) */}
            <video
                ref={videoRef}
                autoPlay
                playsInline
                muted={isMuted}
                className="hidden"
            />

            {/* Canvas visibile (quello da cui campioneremo i pixel) */}
            <canvas
                ref={canvasRef}
                className={`w-full h-full object-${objectFit}`}
            />

            {allowControls && (
                <div className={`
                    absolute inset-0 flex items-end justify-end p-4 transition-opacity duration-300
                    bg-gradient-to-t from-black/40 to-transparent pointer-events-none
                    ${isHovered ? 'opacity-100' : 'opacity-0'}
                `}>
                    <button
                        onClick={() => setIsMuted(!isMuted)}
                        className="p-2.5 bg-black/60 hover:bg-black/80 text-white rounded-lg backdrop-blur-md pointer-events-auto border border-white/10 transition-all active:scale-90"
                    >
                        {isMuted ? <VolumeX size={20} className="text-rose-500" /> : <Volume2 size={20} className="text-emerald-500" />}
                    </button>
                </div>
            )}
        </div>
    );
});

MixerPreviewBackground.displayName = "MixerPreviewBackground";
export default MixerPreviewBackground;