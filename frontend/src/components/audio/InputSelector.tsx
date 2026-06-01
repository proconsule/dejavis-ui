"use client";

import { useEffect, useState } from "react";
import * as Dialog from "@radix-ui/react-dialog";
import {
    Cable,
    FileAudio,
    Mic,
    X,
    ChevronRight,
    Activity,
    Music,
    ArrowLeft,
    Check,
    Settings2,
    Info,
    FileVideoCamera,
    Link
} from "lucide-react";
import type {AudioDevConfig, AudioDevice} from "@/components/audio/deviceselector.tsx";
import { cn } from "@/lib/utils.ts";
import { useWS } from "@/WebSocketContext.tsx";

interface InputSelectorProps {
    idx: number;
    currentInput: string;
}

export const InputSelectorDialog = ({ idx, currentInput }: InputSelectorProps) => {

    const [view, setView] = useState<0 | 1 | 2 | 3>(0);
    const [isOpen, setIsOpen] = useState(false);
    const [selectedDevice, setSelectedDevice] = useState<AudioDevice | null>(null);
    const [configuredDevices, setConfiguredDevices] = useState<AudioDevConfig | null>(null);
    const [streamUrl, setStreamUrl] = useState("");

    const { lastJsonMessage, sendMessage } = useWS();

    const [hardwareData, setHardwareData] = useState<{
        inputs: AudioDevice[],
        outputs: AudioDevice[]
    }>({ inputs: [], outputs: [] });

    useEffect(() => {
        if (!lastJsonMessage) return;
        if (lastJsonMessage.msgid === 45) {
            setHardwareData({
                inputs: lastJsonMessage.inputs || [],
                outputs: lastJsonMessage.outputs || []
            });
        }
    }, [lastJsonMessage]);

    const handleSelectInternal = (type: 0 | 1 | 3 | 5) => {
        sendMessage({ msgid: 5001, type, inputidx: idx, devid: -1 });
        setIsOpen(false);
    };

    const confirmStreamUrl = () => {
        if (!streamUrl.trim()) return;

        sendMessage({
            msgid: 5001,
            type: 4,
            inputidx: idx,
            devid: -1,
            url: streamUrl.trim()
        });

        setIsOpen(false);
        setStreamUrl("");
        setTimeout(() => setView(0), 200);
    };

    const openDeviceDetails = (dev: AudioDevice) => {
        setSelectedDevice(dev);
        setConfiguredDevices({
            id: idx,
            deviceId: dev.id,
            sampleRate: dev.defaultSampleRate,
            channels: dev.maxIn
        });
        setView(2);
    };

    const confirmHardwareSelection = () => {
        if (!selectedDevice) return;

        sendMessage({
            msgid: 6001,
            type: 2,
            inputidx: idx,
            devid: configuredDevices?.deviceId,
            samplerate: configuredDevices?.sampleRate,
            channels: configuredDevices?.channels
        });
        setIsOpen(false);
    };

    const handleOpenChange = (open: boolean) => {
        if (open) sendMessage({ msgid: 6000 });
        setIsOpen(open);
        if (!open) {
            setTimeout(() => {
                setView(0);
                setSelectedDevice(null);
                setStreamUrl(""); // NUOVO: Resetta anche l'URL in chiusura
            }, 200);
        }
    };

    return (
        <Dialog.Root open={isOpen} onOpenChange={handleOpenChange}>
            <Dialog.Trigger asChild>
                <button className="flex items-center gap-2 px-3 py-1.5 rounded bg-zinc-800 border border-white/10 text-zinc-400 hover:text-emerald-400 hover:border-emerald-500/50 transition-all active:scale-95 text-[9px] font-bold uppercase tracking-widest group">
                    <Cable size={12} className={cn("transition-colors", currentInput ? "text-emerald-500" : "group-hover:text-emerald-400")} />
                    <span className="truncate max-w-[120px]">{currentInput || "Unassigned"}</span>
                </button>
            </Dialog.Trigger>

            <Dialog.Portal>
                <Dialog.Overlay className="fixed inset-0 bg-black/60 backdrop-blur-md z-[100] animate-in fade-in duration-200" />
                <Dialog.Content className="fixed top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[400px] max-h-[85vh] overflow-hidden flex flex-col bg-zinc-950 border border-white/10 shadow-2xl z-[101] rounded-xl focus:outline-none">

                    {/* HEADER DINAMICO */}
                    <div className="p-4 border-b border-white/5 flex justify-between items-center bg-zinc-900/50">
                        <div className="flex items-center gap-3">
                            {view !== 0 && (
                                <button

                                    onClick={() => view === 3 ? setView(0) : setView((prev) => (prev - 1) as any)}
                                    className="p-1.5 hover:bg-white/5 rounded-md text-zinc-400 hover:text-white transition-colors"
                                >
                                    <ArrowLeft size={16} />
                                </button>
                            )}
                            <div>
                                <Dialog.Title className="text-zinc-100 font-bold uppercase tracking-tight text-xs">
                                    {view === 0 && "Ch. Routing"}
                                    {view === 1 && "Hardware Input"}
                                    {view === 2 && "Device Configuration"}
                                    {view === 3 && "Stream URL Setup"}
                                </Dialog.Title>
                            </div>
                        </div>
                        <Dialog.Close className="text-zinc-500 hover:text-white transition-colors p-2">
                            <X size={18} />
                        </Dialog.Close>
                    </div>

                    <div className="flex-1 overflow-y-auto p-4 min-h-[350px]">
                        {view === 0 && (
                            <div className="space-y-6 animate-in fade-in slide-in-from-left-2 duration-200">
                                <section>
                                    <h3 className="text-[9px] font-black text-blue-400/80 uppercase tracking-widest mb-3 flex items-center gap-2">
                                        <Music size={11} /> Internal
                                    </h3>
                                    <div className="grid gap-1.5">
                                        <InputOption icon={<FileAudio size={16} />} label="Audio Player" sublabel="File decoder" onClick={() => handleSelectInternal(0)} />
                                        <InputOption icon={<Activity size={16} />} label="Audio Stream" sublabel="remote audio" onClick={() => handleSelectInternal(1)} />

                                        <InputOption icon={<Activity size={16} />} label="Video Stream" sublabel="remote video" onClick={() => setView(3)} />

                                        <InputOption icon={<FileVideoCamera size={16} />} label="Video Player" sublabel="Video File Player" onClick={() => handleSelectInternal(3)} />
                                        <InputOption icon={<FileVideoCamera size={16} />} label="NDI SOURCE" sublabel="NDI SOURCE" onClick={() => handleSelectInternal(5)} />
                                    </div>
                                </section>
                                <section className="pt-2 border-t border-white/5">
                                    <h3 className="text-[9px] font-black text-emerald-500 uppercase tracking-widest mb-3 flex items-center gap-2">
                                        <Mic size={11} /> External
                                    </h3>
                                    <InputOption icon={<Cable size={16} />} label="Physical Hardware" sublabel={`${hardwareData.inputs.length} detected`} onClick={() => setView(1)} />
                                </section>
                            </div>
                        )}

                        {view === 1 && (
                            <div className="space-y-2 animate-in fade-in slide-in-from-right-4 duration-200">
                                {hardwareData.inputs.map((dev) => (
                                    <InputOption
                                        key={`${dev.api}-${dev.id}`}
                                        icon={<Mic size={14} />}
                                        label={dev.name}
                                        sublabel={`${dev.api.toUpperCase()}`}
                                        isCurrent={currentInput === dev.name}
                                        onClick={() => openDeviceDetails(dev)}
                                    />
                                ))}
                            </div>
                        )}

                        {view === 2 && selectedDevice && (
                            <div className="space-y-6 animate-in zoom-in-95 duration-200">
                                {/* Ometto il codice view 2 per brevità, resta invariato */}
                                <div className="p-4 rounded-xl bg-emerald-500/5 border border-emerald-500/10 flex flex-col items-center text-center">
                                    <div className="p-3 bg-emerald-500/20 rounded-full mb-3 text-emerald-400">
                                        <Settings2 size={24} />
                                    </div>
                                    <h2 className="text-sm font-bold text-white">{selectedDevice.name}</h2>
                                    <span className="text-[10px] text-zinc-500 font-mono mt-1 uppercase">{selectedDevice.api} Driver</span>
                                </div>

                                <div className="grid grid-cols-2 gap-2">
                                    <InfoCard label="Channels" value={`${selectedDevice.maxIn} IN`} icon={<Cable size={12}/>} />
                                    <InfoCard label="Sample Rate" value={`${selectedDevice.defaultSampleRate / 1000} kHz`} icon={<Activity size={12}/>} />
                                    <InfoCard label="Device ID" value={selectedDevice.id.toString()} icon={<Info size={12}/>} />
                                    <InfoCard label="Preferred" value={selectedDevice.isDefaultInput ? "YES" : "NO"} icon={<Check size={12}/>} />
                                </div>

                                <div className="flex gap-2 pt-4">
                                    <button
                                        onClick={() => setView(1)}
                                        className="flex-1 px-4 py-2 rounded-lg bg-zinc-900 border border-white/5 text-zinc-400 text-[10px] font-bold uppercase hover:bg-zinc-800 transition-colors"
                                    >
                                        Cancel
                                    </button>
                                    <button
                                        onClick={confirmHardwareSelection}
                                        className="flex-1 px-4 py-2 rounded-lg bg-emerald-600 text-white text-[10px] font-bold uppercase hover:bg-emerald-500 transition-colors shadow-lg shadow-emerald-900/20"
                                    >
                                        Apply Routing
                                    </button>
                                </div>
                            </div>
                        )}

                        {view === 3 && (
                            <div className="space-y-6 animate-in zoom-in-95 duration-200">
                                <div className="p-4 rounded-xl bg-blue-500/5 border border-blue-500/10 flex flex-col items-center text-center">
                                    <div className="p-3 bg-blue-500/20 rounded-full mb-3 text-blue-400">
                                        <Link size={24} />
                                    </div>
                                    <h2 className="text-sm font-bold text-white">Remote Video Stream</h2>
                                    <span className="text-[10px] text-zinc-500 font-mono mt-1 uppercase">Enter source URL</span>
                                </div>

                                <div className="space-y-2">
                                    <label className="text-[10px] font-bold text-zinc-400 uppercase tracking-widest px-1">
                                        Stream Address
                                    </label>
                                    <input
                                        type="url"
                                        value={streamUrl}
                                        onChange={(e) => setStreamUrl(e.target.value)}
                                        placeholder="rtsp://... or http://..."
                                        className="w-full bg-black/50 border border-white/10 rounded-lg p-3 text-sm text-zinc-200 focus:outline-none focus:border-blue-500/50 focus:ring-1 focus:ring-blue-500/50 transition-all font-mono placeholder:text-zinc-700"
                                        autoFocus
                                    />
                                </div>

                                <div className="flex gap-2 pt-4">
                                    <button
                                        onClick={() => setView(0)}
                                        className="flex-1 px-4 py-2 rounded-lg bg-zinc-900 border border-white/5 text-zinc-400 text-[10px] font-bold uppercase hover:bg-zinc-800 transition-colors"
                                    >
                                        Cancel
                                    </button>
                                    <button
                                        onClick={confirmStreamUrl}
                                        disabled={!streamUrl.trim()}
                                        className="flex-1 px-4 py-2 rounded-lg bg-blue-600 text-white text-[10px] font-bold uppercase hover:bg-blue-500 transition-colors shadow-lg shadow-blue-900/20 disabled:opacity-50 disabled:cursor-not-allowed"
                                    >
                                        Connect Stream
                                    </button>
                                </div>
                            </div>
                        )}
                    </div>

                    <div className="p-3 bg-black/40 border-t border-white/5 flex justify-center italic text-[8px] text-zinc-600 font-mono tracking-tighter">
                        DEJAVIS-CORE // AUDIO ENGINE v3.0
                    </div>

                </Dialog.Content>
            </Dialog.Portal>
        </Dialog.Root>
    );
};

const InputOption = ({ icon, label, sublabel, onClick, isCurrent }: any) => (
    <button onClick={onClick} className={cn("group w-full flex items-center justify-between p-3 rounded-lg bg-white/[0.02] border border-white/5 transition-all text-left outline-none hover:bg-zinc-900", isCurrent && "border-emerald-500/50 bg-emerald-500/5")}>
        <div className="flex items-center gap-3">
            <div className={cn("p-2 rounded-md bg-zinc-950 border border-white/5 group-hover:text-emerald-400", isCurrent && "text-emerald-500")}>{icon}</div>
            <div>
                <div className="text-xs font-bold text-zinc-200 leading-none">{label}</div>
                <div className="text-[9px] text-zinc-600 font-mono mt-1 uppercase">{sublabel}</div>
            </div>
        </div>
        <ChevronRight size={14} className="text-zinc-800 group-hover:text-zinc-400 transition-all" />
    </button>
);

const InfoCard = ({ label, value, icon }: { label: string, value: string, icon: any }) => (
    <div className="p-3 bg-white/[0.03] border border-white/5 rounded-lg">
        <div className="flex items-center gap-2 text-zinc-500 mb-1">
            {icon}
            <span className="text-[8px] uppercase font-black tracking-widest">{label}</span>
        </div>
        <div className="text-xs font-mono text-zinc-200">{value}</div>
    </div>
);