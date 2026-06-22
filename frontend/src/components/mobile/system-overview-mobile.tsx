import { useWS } from '../../WebSocketContext';
import { Volume2, Server, AlertCircle } from 'lucide-react';
import { Card } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Badge } from '@/components/ui/badge';

export function SystemOverviewMobile() {
    const {
        lastJsonMessage,
        readyState,
        startAudioMonitor,
        stopAudioMonitor,
        monitorStatus
    } = useWS();

    // Estraiamo i dati di sistema dal messaggio msgid: 1
    const systemData = lastJsonMessage?.msgid === 1 ? lastJsonMessage : null;

    return (
        <div className="flex flex-col gap-4 p-4 pb-20">
            {/* SECTION 1: STATUS CARD */}
            <Card className="p-4 bg-white shadow-sm border-slate-200">
                <div className="flex items-center justify-between mb-4">
                    <div className="flex items-center gap-2">
                        <Server className="w-5 h-5 text-slate-500" />
                        <h2 className="font-bold text-slate-700 uppercase text-sm tracking-tight">Server Status</h2>
                    </div>
                    <Badge variant={readyState === 1 ? "default" : "destructive"} className="text-[10px]">
                        {readyState === 1 ? "ONLINE" : "OFFLINE"}
                    </Badge>
                </div>

                <div className="grid grid-cols-2 gap-2 text-xs">
                    <div className="p-2 bg-slate-50 rounded-lg">
                        <p className="text-slate-400 mb-1">CPU Load</p>
                        <p className="font-mono font-bold text-slate-800">
                            {systemData?.cpu || '--'}%
                        </p>
                    </div>
                    <div className="p-2 bg-slate-50 rounded-lg">
                        <p className="text-slate-400 mb-1">RAM Usage</p>
                        <p className="font-mono font-bold text-slate-800">
                            {systemData?.ram || '--'} MB
                        </p>
                    </div>
                </div>
            </Card>

            {/* SECTION 2: AUDIO MONITOR (WEB RTC) */}
            <Card className="p-4 bg-slate-900 text-white shadow-xl border-none overflow-hidden relative">
                <div className="flex items-center justify-between mb-6 relative z-10">
                    <div className="flex items-center gap-2">
                        <Volume2 className="w-5 h-5 text-emerald-400" />
                        <h2 className="font-bold uppercase text-sm tracking-tight text-slate-200">Audio Monitor</h2>
                    </div>
                    <Badge variant="outline" className="text-emerald-400 border-emerald-400/30 text-[10px]">
                        {monitorStatus.toUpperCase()}
                    </Badge>
                </div>

                <div className="flex flex-col items-center justify-center gap-4 py-4 relative z-10">
                    {monitorStatus === 'off' && (
                        <Button
                            onClick={startAudioMonitor}
                            className="w-full py-6 bg-emerald-500 hover:bg-emerald-600 text-white rounded-xl font-bold transition-all transform active:scale-95"
                        >
                            ACTIVATE MONITOR
                        </Button>
                    )}

                    {monitorStatus === 'connecting' && (
                        <div className="flex flex-col items-center gap-2">
                            <div className="w-8 h-8 border-4 border-emerald-500/30 border-t-emerald-500 rounded-full animate-spin" />
                            <p className="text-xs text-slate-400 animate-pulse">Establishing WebRTC Link...</p>
                        </div>
                    )}

                    {monitorStatus === 'on' && (
                        <div className="w-full space-y-4">
                            <div className="flex justify-between items-center">
                                <p className="text-xs text-slate-400 italic">Live Stream Active</p>
                                <Button
                                    variant="destructive"
                                    size="sm"
                                    onClick={stopAudioMonitor}
                                    className="h-7 text-[10px] px-2"
                                >
                                    STOP
                                </Button>
                            </div>
                            {/* Qui andrebbe un componente semplificato di FFTCanvas o VUMeter */}
                            <div className="h-20 w-full bg-slate-800 rounded-lg flex items-end justify-center gap-1 p-1">
                                {/* Placeholder per le barre FFT */}
                                {[...Array(16)].map((_, i) => (
                                    <div key={i} className="w-1 bg-emerald-500/50 rounded-t-full transition-all duration-75" style={{ height: `${Math.random() * 100}%` }} />
                                ))}
                            </div>
                        </div>
                    )}
                </div>
            </Card>

            {/* SECTION 3: QUICK ALERTS */}
            {readyState !== 1 && (
                <div className="flex items-center gap-3 p-3 bg-red-50 border border-red-100 rounded-xl text-red-600">
                    <AlertCircle className="w-5 h-5" />
                    <p className="text-xs font-medium">Server connection lost. Please check the backend.</p>
                </div>
            )}
        </div>
    );
}