import { useEffect, useRef, useState } from 'react';

export const WebcamCamera = () => {
    const videoRef = useRef<HTMLVideoElement | null>(null);
    const [error, setError] = useState<string | null>(null);

    useEffect(() => {
        const startWebcam = async () => {
            try {
                // Richiediamo video e audio
                const stream = await navigator.mediaDevices.getUserMedia({
                    video: true,
                    audio: true
                });

                if (videoRef.current) {
                    videoRef.current.srcObject = stream;
                }
            } catch (err) {
                console.error("Errore nell'accesso alla webcam:", err);
                setError("Permesso negato o webcam non trovata.");
            }
        };

        startWebcam();

        return () => {
            if (videoRef.current && videoRef.current.srcObject) {
                const stream = videoRef.current.srcObject as MediaStream;
                stream.getTracks().forEach(track => track.stop());
            }
        };
    }, []);

    return (
        <div className="relative w-full max-w-2xl mx-auto border-2 border-zinc-800 rounded-xl overflow-hidden">
            {error ? (
                <div className="p-4 bg-rose-500/10 text-rose-500 text-sm">{error}</div>
            ) : (
                <video
                    ref={videoRef}
                    autoPlay
                    playsInline
                    className="w-full h-full object-cover"
                />
            )}
        </div>
    );
};