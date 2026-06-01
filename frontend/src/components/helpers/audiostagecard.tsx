import { Card, CardContent } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import { Progress } from "@/components/ui/progress"
import { Mic, Speaker, Hash, Zap, Activity } from "lucide-react"

interface AudioStageCardProps {
  type: 'input' | 'output';
  deviceData: any;
  bufferData: {
    fill: number;
    errors: number;
  };
  onDoubleClick?: React.MouseEventHandler<HTMLElement>;
}

export function AudioStageCard({ type, deviceData, bufferData,onDoubleClick }: AudioStageCardProps) {
  const isInput = type === 'input';
  
  const Icon = isInput ? Mic : Speaker;
  const accentClass = isInput 
    ? "text-blue-600 bg-blue-50 border-blue-100" 
    : "text-emerald-600 bg-emerald-50 border-emerald-100";
  
  const borderHover = isInput ? "hover:border-blue-200" : "hover:border-emerald-200";

  return (
    <Card onDoubleClick={onDoubleClick} className={`border-2 border-slate-100 shadow-sm transition-colors ${borderHover}`}>
	  
      <CardContent className="pt-6">
        {/* HEADER: Icona, Frequenza e Stato */}
        <div className="flex items-center justify-between mb-4">
          <div className={`p-2 rounded-lg border ${accentClass}`}>
            <Icon className="h-5 w-5" />
          </div>
          <div className="flex gap-1.5 items-center">
            {/* Frequenza di campionamento messa in evidenza */}
            <Badge variant="outline" className="text-[10px] font-mono border-slate-200 text-slate-600">
              {deviceData?.sampleRate ? `${deviceData.sampleRate / 1000}kHz` : "-- kHz"}
            </Badge>
            <Badge className={deviceData?.active ? "bg-green-600" : "bg-slate-200 text-slate-500"}>
              {deviceData?.active ? "LIVE" : "OFF"}
            </Badge>
          </div>
        </div>

        {/* INFO DISPOSITIVO E CANALI */}
        <div className="mb-4">
          <div className="flex justify-between items-start">
            <p className="text-[10px] font-black text-slate-400 uppercase tracking-widest">
              {isInput ? "Audio Input" : "Audio Output"}
            </p>
            {/* Badge Canali: Mono/Stereo */}
            <Badge variant="secondary" className="text-[9px] h-4 px-1.5 font-bold uppercase bg-slate-100 text-slate-500">
              {deviceData?.channels === 1 ? "Mono" : deviceData?.channels === 2 ? "Stereo" : `${deviceData?.channels || 0} Ch`}
            </Badge>
          </div>
          <p className="text-sm font-bold truncate text-slate-800 mt-1">
            {deviceData?.deviceName || "Nessun Dispositivo"}
          </p>
          <div className="flex items-center gap-2 mt-1 text-[10px] text-muted-foreground font-mono">
            <Hash className="h-3 w-3" /> ID: {deviceData?.deviceId ?? "--"}
            <span className="opacity-50">|</span>
            <Zap className="h-3 w-3" /> {((deviceData?.latency || 0) * 1000).toFixed(1)}ms
          </div>
        </div>

        {/* MONITORAGGIO TELEMETRIA BUFFER */}
        <div className="space-y-2 pt-1 border-t border-slate-50">
          <div className="flex justify-between text-[10px] font-mono font-bold uppercase">
            <div className="flex items-center gap-1 text-slate-500">
              <Activity className="h-3 w-3" />
              <span>Buffer Fill</span>
            </div>
            <span className={bufferData.errors > 0 ? "text-red-500 animate-pulse" : "text-slate-400"}>
              {isInput ? "OVF" : "UDF"}: {bufferData.errors}
            </span>
          </div>
          
          <Progress value={(bufferData.fill / 8192) * 100} className="h-1.5" />
          
          <div className="text-[9px] font-mono text-muted-foreground text-right">
            {bufferData.fill} / 8192 samples
          </div>
        </div>
      </CardContent>
    </Card>
  );
}