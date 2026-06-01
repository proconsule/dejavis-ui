import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { WebSocketProvider } from './WebSocketContext'
import {WebRTCProvider} from "@/components/rtc/WebRTCProvider.tsx";

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <WebSocketProvider>
        <WebRTCProvider>
      <App />
        </WebRTCProvider>
    </WebSocketProvider>
  </StrictMode>,
)
