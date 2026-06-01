import { useEffect } from 'react'
import { DeviceSelector} from './audio/deviceselector'
import { Progress } from "@/components/ui/progress"
import { Card, CardContent, CardHeader } from "@/components/ui/card"
import { Mic, Speaker, Zap, Clock, RefreshCw } from "lucide-react"

interface AudioDashboardProps {
  data: any;
  onApplyDevice: (type: 'input' | 'output', id: number, chans: number, rate: number) => void;
  onRequestHardware: () => void;
  onStopDevice: (msgid: number) => void;
}

export function AudioDashboard({ 
  data,
  onApplyDevice, 
  onRequestHardware,
  onStopDevice
}: AudioDashboardProps) {
  
  const audio = data?.audio;
  const buffers = audio?.buffers || { inputFill: 0, outputFill: 0, overflows: 0, underflows: 0 };
  

  // Richiesta automatica lista hardware all'apertura
  useEffect(() => {
    onRequestHardware();
  }, [onRequestHardware]);

  return (
    <div className="flex flex-col gap-6 p-4 max-w-5xl mx-auto animate-in fade-in duration-500">
      
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">

        <Card className="border-l-4 border-l-blue-500 shadow-sm bg-muted/5">
          <CardHeader className="py-3 px-4 flex flex-row items-center justify-between bg-blue-500/5">
            <div className="flex items-center gap-2">
              <Mic className="h-4 w-4 text-blue-500" />
              <span className="text-[11px] font-bold uppercase tracking-tighter">Input Hardware (ID 51)</span>
            </div>
            <button onClick={onRequestHardware} className="p-1 hover:bg-blue-500/10 rounded-md group">
              <RefreshCw className="h-3.5 w-3.5 text-blue-400 group-active:rotate-180 transition-transform" />
            </button>
          </CardHeader>
          <CardContent className="p-4 space-y-4">
            <DeviceSelector 
              type="input"
              devices={[]}
			  data = {audio?.input}
              onApply={(id, chans, rate) => onApplyDevice('input', id, chans, rate)}
			  onStop={() => onStopDevice(53)}
            />

            <div className="space-y-1.5 pt-2 border-t border-border/40">
               <div className="flex justify-between text-[9px] font-bold uppercase text-muted-foreground">
                  <span>Input Buffer</span>
                  <span className={buffers.overflows > 0 ? "text-red-500 animate-pulse font-black" : ""}>
                    Ovf: {buffers.overflows}
                  </span>
               </div>
               <Progress value={(buffers.inputFill / 8192) * 100} className="h-1.5 bg-blue-950/20" />
            </div>
          </CardContent>
        </Card>


        <Card className="border-l-4 border-l-green-500 shadow-sm bg-muted/5">
          <CardHeader className="py-3 px-4 flex flex-row items-center justify-between bg-green-500/5">
            <div className="flex items-center gap-2">
              <Speaker className="h-4 w-4 text-green-500" />
              <span className="text-[11px] font-bold uppercase tracking-tighter">Output Hardware (ID 52)</span>
            </div>
            <div className="flex items-center gap-1 text-[10px] font-mono text-green-600 bg-green-500/10 px-1.5 py-0.5 rounded">
              <Zap className="h-3 w-3" /> {(audio?.output?.latency * 1000).toFixed(1)}ms
            </div>
          </CardHeader>
          <CardContent className="p-4 space-y-4">
            <DeviceSelector 
              type="output"
              devices={[]}
			  data = {audio?.output}
              onApply={(id, chans, rate) => onApplyDevice('output', id, chans, rate)}
			  onStop={() => onStopDevice(54)}
            />

            <div className="space-y-1.5 pt-2 border-t border-border/40">
               <div className="flex justify-between text-[9px] font-bold uppercase text-muted-foreground">
                  <span>Output Buffer</span>
                  <span className={buffers.underflows > 0 ? "text-red-500 animate-pulse font-black" : ""}>
                    Udf: {buffers.underflows}
                  </span>
               </div>
               <Progress value={(buffers.outputFill / 8192) * 100} className="h-1.5 bg-green-950/20" />
               <div className="text-[9px] font-mono text-muted-foreground mt-1 flex justify-between">
                  <span className="flex items-center gap-1"><Clock className="h-2.5 w-2.5" /> {audio?.output?.sampleRate || "---"} Hz</span>
                  <span className="italic">{audio?.output?.channels === 2 ? "Stereo" : "Mono"} Mode</span>
               </div>
            </div>
          </CardContent>
        </Card>

      </div>
    </div>
  );
}