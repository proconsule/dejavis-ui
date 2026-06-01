import { useState } from "react"
import { Cpu, Gauge, Power, Activity, Monitor } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Switch } from "@/components/ui/switch"
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select"
import { Label } from "@/components/ui/label"

// --- Tipi ---
interface GpuDevice {
  id: number;
  name: string;
  type: string;
  vendorId: string;
  vramGB: number;
}

export interface GpuInitConfig {
  gpuId: number;
  vsync: boolean;
  frameLimit: boolean;
  fpsValue: number;
}

interface GpuInventoryProps {
  availableGpus: GpuDevice[];
  activeGpuName?: string;
  isRunning: boolean;
  onInitGpu: (config: GpuInitConfig) => void;
  onStopGpu: () => void;
}

function GpuRow({ 
  gpu, 
  isActive, 
  isRunning, 
  onInit
}: { 
  gpu: GpuDevice, 
  isActive: boolean, 
  isRunning: boolean, 
  onInit: (config: GpuInitConfig) => void,
}) {
  const [vsync, setVsync] = useState(true);
  const [frameLimit, setFrameLimit] = useState(false);
  const [fpsValue, setFpsValue] = useState("60");
  const [coreresValue, setcoreresValue] = useState("1024");
  const [windowresValue, setwindowresValueValue] = useState("1024");

  const handleVsync = (checked: boolean) => {
    setVsync(checked);
    if (checked) setFrameLimit(false);
  };

  const handleLimit = (checked: boolean) => {
    setFrameLimit(checked);
    if (checked) setVsync(false);
  };

  return (
    <div className={`rounded-lg border transition-all ${
      isActive ? 'border-indigo-500 bg-indigo-50/30' : 'border-slate-200 bg-white'
    }`}>
      {/* AREA INFO DISPOSITIVO */}
      <div className="flex items-center justify-between p-4 border-b border-slate-100">
        <div className="flex items-center gap-4">
          <div className={`p-2 rounded-md ${isActive ? 'bg-indigo-600 text-white' : 'bg-slate-100 text-slate-500'}`}>
            <Cpu className="h-5 w-5" />
          </div>
          <div>
            <h3 className="text-sm font-bold text-slate-900">{gpu.name}</h3>
            <p className="text-[10px] font-mono text-slate-500 uppercase tracking-tight">
              Hardware ID: {gpu.id} • Type: {gpu.type} • RAM: {gpu.vramGB.toFixed(2)}GB
            </p>
          </div>
        </div>

        {isActive ? (
          <div className="flex items-center gap-3">
            <span className="flex items-center gap-1.5 text-[10px] font-black text-indigo-700 bg-indigo-100 px-3 py-1 rounded-full uppercase tracking-wider animate-pulse">
              <Activity className="h-3 w-3" /> Online
            </span>
          </div>
        ) : (
          <span className="text-[10px] font-bold text-slate-400 uppercase tracking-widest bg-slate-50 px-2 py-1 rounded">
            {isRunning ? 'LOCKED' : 'READY'}
          </span>
        )}
      </div>

      {/* AREA PARAMETRI INIT (Solo se engine è fermo) */}
      {!isRunning && (
        <div className="p-4 bg-slate-50/50">
          <div className="grid grid-cols-12 gap-6 items-end">



            <div className="col-span-2 space-y-2">
              <Label className="text-[10px] font-black text-slate-500 uppercase flex items-center gap-2">
                <Monitor className="h-3 w-3" /> Core Resolution
              </Label>
                <Select value={coreresValue} onValueChange={setcoreresValue}>
                  <SelectTrigger className="h-10 w-28 font-bold text-xs bg-white border-slate-200 shadow-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="640">640x480</SelectItem>
                    <SelectItem value="800">800x600</SelectItem>
                    <SelectItem value="1024">1024x768</SelectItem>
                    <SelectItem value="1280">1280x1024</SelectItem>
                  </SelectContent>
                </Select>
            </div>
            <div className="col-span-2 space-y-2">
              <Label className="text-[10px] font-black text-slate-500 uppercase flex items-center gap-2">
                <Monitor className="h-3 w-3" /> Window Resolution
              </Label>
              <Select value={windowresValue} onValueChange={setwindowresValueValue}>
                <SelectTrigger className="h-10 w-28 font-bold text-xs bg-white border-slate-200 shadow-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="640">640x480</SelectItem>
                  <SelectItem value="800">800x600</SelectItem>
                  <SelectItem value="1024">1024x768</SelectItem>
                  <SelectItem value="1280">1280x1024</SelectItem>
                </SelectContent>
              </Select>
            </div>

            {/* VSYNC SECTION */}
            <div className="col-span-3 space-y-3">
              <Label className="text-[10px] font-black text-slate-500 uppercase flex items-center gap-2">
                <Monitor className="h-3 w-3" /> Sync Mode
              </Label>
              <div className="flex items-center gap-3 bg-white p-2 rounded-md border border-slate-200 h-10">
                <Switch checked={vsync} onCheckedChange={handleVsync} />
                <span className="text-xs font-bold text-slate-700">{vsync ? 'VSync ON' : 'VSync OFF'}</span>
              </div>
            </div>

            {/* LIMITER SECTION */}
            <div className="col-span-5 space-y-3">
              <Label className="text-[10px] font-black text-slate-500 uppercase flex items-center gap-2">
                <Gauge className="h-3 w-3" /> Frame Limiter
              </Label>
              <div className="flex items-center gap-2">
                <div className="flex items-center gap-3 bg-white p-2 rounded-md border border-slate-200 h-10 grow">
                  <Switch checked={frameLimit} onCheckedChange={handleLimit} />
                  <span className="text-xs font-bold text-slate-700">Limit</span>
                </div>
                <Select disabled={!frameLimit} value={fpsValue} onValueChange={setFpsValue}>
                  <SelectTrigger className="h-10 w-28 font-bold text-xs bg-white border-slate-200 shadow-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="30">30 FPS</SelectItem>
                    <SelectItem value="60">60 FPS</SelectItem>
                    <SelectItem value="90">90 FPS</SelectItem>
                    <SelectItem value="120">120 FPS</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>

            {/* ACTION SECTION */}
            <div className="col-span-4">
              <Button 
                onClick={() => onInit({
                  gpuId: gpu.id,
                  vsync: vsync,
                  frameLimit: frameLimit,
                  fpsValue: parseInt(fpsValue)
                })}
                className="w-full h-10 bg-slate-900 hover:bg-indigo-600 text-white font-black text-xs uppercase tracking-widest gap-2 shadow-lg transition-all"
              >
                <Power className="h-4 w-4" /> Init Device {gpu.id}
              </Button>
            </div>

          </div>
        </div>
      )}
    </div>
  );
}

export function GpuInventory({ availableGpus, activeGpuName, isRunning, onInitGpu }: GpuInventoryProps) {
  return (
    <Card className="border-none shadow-none bg-transparent">
      <CardHeader className="px-0 pt-0 pb-4">
        <div className="flex items-center gap-2">
          <Activity className="h-4 w-4 text-indigo-600" />
          <CardTitle className="text-xs font-black uppercase tracking-[0.2em] text-slate-500">
            System Hardware / Init Controller
          </CardTitle>
        </div>
      </CardHeader>
      <CardContent className="px-0 grid gap-4">
        {availableGpus?.map((gpu) => (
          <GpuRow 
            key={gpu.id}
            gpu={gpu}
            isActive={isRunning && gpu.name === activeGpuName}
            isRunning={isRunning}
            onInit={onInitGpu}
          />
        ))}
      </CardContent>
    </Card>
  );
}