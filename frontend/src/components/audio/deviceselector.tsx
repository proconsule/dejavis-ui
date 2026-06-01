import { useState, useMemo, useEffect } from 'react'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select"
import { Button } from "@/components/ui/button"
import { Check, Power, Mic, Speaker, Activity, Layers } from "lucide-react"

export type AudioDevice = {
  id: number;
  name: string;
  api: string;
  maxIn: number;
  maxOut: number;
  defaultSampleRate: number;
  maxSampleRate: number;
  isDefaultInput: boolean;
  isDefaultOutput: boolean;
};

export type AudioDevConfig = {
  id: number;
  deviceId: number;
  sampleRate: number;
  channels: number;
};

interface DeviceSelectorProps {
  devices: AudioDevice[];
  type: 'input' | 'output';
  data?: {
    active: boolean;
    deviceId?: number;
    deviceName?: string;
    sampleRate?: number;
    latency?: number;
    channels?: number;
  };
  onApply: (id: number, channels: number, sampleRate: number) => void;
  onStop: () => void;
}

export function DeviceSelector({ devices, type, data, onApply, onStop }: DeviceSelectorProps) {
  const [localSelection, setLocalSelection] = useState<string>("");
  const [selChannels, setSelChannels] = useState<string>("");
  const [selRate, setSelRate] = useState<string>("");

  const isStreaming = !!data?.active;

  const systemDefault = useMemo(() =>
    devices.find(d => type === 'input' ? d.isDefaultInput : d.isDefaultOutput),
    [devices, type]
  );

  const currentId = useMemo(() => {
    if (isStreaming) {
      if (data?.deviceId !== undefined) return data.deviceId.toString();
      const found = devices.find(d => d.name === data?.deviceName || d.name.startsWith(data?.deviceName || ""));
      return found ? found.id.toString() : (localSelection || systemDefault?.id.toString() || "");
    }
    return localSelection || systemDefault?.id.toString() || "";
  }, [isStreaming, data, devices, localSelection, systemDefault]);

  const currentDevice = useMemo(() => 
    devices.find(d => d.id.toString() === currentId),
    [devices, currentId]
  );

  useEffect(() => {
    if (currentDevice && !isStreaming) {
      const maxCh = type === 'input' ? currentDevice.maxIn : currentDevice.maxOut;
      setSelChannels(maxCh >= 2 ? "2" : maxCh.toString());
      setSelRate(currentDevice.defaultSampleRate.toString());
    }
  }, [currentDevice, isStreaming, type]);

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between mb-1">
        <label className="text-[10px] uppercase font-black text-muted-foreground italic flex items-center gap-1.5">
          {type === 'input' ? <Mic className="h-3.5 w-3.5" /> : <Speaker className="h-3.5 w-3.5" />}
          {type} {isStreaming ? "Streaming" : "Status"}
        </label>
        
        {isStreaming && (
          <div className="flex gap-1.5 animate-in fade-in zoom-in">
            <div className="flex items-center gap-1 text-[9px] bg-slate-100 px-1.5 py-0.5 rounded border font-mono">
              <Layers className="h-2.5 w-2.5" /> {data?.channels || "--"}ch
            </div>
            <div className={`flex items-center gap-1 text-[9px] px-1.5 py-0.5 rounded border font-mono ${type === 'input' ? 'bg-blue-50 text-blue-600 border-blue-200' : 'bg-green-50 text-emerald-600 border-green-200'}`}>
              <Activity className="h-2.5 w-2.5" /> {data?.sampleRate ? `${Math.round(data.sampleRate/1000)}k` : "--"}
            </div>
          </div>
        )}
      </div>

      <div className="flex gap-2">
        <Select 
          value={currentId} 
          onValueChange={setLocalSelection}
          disabled={isStreaming}
        >
          <SelectTrigger className={`w-full text-xs font-bold h-9 bg-background ${isStreaming ? 'border-primary/40 ring-1 ring-primary/10' : ''}`}>
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {devices.map((d) => (
              <SelectItem key={d.id} value={d.id.toString()}>
                {d.name} {d.id === systemDefault?.id && "★"}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>

        {isStreaming && (
          <Button variant="destructive" size="icon" onClick={onStop} className="h-9 w-10 shrink-0">
            <Power className="h-4 w-4" />
          </Button>
        )}
      </div>

      {!isStreaming ? (
        currentDevice && (
          <div className="animate-in fade-in slide-in-from-top-2 bg-muted/20 border border-dashed rounded-lg p-3 space-y-3">
            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1">
                <label className="text-[8px] uppercase font-black text-muted-foreground ml-1">Channels</label>
                <Select value={selChannels} onValueChange={setSelChannels}>
                  <SelectTrigger className="h-8 text-[10px] bg-background"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    <SelectItem value="1">1 Ch (Mono)</SelectItem>
                    {(type === 'input' ? currentDevice.maxIn : currentDevice.maxOut) >= 2 && <SelectItem value="2">2 Ch (Stereo)</SelectItem>}
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1">
                <label className="text-[8px] uppercase font-black text-muted-foreground ml-1">Sample Rate</label>
                <Select value={selRate} onValueChange={setSelRate}>
                  <SelectTrigger className="h-8 text-[10px] bg-background"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    {[192000, 96000, 48000, 44100].filter(r => r <= currentDevice.maxSampleRate).map(r => (
                      <SelectItem key={r} value={r.toString()}>{r.toLocaleString()} Hz</SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            </div>
            <Button 
              onClick={() => onApply(currentDevice.id, parseInt(selChannels), parseInt(selRate))}
              className={`w-full text-white h-8 text-[10px] font-black uppercase ${type === 'input' ? 'bg-blue-600 hover:bg-blue-700' : 'bg-green-600 hover:bg-green-700'}`}
            >
              <Check className="mr-2 h-3.5 w-3.5" /> Initialize {type}
            </Button>
          </div>
        )
      ) : (
        <Button 
          variant="outline" 
          onClick={onStop}
          className="w-full h-10 border-red-500/20 text-red-600 hover:bg-red-50 font-black uppercase text-[10px] tracking-widest gap-2"
        >
          <Power className="h-3.5 w-3.5" /> Stop Hardware Engine
        </Button>
      )}
    </div>
  );
}