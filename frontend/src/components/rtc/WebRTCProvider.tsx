import { createContext, useContext, type ReactNode } from 'react';
import { useWebRTCPreview } from '../video/webrtcpreview.ts';

const WebRTCContext = createContext<any>(null);

export function WebRTCProvider({ children }: { children: ReactNode }) {
    const webrtc = useWebRTCPreview({ autoStart: false });
    return (
        <WebRTCContext.Provider value={webrtc}>
            {children}
        </WebRTCContext.Provider>
    );
}

export const useGlobalWebRTC = () => useContext(WebRTCContext);