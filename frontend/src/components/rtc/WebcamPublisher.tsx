import { useEffect, useRef, useState } from 'react';
import { useWebRTCPublisher } from '../video/useWebRTCPublisher';

export function WebcamPublisher() {
    const videoRef = useRef<HTMLVideoElement>(null);
    const [localStream, setLocalStream] = useState<MediaStream | null>(null);

    const { state, error } = useWebRTCPublisher(localStream, { autoStart: true });

    useEffect(() => {
        const startCamera = async () => {
            try {
                const stream = await navigator.mediaDevices.getUserMedia({ video: true, audio: true });
                setLocalStream(stream);


                if (videoRef.current) {
                    videoRef.current.srcObject = stream;
                }
            } catch (err) {
                console.error("Errore webcam:", err);
            }
        };

        startCamera();

        return () => {
            localStream?.getTracks().forEach(track => track.stop());
        };
    }, []);

    return (
        <div>
            <video ref={videoRef} autoPlay playsInline muted />
            <p>Stato Connessione: {state}</p>
            {error && <p className="text-red-500">{error}</p>}
        </div>
    );
}