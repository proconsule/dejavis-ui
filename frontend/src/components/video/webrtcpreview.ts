import { useEffect, useRef, useState, useCallback } from 'react';
import { useWS } from '../../WebSocketContext.tsx';

export type PreviewState =
    | 'idle'
    | 'connecting'
    | 'connected'
    | 'disconnected'
    | 'error';

export type UsePreviewOptions = {
    iceServers?: RTCIceServer[];
    autoStart?: boolean;
};

type SignalMessage = {
    type: 'webrtc_signal';
    role: 'preview';
    session_id?: string;
    payload: {
        type: 'answer' | 'candidate';
        sdp?: string;
        candidate?: string;
        sdpMid?: string;
    };
};

export function useWebRTCPreview(opts: UsePreviewOptions = {}) {
    const { lastJsonMessage, sendMessage } = useWS();
    const {
        iceServers = [{ urls: 'stun:stun.l.google.com:19302' }],
        autoStart = false,
    } = opts;

    const pcRef = useRef<RTCPeerConnection | null>(null);
    const streamRef = useRef<MediaStream | null>(null);

    const iceQueueRef = useRef<RTCIceCandidateInit[]>([]);

    const [stream, setStream] = useState<MediaStream | null>(null);
    const [state, setState] = useState<PreviewState>('idle');
    const [error, setError] = useState<string>('');

    const start = useCallback(async () => {
        // Cleanup eventuale sessione precedente
        if (pcRef.current) {
            try { pcRef.current.close(); } catch {}
            pcRef.current = null;
        }
        if (streamRef.current) {
            streamRef.current.getTracks().forEach(t => t.stop());
            streamRef.current = null;
            setStream(null);
        }
        iceQueueRef.current = [];

        setError('');
        setState('connecting');

        try {
            const pc = new RTCPeerConnection({ iceServers });
            pcRef.current = pc;

            const videoTransceiver = pc.addTransceiver('video', { direction: 'recvonly' });
            pc.addTransceiver('audio', { direction: 'recvonly' });

            if ('getCapabilities' in RTCRtpReceiver) {
                const capabilities = RTCRtpReceiver.getCapabilities('video');
                if (capabilities) {

                    const codecs = capabilities.codecs.filter(c =>
                        c.mimeType === 'video/H264' && c.sdpFmtpLine?.includes('42001f')
                    );

                    const otherCodecs = capabilities.codecs.filter(c =>
                        !(c.mimeType === 'video/H264' && c.sdpFmtpLine?.includes('42001f'))
                    );
                    videoTransceiver.setCodecPreferences([...codecs, ...otherCodecs]);
                }
            }

            pc.ontrack = (ev) => {
                const remoteStream = ev.streams[0];
                if (!remoteStream) return;
                streamRef.current = remoteStream;
                setStream(remoteStream);
            };

            pc.getReceivers().forEach(r => {
                if(r.track.kind === 'video') {
                    console.log("Video Track ID:", r.track.id);
                    console.log("Video Track State:", r.track.readyState); // Deve essere 'live'
                    console.log("Video Track Muted:", r.track.muted);    // Se è true, non arrivano pacchetti
                }
            })

            pc.onicecandidate = (ev) => {
                if (!ev.candidate) return;
                sendMessage({
                    type: 'webrtc_signal',
                    role: 'preview',
                    payload: {
                        type: 'candidate',
                        candidate: ev.candidate.candidate,
                        sdpMid: ev.candidate.sdpMid ?? '',
                    },
                });
            };

            pc.onconnectionstatechange = () => {
                switch (pc.connectionState) {
                    case 'connected':
                        setState('connected');
                        break;
                    case 'disconnected':
                    case 'closed':
                        setState('disconnected');
                        break;
                    case 'failed':
                        setState('error');
                        setError('Connection failed');
                        break;
                }
            };

            const offer = await pc.createOffer();
            await pc.setLocalDescription(offer);

            sendMessage({
                type: 'webrtc_signal',
                role: 'preview',
                payload: {
                    type: 'offer',
                    sdp: offer.sdp,
                },
            });
        } catch (e: any) {
            console.error('[Preview] start failed', e);
            setState('error');
            setError(e?.message ?? 'Unknown error');
        }
    }, [iceServers, sendMessage]);

    const stop = useCallback(() => {
        if (pcRef.current) {
            try {
                sendMessage({
                    type: 'webrtc_signal',
                    role: 'preview',
                    payload: { type: 'close' },
                });
            } catch {}
            try { pcRef.current.close(); } catch {}
            pcRef.current = null;
        }
        if (streamRef.current) {
            streamRef.current.getTracks().forEach(t => t.stop());
            streamRef.current = null;
            setStream(null);
        }
        iceQueueRef.current = [];
        setState('idle');
    }, [sendMessage]);

    useEffect(() => {
        if (!lastJsonMessage) return;
        if (lastJsonMessage.type !== 'webrtc_signal') return;
        if (lastJsonMessage.role !== 'preview') return;

        const msg = lastJsonMessage as SignalMessage;
        const pc = pcRef.current;
        if (!pc) return;

        const handle = async () => {
            try {
                if (msg.payload.type === 'answer' && msg.payload.sdp) {

                    if (pc.signalingState === 'stable') return;

                    await pc.setRemoteDescription({
                        type: 'answer',
                        sdp: msg.payload.sdp,
                    });


                    while (iceQueueRef.current.length > 0) {
                        const cand = iceQueueRef.current.shift();
                        if (cand) {
                            try {
                                await pc.addIceCandidate(cand);
                            } catch (err) {
                                console.warn('[Preview] queued candidate add failed:', err);
                            }
                        }
                    }
                }
                else if (msg.payload.type === 'candidate' && msg.payload.candidate) {
                    const cand: RTCIceCandidateInit = {
                        candidate: msg.payload.candidate,
                        sdpMid: msg.payload.sdpMid ?? '',
                    };

                    if (pc.remoteDescription) {
                        // Answer già applicata, possiamo aggiungere subito
                        await pc.addIceCandidate(cand);
                    } else {
                        // Answer non ancora arrivata: parcheggia
                        iceQueueRef.current.push(cand);
                    }
                }
            } catch (e: any) {
                console.error('[Preview] signal handle error:', e);
            }
        };
        handle();
    }, [lastJsonMessage]);

    useEffect(() => {
        if (autoStart) start();
        return () => {
            stop();
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    return {
        stream,
        state,
        error,
        start,
        stop,
        isConnected: state === 'connected',
    };
}