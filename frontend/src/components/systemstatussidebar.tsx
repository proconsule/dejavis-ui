import {
  SidebarGroup,
  SidebarGroupLabel,
  SidebarGroupContent,
} from "@/components/ui/sidebar"
import { Mic, Music, Cpu, Activity, Speaker, RefreshCw } from "lucide-react"
import { memo } from "react"

interface SystemStatusSidebarProps {
  systemStatus: any;
}

export const SystemStatusSidebar = memo(function SystemStatusSidebar({ systemStatus }: SystemStatusSidebarProps) {

  const audio = systemStatus?.audio || {};
  const isPlayerActive = audio?.file?.isPlaying;

  const trackTitle = audio?.file?.title || "Streaming...";
  const inputName = isPlayerActive ? trackTitle : (audio?.input?.deviceName || "Inactive");
  const inputHz = isPlayerActive ? audio?.file?.sampleRate : audio?.input?.sampleRate;


  const outputName = audio?.output?.deviceName || "System Default";
  const outputHz = audio?.output?.sampleRate || 0;

  const isResampling = inputHz > 0 && (
      audio?.file?.isResampling ||
      (outputHz > 0 && inputHz !== outputHz)
  );

  const InputIcon = isPlayerActive ? Music : Mic;

  return (
      <SidebarGroup className="select-none overflow-hidden">
        <SidebarGroupLabel className="font-bold text-slate-400 font-mono text-[9px] tracking-tighter">
          SYSTEM MONITOR
        </SidebarGroupLabel>

        <SidebarGroupContent>
          <div className="flex flex-col gap-4 px-4 py-2">

            {/* 1. ACTIVE SOURCE */}
            <div className="flex flex-col gap-1.5">
              <div className="flex items-center gap-2 text-muted-foreground">
                <InputIcon className={`h-3.5 w-3.5 ${isPlayerActive ? "text-purple-500" : "text-slate-400"}`} />
                <span className="text-[10px] uppercase font-bold italic tracking-wider text-slate-500">
                Source
              </span>
              </div>

              <div className="flex flex-col overflow-hidden">
                <div className="relative h-[14px] overflow-hidden">
                  {isPlayerActive ? (
                      <div className="animate-marquee-sidebar">
                        <span className="text-[11px] font-black text-purple-600 pr-8">{inputName}</span>
                        <span className="text-[11px] font-black text-purple-600 pr-8">{inputName}</span>
                      </div>
                  ) : (
                      <span className="text-[11px] truncate font-black text-slate-700 block w-full">
                    {inputName}
                  </span>
                  )}
                </div>

                <div className="flex items-center gap-2 mt-1 min-h-[12px]">
                <span className="text-[9px] font-mono font-bold text-slate-400">
                  {inputHz > 0 ? `${inputHz.toLocaleString()} Hz` : "--- Hz"}
                </span>
                  {isResampling && (
                      <span className="flex items-center gap-1 text-[7px] font-black bg-amber-100 text-amber-700 px-1 rounded border border-amber-200 uppercase tracking-tighter">
                    <RefreshCw className="h-2 w-2 animate-spin-slow" />
                    RESAMP
                  </span>
                  )}
                </div>
              </div>
            </div>

            {/* 2. AUDIO OUTPUT */}
            <div className="flex flex-col gap-1.5">
              <div className="flex items-center gap-2 text-muted-foreground">
                <Speaker className="h-3.5 w-3.5 text-emerald-500" />
                <span className="text-[10px] uppercase font-bold italic tracking-wider text-slate-500">
                Output
              </span>
              </div>
              <div className="flex flex-col">
              <span className="text-[11px] truncate font-black text-slate-700 leading-none">
                {outputName}
              </span>
                <span className="text-[9px] font-mono font-bold text-emerald-600/70 mt-1">
                {outputHz > 0 ? `${outputHz.toLocaleString()} Hz` : "--- Hz"}
              </span>
              </div>
            </div>

            {/* 3. GPU RENDERING (HW Monitor) */}
            <div className="flex flex-col gap-1.5">
              <div className="flex items-center gap-2 text-muted-foreground">
                <Cpu className="h-3.5 w-3.5 text-indigo-500" />
                <span className="text-[10px] uppercase font-bold italic tracking-wider text-slate-500">
                Engine
              </span>
              </div>
              <div className="flex flex-col gap-2">
                <div className="flex items-center gap-2">
                <span className="text-[10px] font-black font-mono text-indigo-600 bg-indigo-50 px-1.5 py-0.5 rounded border border-indigo-100">
                  {Math.round(systemStatus?.video?.fps || 0)} <span className="text-[8px] opacity-70 uppercase">fps</span>
                </span>
                  <span className="text-[10px] truncate font-bold text-slate-500 font-mono italic">
                  {systemStatus?.video?.activeGpu?.name || "Software"}
                </span>
                </div>

                {systemStatus?.video?.activeGpu && (
                    <div className="flex items-center gap-1 opacity-50">
                      <Activity className="h-2.5 w-2.5 text-indigo-400" />
                      <span className="text-[8px] font-bold uppercase tracking-tight text-slate-500">Hardware Accelerated</span>
                    </div>
                )}
              </div>
            </div>

          </div>
        </SidebarGroupContent>
      </SidebarGroup>
  )
});