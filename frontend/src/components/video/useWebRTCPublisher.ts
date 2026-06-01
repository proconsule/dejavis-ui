import { useEffect, useRef, useState, useCallback } from 'react';
import { useWS } from '../../WebSocketContext.tsx';

export type PublishState =
    | 'idle'
    | 'connecting'
    | 'connected'
    | 'disconnected'
    | 'error';

export type UsePublisherOptions = {
    iceServers?: RTCIceServer[];
    autoStart?: boolean;
};

// Adattato per il ruolo di publisher
type SignalMessage = {
    type: 'webrtc_signal';
    role: 'publisher';
    session_id?: string;
    payload: {
        type: 'answer' | 'candidate';
        sdp?: string;
        candidate?: string;
        sdpMid?: string;
    };
};

export function useWebRTCPublisher(localStream: MediaStream | null, opts: UsePublisherOptions = {}) {
    const { lastJsonMessage, sendMessage } = useWS();
    const {
        iceServers = [{ urls: 'stun:stun.l.google.com:19302' }],
        autoStart = false,
    } = opts;

    const pcRef = useRef<RTCPeerConnection | null>(null);
    const iceQueueRef = useRef<RTCIceCandidateInit[]>([]);

    const [state, setState] = useState<PublishState>('idle');
    const [error, setError] = useState<string>('');

    const start = useCallback(async () => {
        if (!localStream) {
            setError('Nessun MediaStream locale fornito');
            return;
        }

        // Cleanup eventuale sessione precedente
        if (pcRef.current) {
            try { pcRef.current.close(); } catch {}
            pcRef.current = null;
        }
        iceQueueRef.current = [];

        setError('');
        setState('connecting');

        try {
            const pc = new RTCPeerConnection({ iceServers });
            pcRef.current = pc;

            // 1. Aggiungiamo le tracce del nostro stream al PeerConnection
            localStream.getTracks().forEach(track => {
                pc.addTrack(track, localStream);
            });

            // Opzionale: Forza codec H.264 per l'invio (come nel tuo preview)
            if ('getCapabilities' in RTCRtpSender) {
                const capabilities = RTCRtpSender.getCapabilities('video');
                if (capabilities) {
                    const codecs = capabilities.codecs.filter(c =>
                        c.mimeType === 'video/H264' && c.sdpFmtpLine?.includes('42001f')
                    );
                    const otherCodecs = capabilities.codecs.filter(c =>
                        !(c.mimeType === 'video/H264' && c.sdpFmtpLine?.includes('42001f'))
                    );

                    // Applica le preferenze ai transceiver video appena creati dal addTrack
                    pc.getTransceivers().forEach(transceiver => {
                        if (transceiver.sender.track?.kind === 'video' && transceiver.setCodecPreferences) {
                            transceiver.setCodecPreferences([...codecs, ...otherCodecs]);
                        }
                    });
                }
            }

            pc.onicecandidate = (ev) => {
                if (!ev.candidate) return;
                sendMessage({
                    type: 'webrtc_signal',
                    role: 'publisher',
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
                role: 'publisher', // Specifichiamo che stiamo inviando
                payload: {
                    type: 'offer',
                    sdp: offer.sdp,
                },
            });
        } catch (e: any) {
            console.error('[Publisher] start failed', e);
            setState('error');
            setError(e?.message ?? 'Unknown error');
        }
    }, [iceServers, localStream, sendMessage]);

    const stop = useCallback(() => {
        if (pcRef.current) {
            try {
                sendMessage({
                    type: 'webrtc_signal',
                    role: 'publisher',
                    payload: { type: 'close' },
                });
            } catch {}
            try { pcRef.current.close(); } catch {}
            pcRef.current = null;
        }
        iceQueueRef.current = [];
        setState('idle');
    }, [sendMessage]);

    useEffect(() => {
        if (!lastJsonMessage) return;
        if (lastJsonMessage.type !== 'webrtc_signal') return;
        if (lastJsonMessage.role !== 'publisher') return; // Ascoltiamo solo le risposte per il publisher

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

                    // === DRENA LA CODA DEI CANDIDATES ARRIVATI PRIMA DELL'ANSWER ===
                    while (iceQueueRef.current.length > 0) {
                        const cand = iceQueueRef.current.shift();
                        if (cand) {
                            try {
                                await pc.addIceCandidate(cand);
                            } catch (err) {
                                console.warn('[Publisher] queued candidate add failed:', err);
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
                        await pc.addIceCandidate(cand);
                    } else {
                        iceQueueRef.current.push(cand);
                    }
                }
            } catch (e: any) {
                console.error('[Publisher] signal handle error:', e);
            }
        };
        handle();
    }, [lastJsonMessage]);

    useEffect(() => {
        // Autostart solo se abbiamo uno stream valido pronto
        if (autoStart && localStream) {
            start();
        }
        return () => {
            stop();
        };
    }, [autoStart, localStream, start, stop]);

    return {
        state,
        error,
        start,
        stop,
        isConnected: state === 'connected',
    };
}