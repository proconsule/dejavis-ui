"use client";

import { useEffect, useState } from "react";
import * as Dialog from "@radix-ui/react-dialog";
import {
    Cable,
    X,
    ChevronRight,
    Activity,
    Music,
    ArrowLeft,
    Check,
    Settings2,
    Info, Volume2,
    ChevronDown, AudioWaveform
} from "lucide-react";
import type { AudioDevConfig, AudioDevice } from "@/components/audio/deviceselector.tsx";
import { cn } from "@/lib/utils.ts";
import { useWS } from "@/WebSocketContext.tsx";
import {Marquee} from "@/components/ui/marquee.tsx";

const COMMON_SAMPLE_RATES = [44100, 48000, 88200, 96000, 176400, 192000];

interface OutputSelectorProps {
    idx: number;
}

export const OutputSelectorDialog = ({ idx }: OutputSelectorProps) => {
    const [view, setView] = useState<0 | 1 | 2>(0);
    const [isOpen, setIsOpen] = useState(false);
    const [selectedDevice, setSelectedDevice] = useState<AudioDevice | null>(null);
    const [configuredDevices, setConfiguredDevices] = useState<AudioDevConfig | null>(null);
    const { lastJsonMessage, sendMessage } = useWS();

    const [currentOutput, setcurrentOutput] = useState<string>("");
    const [dev_id,setdev_id] = useState<number>(-1);

    const [hardwareData, setHardwareData] = useState<{
        inputs: AudioDevice[],
        outputs: AudioDevice[]
    }>({ inputs: [], outputs: [] });


    //const currentOutput = hardwareData.outputs[idx]?.name || "";
    //const dev_id = hardwareData.outputs[idx]?.id || 0;


    useEffect(() => {
        if (!lastJsonMessage) return;
        if (lastJsonMessage.msgid === 45) {
            setHardwareData({
                inputs: lastJsonMessage.inputs || [],
                outputs: lastJsonMessage.outputs || []
            });
        }
        if (lastJsonMessage.msgid === 1) {
            if(idx === 0) {
                setcurrentOutput(lastJsonMessage.audio?.mixer?.master_output?.audio_dev_name);
                setdev_id(lastJsonMessage.audio?.mixer?.master_output?.audio_device_id);
            }
            if(idx === 1) {
                setcurrentOutput(lastJsonMessage.audio?.mixer?.aux_output?.audio_dev_name);
                setdev_id(lastJsonMessage.audio?.mixer?.aux_output?.audio_device_id);
            }
        }
    }, [lastJsonMessage]);

    const handleSelectInternal = (type: 0 | 1) => {
        sendMessage({ msgid: 7000, type: type, outputidx: idx, devid: -1 });
        setIsOpen(false);
    };

    const openDeviceDetails = (dev: AudioDevice) => {
        setSelectedDevice(dev);
        setConfiguredDevices({
            id: idx,
            deviceId: dev.id,
            sampleRate: dev.defaultSampleRate,
            channels: dev.maxOut // Per gli output usiamo maxOut
        });
        setView(2);
    };

    const confirmHardwareSelection = () => {
        if (!selectedDevice || !configuredDevices) return;

        sendMessage({
            msgid: 7000,
            type: 1,
            outputidx: idx, // Nota: corretto in outputidx per coerenza col componente
            devid: configuredDevices.deviceId,
            samplerate: configuredDevices.sampleRate,
            channels: configuredDevices.channels
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
            }, 200);
        }
    };

    return (
        <Dialog.Root open={isOpen} onOpenChange={handleOpenChange}>
            <Dialog.Trigger asChild>
                <button className="flex items-center gap-2 px-3 py-1.5 rounded bg-zinc-800 border border-white/10 text-zinc-400 hover:text-emerald-400 hover:border-emerald-500/50 transition-all active:scale-95 text-[9px] font-bold uppercase tracking-widest group">
                    <Cable size={12} className={cn("transition-colors", currentOutput ? "text-emerald-500" : "group-hover:text-emerald-400")} />
                    <Marquee>{currentOutput || "Unassigned"}</Marquee>
                </button>
            </Dialog.Trigger>

            <Dialog.Portal>
                <Dialog.Overlay className="fixed inset-0 bg-black/60 backdrop-blur-md z-[100] animate-in fade-in duration-200" />
                <Dialog.Content className="fixed top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[400px] max-h-[85vh] overflow-hidden flex flex-col bg-zinc-950 border border-white/10 shadow-2xl z-[101] rounded-xl focus:outline-none">

                    <div className="p-4 border-b border-white/5 flex justify-between items-center bg-zinc-900/50">
                        <div className="flex items-center gap-3">
                            {view !== 0 && (
                                <button
                                    onClick={() => setView((prev) => (prev - 1) as any)}
                                    className="p-1.5 hover:bg-white/5 rounded-md text-zinc-400 hover:text-white transition-colors"
                                >
                                    <ArrowLeft size={16} />
                                </button>
                            )}
                            <div>
                                <Dialog.Title className="text-zinc-100 font-bold uppercase tracking-tight text-xs">
                                    {view === 0 && "Ch. Routing"}
                                    {view === 1 && "Hardware Output"}
                                    {view === 2 && "Device Configuration"}
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
                                        <OutputOption icon={<AudioWaveform size={16} />} label="Dummy Clock" sublabel="dummy 48khz internal clock" onClick={() => handleSelectInternal(0)} />
                                    </div>
                                </section>
                                <section className="pt-2 border-t border-white/5">
                                    <h3 className="text-[9px] font-black text-emerald-500 uppercase tracking-widest mb-3 flex items-center gap-2">
                                        <Volume2 size={11} /> External
                                    </h3>
                                    <OutputOption icon={<Cable size={16} />} label="Physical Hardware" sublabel={`${hardwareData.outputs.length} detected`} onClick={() => setView(1)} />
                                </section>
                            </div>
                        )}

                        {view === 1 && (
                            <div className="space-y-2 animate-in fade-in slide-in-from-right-4 duration-200">
                                {hardwareData.outputs.map((dev) => (
                                    <OutputOption
                                        key={`${dev.api}-${dev.id}`}
                                        icon={<Volume2 size={14} />}
                                        label={dev.name}
                                        sublabel={`${dev.api.toUpperCase()}`}
                                        isCurrent={dev_id == dev.id}
                                        onClick={() => openDeviceDetails(dev)}
                                    />
                                ))}
                            </div>
                        )}

                        {view === 2 && selectedDevice && configuredDevices && (
                            <div className="space-y-6 animate-in zoom-in-95 duration-200">
                                <div className="p-4 rounded-xl bg-emerald-500/5 border border-emerald-500/10 flex flex-col items-center text-center">
                                    <div className="p-3 bg-emerald-500/20 rounded-full mb-3 text-emerald-400">
                                        <Settings2 size={24} />
                                    </div>
                                    <h2 className="text-sm font-bold text-white">{selectedDevice.name}</h2>
                                    <span className="text-[10px] text-zinc-500 font-mono mt-1 uppercase">{selectedDevice.api} Driver</span>
                                </div>

                                <div className="grid grid-cols-2 gap-2">
                                    {/* SELECT CHANNELS */}
                                    <SelectCard
                                        label="Channels"
                                        icon={<Cable size={12}/>}
                                        value={configuredDevices.channels}
                                        onChange={(val) => setConfiguredDevices({...configuredDevices, channels: parseInt(val)})}
                                        options={Array.from({ length: selectedDevice.maxOut }, (_, i) => ({
                                            label: `${i + 1} OUT`,
                                            value: (i + 1).toString()
                                        }))}
                                    />

                                    {/* SELECT SAMPLE RATE */}
                                    <SelectCard
                                        label="Sample Rate"
                                        icon={<Activity size={12}/>}
                                        value={configuredDevices.sampleRate.toString()}
                                        onChange={(val) => setConfiguredDevices({...configuredDevices, sampleRate: parseInt(val)})}
                                        options={COMMON_SAMPLE_RATES.map(rate => ({
                                            label: `${rate / 1000} kHz`,
                                            value: rate.toString()
                                        }))}
                                    />

                                    <InfoCard label="Device ID" value={selectedDevice.id.toString()} icon={<Info size={12}/>} />
                                    <InfoCard label="Preferred" value={selectedDevice.isDefaultOutput ? "YES" : "NO"} icon={<Check size={12}/>} />
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
                    </div>
                </Dialog.Content>
            </Dialog.Portal>
        </Dialog.Root>
    );
};

/* Sotto-componenti UI */

const SelectCard = ({ label, icon, value, onChange, options }: {
    label: string,
    icon: any,
    value: string | number,
    onChange: (val: string) => void,
    options: {label: string, value: string}[]
}) => (
    <div className="p-3 bg-white/[0.03] border border-white/5 rounded-lg group hover:border-emerald-500/30 transition-colors relative">
        <div className="flex items-center gap-2 text-zinc-500 mb-1">
            {icon}
            <span className="text-[8px] uppercase font-black tracking-widest">{label}</span>
        </div>
        <div className="relative">
            <select
                value={value}
                onChange={(e) => onChange(e.target.value)}
                className="w-full bg-transparent text-xs font-mono text-zinc-200 appearance-none outline-none cursor-pointer pr-4"
            >
                {options.map(opt => (
                    <option key={opt.value} value={opt.value} className="bg-zinc-900 text-zinc-200">
                        {opt.label}
                    </option>
                ))}
            </select>
            <ChevronDown size={10} className="absolute right-0 top-1/2 -translate-y-1/2 text-zinc-600 pointer-events-none group-hover:text-emerald-400 transition-colors" />
        </div>
    </div>
);

const OutputOption = ({ icon, label, sublabel, onClick, isCurrent }: any) => (
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