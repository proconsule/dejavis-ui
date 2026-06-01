import { useEffect, useRef, useState, useCallback } from 'react'
import { useWS } from './WebSocketContext'
import { AudioDashboard } from "./components/audio-dashboard"
import { VideoDashboard } from "./components/video-dashboard"
import { SystemOverview } from "./components/system-overview"
import { AudioMixerDashboard} from "./components/audiomixer-dashboard.tsx";
import { ConnectionOverlay } from "./components/connection-overlay"
import { ProjectMDashboard} from "./components/projectm-dashboard.tsx";
import { PlayCircle,Blend,BookAudio ,Wallpaper,LayoutDashboard, Terminal, Monitor } from "lucide-react"
import { 
  Sidebar, SidebarContent, SidebarGroup, SidebarGroupContent, 
  SidebarGroupLabel, SidebarMenu, SidebarMenuButton, SidebarMenuItem, 
  SidebarProvider, SidebarFooter,SidebarTrigger
} from "@/components/ui/sidebar"
import { Separator } from "@/components/ui/separator"
import {AudioPlayerDashboard} from "@/components/audioplayer-dashboard.tsx";
import {VideoMixerDashboard} from "@/components/videomixer-dashboard.tsx";
import {GlobalPiP} from "@/components/rtc/globalpip.tsx";

const connectionLabels: Record<number, string> = {
  0: "Connecting",
  1: "Online",
  2: "Closing",
  3: "Offline",
};

function DashboardLayout() {
  const { lastJsonMessage, sendMessage, readyState } = useWS();
  const [milkdbdata, setmilkdbdata] = useState<any>(null);

  const [systemStatus, setSystemStatus] = useState<any>(null);

  const welcomeSent = useRef(false);
  
  const [activeTab, setActiveTab] = useState<'dashboard' | 'audio' | 'audiomixer' | 'projectm' | 'video' | 'logs' | 'player' |'videomixer'| 'visualdj'| 'shader' >('dashboard');

  const handleStopDevice = (msgid: number) => {
	  // Se usi websocket:
	  sendMessage({ msgid: msgid, path: "" });
	};

  // GESTIONE MESSAGGI IN ARRIVO (Dispatcher)
  useEffect(() => {
    if (!lastJsonMessage) return;

    switch (lastJsonMessage.msgid) {
      case 1: 
        setSystemStatus(lastJsonMessage);
        break;
      case 45:
        /*
        setHardwareData({
          inputs: lastJsonMessage.inputs || [],
          outputs: lastJsonMessage.outputs || []
        });
        */

        break;
      case 4020:
        setmilkdbdata(lastJsonMessage);
        break;
    }
  }, [lastJsonMessage]);


 useEffect(() => {


   if (readyState === 1 && !welcomeSent.current) {
     sendMessage({ command: "welcome", client: "Dejavis-UI" });
     sendMessage({ msgid: 41, path: "" });
     sendMessage({ msgid: 4020 });
     welcomeSent.current = true;
   }
   if (readyState !== 1) welcomeSent.current = false;
 }, [readyState, sendMessage]);

 const requestHardware = useCallback(() => {
   sendMessage({ msgid: 45 });
 }, [sendMessage]);



  const applyAudioDevice = useCallback((type: 'input' | 'output', id: number, chans: number, rate: number) => {
    sendMessage({ 
      msgid: type === 'input' ? 51 : 52, 
      deviceId: id,
      channels: chans,
      sampleRate: rate
    });
  }, [sendMessage]);


      return (
      <div className="relative flex min-h-screen w-full bg-white text-slate-900">
        {/* Overlay di connessione se il backend C++ è offline */}
        {readyState !== 1 && <ConnectionOverlay state={connectionLabels[readyState] || "Unknown"} />}
        {(activeTab !== 'videomixer' && activeTab !== 'dashboard' && activeTab !== 'audiomixer') && <GlobalPiP />}
        {/* SIDEBAR: collapsible="icon" permette la scomparsa lasciando solo i simboli */}
        <Sidebar collapsible="icon" className="border-r shadow-sm transition-all duration-300">
          <SidebarContent>
            <SidebarGroup>
              <SidebarGroupLabel className="font-bold text-slate-400 uppercase text-[10px] tracking-widest">
                DEJAVISUI
              </SidebarGroupLabel>
              <SidebarGroupContent>
                <SidebarMenu>
                  <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('dashboard')} isActive={activeTab === 'dashboard'} tooltip="Dashboard">
                      <LayoutDashboard /> <span>Dashboard</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                  <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('player')} isActive={activeTab === 'player'} tooltip="Media Players">
                      <PlayCircle /> <span>Media Players</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                  <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('audiomixer')} isActive={activeTab === 'audiomixer'} tooltip="Audio Mixer">
                      <BookAudio /> <span>Mixer Audio</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                  <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('videomixer')} isActive={activeTab === 'videomixer'} tooltip="Video Mixer">
                      <Blend /> <span>Mixer Video</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                  <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('projectm')} isActive={activeTab === 'projectm'} tooltip="projectM">
                      <Wallpaper  /> <span>projectM</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                    <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('logs')} isActive={activeTab === 'logs'} tooltip="Logs">
                      <Terminal /> <span>Log Server</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                </SidebarMenu>
              </SidebarGroupContent>
            </SidebarGroup>

            <SidebarGroup>
              <SidebarGroupLabel className="font-bold text-slate-400 uppercase text-[10px] tracking-widest">
                Hardware
              </SidebarGroupLabel>
              <SidebarGroupContent>
                <SidebarMenu>
                  <SidebarMenuItem>
                    <SidebarMenuButton onClick={() => setActiveTab('video')} isActive={activeTab === 'video'} tooltip="Renderer">
                      <Monitor /> <span>Renderer</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                </SidebarMenu>
              </SidebarGroupContent>
            </SidebarGroup>

          </SidebarContent>

          <SidebarFooter className="group-data-[collapsible=icon]:p-2">
            <Separator />

          </SidebarFooter>
        </Sidebar>

        <main className="flex-1 overflow-auto bg-slate-50/30">
          <div className="w-full p-8 space-y-0">

            {/* HEADER CON TRIGGER A SCOMPARSA */}
            <header className="flex items-center justify-between border-b border-slate-200 pb-1">
              <div className="flex items-center gap-4">
                {/* Il pulsante che apre/chiude la sidebar */}
                <SidebarTrigger className="h-9 w-9 border border-slate-200 shadow-sm hover:bg-slate-100 transition-colors" />

                <div>
                  <h1 className="text-2xl font-black tracking-tighter text-slate-900 uppercase">
                    {activeTab === 'dashboard' && "Dashboard"}
                    {activeTab === 'player' && "Media Players"}
                    {activeTab === 'audiomixer' && "Mixer Audio"}
                    {activeTab === 'videomixer' && "Mixer Video"}
                    {activeTab === 'projectm' && "projectM"}
                    {activeTab === 'visualdj' && "Visual DJ"}
                    {activeTab === 'shader' && "Shader Editor"}
                    {activeTab === 'audio' && "Audio Configuration"}
                    {activeTab === 'video' && "Vulkan Pipeline"}
                    {activeTab === 'logs' && "Server Console"}
                  </h1>
                  <p className="text-slate-500 text-sm mt-1 italic font-medium">
                    {activeTab === 'audiomixer' && "Trivial Audio Mixer"}
                  </p>
                </div>
              </div>


            </header>

            {/* CONTENUTO DINAMICO */}
            <div className="">
              {activeTab === 'dashboard' && (
                  <SystemOverview />
              )}

              {activeTab === 'audio' && (
                  <AudioDashboard
                      data={systemStatus}
                      onApplyDevice={applyAudioDevice}
                      onRequestHardware={requestHardware}
                      onStopDevice={handleStopDevice}
                  />
              )}

              {activeTab === 'audiomixer' && (
                  <AudioMixerDashboard />
              )}

              {activeTab === 'videomixer' && (
                  <VideoMixerDashboard />
              )}

              {activeTab === 'projectm' && (
                  <ProjectMDashboard
                      milkdbdata={milkdbdata}

                  />
              )}


              {activeTab === 'video' && <VideoDashboard />}

              {activeTab === 'player' && (
                  <div className="space-y-6">
                    <AudioPlayerDashboard />

                  </div>
              )}

              {activeTab === 'logs' && (
                  <div className="bg-slate-900 text-emerald-400 p-6 rounded-xl font-mono text-xs shadow-2xl border-2 border-slate-800">
                    <p className="opacity-40 mb-2"># DEJAVIS SERVER LOGS - {new Date().toLocaleDateString()}</p>
                    <p>{`> [${new Date().toLocaleTimeString()}] Handshake riuscito.`}</p>
                    <p>{`> [${new Date().toLocaleTimeString()}] ReadyState: ${connectionLabels[readyState]}`}</p>
                    <p className="mt-4 text-emerald-600 animate-pulse">// In attesa di segnali hardware...</p>
                  </div>
              )}
            </div>

            {activeTab === 'dashboard' && (
                <div className="pt-8 flex justify-center">

                </div>
            )}
          </div>
        </main>
      </div>
  );
}

export default function App() {
  return (
       <SidebarProvider>
        <DashboardLayout />
      </SidebarProvider>

  );
}