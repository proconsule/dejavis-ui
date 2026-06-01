"use client";

import { createContext, useContext, useState, useEffect, useRef, useCallback, type ReactNode } from 'react';
import useWebSocket, { ReadyState } from 'react-use-websocket';
import pcmWorkerRaw from './pcm_worker.js?raw';

interface AudioBuffers {
    inputs: Float32Array[];
    master: Float32Array;
    master_prelimit: Float32Array;
    aux: Float32Array;
    aux_prelimit: Float32Array;
}

interface WebSocketContextType {
    lastJsonMessage: any;
    readyState: ReadyState;
    sendMessage: (jsonMessage: any) => void;
    audioRef: React.RefObject<AudioBuffers>;
    startAudioMonitor: () => void;
    stopAudioMonitor: () => void;
    monitorStatus: string;
}

const WebSocketContext = createContext<WebSocketContextType | undefined>(undefined);

export const WebSocketProvider = ({ children }: { children: ReactNode }) => {
    const [socketUrl, setSocketUrl] = useState<string | null>(null);
    const [currentJson, setCurrentJson] = useState<any>(null);
    const [monitorStatus, setMonitorStatus] = useState<'off' | 'connecting' | 'on'>('off');

    const pc = useRef<RTCPeerConnection | null>(null);
    const audioCtxRef = useRef<AudioContext | null>(null);
    const iceQueue = useRef<RTCIceCandidateInit[]>([]);

    const fftAnimationFrameRef = useRef<number | null>(null);

    const audioRef = useRef<AudioBuffers>({
        inputs : Array.from({ length: 16 }, () => new Float32Array(2)),
        master: new Float32Array(2),
        master_prelimit: new Float32Array(2),
        aux: new Float32Array(2),
        aux_prelimit: new Float32Array(2)
    });

    useEffect(() => {
        const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const host = window.location.hostname;
        setSocketUrl(`${wsProtocol}//${host}:8848/ws`);
    }, []);

    const stopAudioMonitor = useCallback(() => {
        console.log("Arresto Monitor Audio...");
        if (fftAnimationFrameRef.current) {
            cancelAnimationFrame(fftAnimationFrameRef.current);
            fftAnimationFrameRef.current = null;
        }
        if (pc.current) {
            pc.current.close();
            pc.current = null;
        }
        if (audioCtxRef.current) {
            audioCtxRef.current.close();
            audioCtxRef.current = null;
        }
        setMonitorStatus('off');
    }, []);

    const onMessageHandler = useCallback(async (event: MessageEvent) => {
        if (event.data instanceof ArrayBuffer) {
            const buffer = new Float32Array(event.data);
            if (buffer.length >= 54) {
                for (let i = 0; i < 16; i++) {
                    const start = i * 2;
                    const end = start + 2;
                    audioRef.current.inputs[i].set(buffer.subarray(start, end));
                }
                audioRef.current.master.set(buffer.subarray(46, 48));
                audioRef.current.master_prelimit.set(buffer.subarray(48, 50));
                audioRef.current.aux.set(buffer.subarray(50, 52));
                audioRef.current.aux_prelimit.set(buffer.subarray(52, 54));
            }
            return;
        }

        if (typeof event.data === "string") {
            try {
                const json = JSON.parse(event.data);

                if (json.type === 'webrtc_signal') {
                    const role = json.role ?? 'audio';

                    if (role !== 'audio') {
                        // Non è per l'audio monitor: propaga al consumer esterno
                        setCurrentJson(json);
                        return;
                    }

                    // Da qui in poi: signal per l'audio monitor
                    const { payload } = json;
                    if (!pc.current) return;

                    if (payload.type === 'answer') {
                        if (pc.current.signalingState === 'stable') return;
                        await pc.current.setRemoteDescription(new RTCSessionDescription(payload));
                        while (iceQueue.current.length > 0) {
                            const cand = iceQueue.current.shift();
                            if (cand) await pc.current.addIceCandidate(new RTCIceCandidate(cand));
                        }
                    } else if (payload.type === 'candidate') {
                        if (pc.current.remoteDescription) {
                            await pc.current.addIceCandidate(new RTCIceCandidate(payload));
                        } else {
                            iceQueue.current.push(payload);
                        }
                    }
                    return;
                }

                setCurrentJson(json);
            } catch (e) { console.error("Errore JSON:", e); }
        }
    }, []);

    const { sendJsonMessage, readyState } = useWebSocket(socketUrl, {
        onMessage: onMessageHandler,
        shouldReconnect: () => true,
        reconnectInterval: 3000,
        share: true,
        filter: () => false,
        onOpen: (e: any) => {
            const ws = e.target;
            ws.binaryType = "arraybuffer";
            ws.send(JSON.stringify({ command: "welcome" }));
        }
    }, socketUrl !== null);

    const startAudioMonitor = useCallback(async () => {
        stopAudioMonitor();
        setMonitorStatus('connecting');

        pc.current = new RTCPeerConnection({ iceServers: [] });

        const dc = pc.current.createDataChannel("audio_monitor");
        dc.onopen = () => {
            setMonitorStatus('on');
            setupAudioPlayback(dc, audioCtxRef);
        };

        pc.current.onicecandidate = (event) => {
            if (event.candidate) {
                sendJsonMessage({
                    type: 'webrtc_signal',
                    role: 'audio',                                    // NUOVO: marca esplicita
                    payload: { type: 'candidate', ...event.candidate.toJSON() }
                });
            }
        };

        const offer = await pc.current.createOffer();
        await pc.current.setLocalDescription(offer);
        sendJsonMessage({
            type: 'webrtc_signal',
            role: 'audio',                                            // NUOVO: marca esplicita
            payload: { type: 'offer', sdp: offer.sdp }
        });
    }, [sendJsonMessage, stopAudioMonitor]);

    return (
        <WebSocketContext.Provider value={{
            lastJsonMessage: currentJson,
            readyState,
            sendMessage: sendJsonMessage,
            audioRef,
            startAudioMonitor,
            stopAudioMonitor,
            monitorStatus
        }}>
            {children}
        </WebSocketContext.Provider>
    );
};

async function setupAudioPlayback(dc: RTCDataChannel, audioCtxRef: React.MutableRefObject<AudioContext | null>) {
    const audioCtx = new (window.AudioContext || (window as any).webkitAudioContext)({
        latencyHint: 'interactive',
        sampleRate: 48000
    });
    audioCtxRef.current = audioCtx;

    if (audioCtx.state === 'suspended') await audioCtx.resume();

    try {
        const blob = new Blob([pcmWorkerRaw], { type: 'application/javascript' });
        const workerUrl = URL.createObjectURL(blob);
        await audioCtx.audioWorklet.addModule(workerUrl);

        const playbackNode = new AudioWorkletNode(audioCtx, 'pcm-processor', {
            outputChannelCount: [2],
            numberOfInputs: 0,
            numberOfOutputs: 1
        });
        playbackNode.connect(audioCtx.destination);

        playbackNode.port.onmessage = (event) => {
            if (event.data.type === 'levels') {
                window.dispatchEvent(new CustomEvent('audio-monitor-levels', { detail: event.data }));
            }
            if (event.data.type === 'fft') {
                window.dispatchEvent(new CustomEvent('audio-fft-data', { detail: event.data.data }));
            }
        };

        dc.binaryType = 'arraybuffer';
        dc.onmessage = (event: MessageEvent) => {
            if (event.data instanceof ArrayBuffer) {
                playbackNode.port.postMessage(new Float32Array(event.data));
            }
        };

        dc.onclose = () => audioCtx.close();
    } catch (err) {
        console.error("Errore AudioWorklet:", err);
    }
}

export const useWS = () => {
    const context = useContext(WebSocketContext);
    if (!context) throw new Error("useWS deve essere usato in WebSocketProvider");
    return context;
};
