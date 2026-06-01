import { useEffect, useRef } from 'react';
import rtcWorkerRaw from '../rtcaudio_worker.js?raw';

export function useWorkletAnalysis(stream: MediaStream | null) {
    const fftRef = useRef<Float32Array>(new Float32Array(16));

    useEffect(() => {
        if (!stream || stream.getAudioTracks().length === 0) return;

        let audioCtx: AudioContext | null = null;

        const setupWorklet = async () => {
            try {
                audioCtx = new (window.AudioContext || (window as any).webkitAudioContext)();

                if (audioCtx.state === 'suspended') {
                    await audioCtx.resume();
                }

                const blob = new Blob([rtcWorkerRaw], { type: 'application/javascript' });
                const workerUrl = URL.createObjectURL(blob);

                await audioCtx.audioWorklet.addModule(workerUrl);

                if (audioCtx.state === 'closed') return;

                const source = audioCtx.createMediaStreamSource(stream);
                const workletNode = new AudioWorkletNode(audioCtx, 'rtcaudio-processor');

                workletNode.port.onmessage = (event) => {
                    if (event.data.type === 'fft') {
                        fftRef.current = event.data.data;
                    }
                };

                // === MODIFICA CRITICA QUI ===
                // Creiamo un buco nero per l'audio: Gain = 0
                const silenceNode = audioCtx.createGain();
                silenceNode.gain.value = 0;

                // Colleghiamo la catena: Stream -> Worklet -> Silenzio -> Casse
                source.connect(workletNode);
                workletNode.connect(silenceNode);
                silenceNode.connect(audioCtx.destination);

            } catch (e) {
                console.error("Errore critico AudioWorklet:", e);
            }
        };

        setupWorklet();

        return () => {
            if (audioCtx && audioCtx.state !== 'closed') {
                audioCtx.close();
            }
        };
    }, [stream]);

    return fftRef;
}