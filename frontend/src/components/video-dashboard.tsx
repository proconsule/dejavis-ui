import { useWS } from '../WebSocketContext'
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import {  Gpu,Zap, Activity, Monitor, Maximize, AlertCircle } from "lucide-react"
import { GpuInventory , type GpuInitConfig} from "@/components/video/gpuinventory"

export function VideoDashboard() {
  const { lastJsonMessage , sendMessage} = useWS();
  const video = lastJsonMessage?.video;

  const isRunning = !!video?.running; // Controllo esplicito dello stato
  const isGPUActive = !!video?.gpu_active; // Controllo esplicito dello stato

  const handleInitGpu = (config: GpuInitConfig) => {
    console.log(`[VIDEO] Sending Hard Init for GPU ID: ${config.gpuId}`);

    sendMessage({
      msgid: 200,            // Hard Reset / Power On
      gpu_id: config.gpuId,  // Deve essere un numero
      vsync: config.vsync,
      frameLimit: config.frameLimit, // Occhio alla CamelCase: frameLimit
      fpsValue: config.fpsValue
    });
  };
  
  const handleStopGpu = () => {
    console.log("[VIDEO] Sending Stop Command");
    sendMessage({
      msgid: 201,
      command: "stop_video_engine"
    });
  };

  return (
    <div className="space-y-4 animate-in fade-in duration-500">
      {/* MINI HEADER STATS */}
      <div className="flex flex-wrap gap-2 items-center">
        <div className="flex items-center gap-2 px-2 py-1 rounded border bg-background shadow-sm">
          <Zap className={`h-3 w-3 ${isGPUActive ? "text-amber-500" : "text-slate-400"}`} />
          <span className="text-[9px] font-black uppercase text-muted-foreground">Engine</span>
          <Badge variant="outline" className={`text-[9px] h-4 px-1.5 font-mono ${isGPUActive ? "bg-amber-50 text-amber-700 border-amber-200" : "bg-slate-50 text-slate-500"}`}>
            {isGPUActive ? "LIVE" : "INACTIVE"}
          </Badge>
        </div>
        {/* ... Graphics Badge ... */}
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4 relative">
        {/* STATO INATTIVO OVERLAY (Opzionale, se vuoi bloccare l'interazione) */}
        {!isRunning && (
          <div className="absolute inset-0 z-10 bg-background/20 backdrop-blur-[1px] flex items-center justify-center rounded-xl border border-dashed border-slate-300">
            <div className="bg-white px-4 py-2 rounded-full shadow-lg border flex items-center gap-2 animate-bounce">
              <AlertCircle className="h-4 w-4 text-slate-400" />
              <span className="text-[10px] font-black uppercase tracking-widest text-slate-500">Video Engine Inactive</span>
            </div>
          </div>
        )}

        {/* CURRENT DEVICE CARD */}
        <Card className={`md:col-span-3 border shadow-sm overflow-hidden transition-opacity ${!isRunning ? 'opacity-50' : 'opacity-100'}`}>
          <CardHeader className="py-2 px-4 bg-indigo-500/5 border-b flex flex-row items-center justify-between">
            <div className="flex items-center gap-2">
              <Monitor className="h-3.5 w-3.5 text-indigo-600" />
              <CardTitle className="text-[10px] font-black uppercase tracking-tight text-indigo-900">Current Device</CardTitle>
            </div>
            <span className="text-[9px] font-mono font-bold text-indigo-600 bg-indigo-100 px-1.5 py-0.5 rounded italic">
              {isRunning ? (video?.activeGpu?.type || "Discrete") : "N/A"}
            </span>
          </CardHeader>
          <CardContent className="p-4">
            <div className="flex flex-col md:flex-row md:items-center justify-between gap-4">
              <div className="space-y-1">
                <p className="text-lg font-black text-slate-900 tracking-tight uppercase truncate max-w-[400px]">
                  {isRunning ? video?.activeGpu?.name : "Engine Offline"}
                </p>
                <div className="flex items-center gap-3">
                   <div className="flex items-center gap-1.5 text-[10px] font-bold text-slate-500">
                    <Activity className="h-3 w-3" /> 
                    Vulkan: <span className="font-mono text-slate-900">{isRunning ? video?.activeGpu.vulkanVersion : "---"}</span>
                  </div>
                </div>
              </div>
              {isGPUActive && (
              <div className="flex items-center gap-6 border-l pl-6 border-slate-100">
                <div className="flex flex-col">
                  <span className="text-[8px] font-black text-slate-400 uppercase tracking-widest mb-0.5">Render Resolution</span>
                  <div className="flex items-center gap-1.5 text-lg font-mono font-bold text-slate-800">
                    <Gpu className="h-4 w-4 text-indigo-500" />
                    {isGPUActive ? `${video?.core_w} × ${video?.core_h}` : "0 × 0"}
                  </div>
                </div>
              </div>
              )}
              <div className="flex items-center gap-6 border-l pl-6 border-slate-100">
                <div className="flex flex-col">
                  <span className="text-[8px] font-black text-slate-400 uppercase tracking-widest mb-0.5">Window Resolution</span>
                  <div className="flex items-center gap-1.5 text-lg font-mono font-bold text-slate-800">
                    <Maximize className="h-4 w-4 text-indigo-500" />
                    {`${video?.window_w} × ${video?.window_h}`}
                  </div>
                </div>
              </div>
            </div>
          </CardContent>
        </Card>

        {/* EFFICIENCY CARD */}
        <Card className={`border shadow-sm overflow-hidden transition-all ${!isGPUActive ? 'bg-slate-50 border-slate-200' : ''}`}>
          <CardHeader className="py-2 px-4 bg-emerald-500/5 border-b">
            <CardTitle className="text-[10px] font-black uppercase text-emerald-800 tracking-widest text-center">Efficiency</CardTitle>
          </CardHeader>
          <CardContent className="p-4 flex flex-col items-center justify-center">
            {isGPUActive ? (
              <>
                <div className="flex items-baseline gap-1">
                  <span className="text-4xl font-mono font-black text-emerald-600 tracking-tighter">
                    {video?.fps?.toFixed(0) || "0"}
                  </span>
                  <span className="text-[10px] font-bold text-emerald-700 uppercase">FPS</span>
                </div>
                <div className="w-full mt-3 pt-3 border-t border-emerald-100 flex justify-between items-center">
                   <span className="text-slate-400 text-[8px] uppercase font-black">Frame Time</span>
                   <span className="text-sm font-mono font-bold text-slate-700">{video?.frameTimeMs?.toFixed(2)} ms</span>
                </div>
              </>
            ) : (
              <div className="py-4 text-center">
                <span className="text-[10px] font-black text-slate-300 uppercase italic">Standby Mode</span>
              </div>
            )}
          </CardContent>
        </Card>
      </div>
		  <GpuInventory 
			availableGpus={video?.availableGpus || []}
			activeGpuName={video?.activeGpu?.name}
			isRunning={isRunning}
			onInitGpu={handleInitGpu}
			onStopGpu={handleStopGpu}
		  />
		</div>

  );
}