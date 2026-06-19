
import {useState, memo, useEffect} from 'react';
import { Plus, Power, GripVertical, X } from 'lucide-react';
import { cn } from "@/lib/utils";
import {useWS} from "@/WebSocketContext.tsx";

interface FxEffect {
    type: number;
    enabled?: boolean;
    // I parametri sono piatti nell'oggetto, non in un sotto-oggetto 'params'
    [key: string]: any;
}

interface FxBankProps {
    channelIdx: number;
    // In futuro qui passerai i dati che arrivano dal WebSocket
    fxData?: {
        active: boolean;
        effects: FxEffect[];
    };
}

export const FxBank = memo(function FxBank({ channelIdx, fxData }: FxBankProps) {
    const [selectedFxIdx, setSelectedFxIdx] = useState<number | null>(null);
    const [showAddMenu, setShowAddMenu] = useState(false);
    const [localParams, setLocalParams] = useState<Record<string, any>>({});

    const { lastJsonMessage,sendMessage } = useWS();
    const audio_dsp = lastJsonMessage?.audio?.mixer?.inputs[channelIdx]?.audio_dsp;

    const effectsChain = audio_dsp?.effects || fxData?.effects || [
        {
            type: 1,
            enabled: true,
            limit: -1.0, attackMs: 5, releaseMs: 100, autoLevel: 1, asc: 0
        },
    ];

    useEffect(() => {
        if (selectedFxIdx !== null && effectsChain[selectedFxIdx]) {
            const fx = effectsChain[selectedFxIdx];
            setLocalParams({ ...fx });
        }
    }, [selectedFxIdx, effectsChain]);


    const isBankEnabled = audio_dsp?.active ?? fxData?.active ?? true;


    const PARAM_CONFIG: Record<number, { label: string, key: string, min: number, max: number, step: number }[]> = {
        1: [
            { label: 'Limit', key: 'limit', min: -10, max: 1, step: 0.1 },
            { label: 'Attack', key: 'attackMs', min: 0, max: 100, step: 1 },
            { label: 'Release', key: 'releaseMs', min: 0, max: 1000, step: 1 },
            { label: 'Auto Level', key: 'autoLevel', min: 0, max: 1, step: 1 },
        ],
        2: [
            { label: 'Delay', key: 'delayMs', min: 0, max: 2000, step: 1 },
            { label: 'Decay', key: 'decay', min: 0, max: 1, step: 0.01 },
            { label: 'Out Amp', key: 'outAmplitude', min: 0, max: 1, step: 0.01 },
        ]
    };

    const selectedFx = selectedFxIdx !== null ? effectsChain[selectedFxIdx] : null;

    const handleAddEffect = (type: number) => {

        sendMessage({msgid: 3041,idx:channelIdx,fxtype: type});

        setShowAddMenu(false);
        // Qui andrà la chiamata al server via WebSocket
    };

  const getFxTypeName = (type: number): string => {
        const fxNames: Record<number, string> = {
            1: "Limiter",
            2: "Echo",
            3: "Tempo",
        };

        return fxNames[type] || "Unknown FX";
    };

    return (
        <div className="flex flex-col w-28 shrink-0 bg-zinc-900/90 border-x border-b border-white/5 border-t-4 border-t-violet-600 transition-all shadow-xl">

            {/* HEADER: Bank Status */}
            <div className="p-2 bg-black/40 flex flex-col items-center gap-2 border-b border-white/5 relative">
                <span className="text-[10px] font-mono text-zinc-500 uppercase tracking-widest">FX BANK CH {channelIdx.toString().padStart(2, '0')}</span>

                <div className="flex gap-1 w-full">
                    <button
                        className={cn(
                            "flex-1 flex items-center justify-center py-1 rounded-sm border transition-all",
                            isBankEnabled
                                ? "bg-violet-600 border-violet-400 text-white shadow-[0_0_10px_rgba(139,92,246,0.5)]"
                                : "bg-zinc-800 border-white/10 text-zinc-500"
                        )}
                    >
                        <Power size={12} className="mr-1" />
                        <span className="text-[9px] font-bold">BANK</span>
                    </button>
                    <div className="relative">
                        <button
                            onClick={() => setShowAddMenu(!showAddMenu)}
                            className="px-2 py-1 bg-zinc-800 border border-white/10 text-zinc-400 hover:bg-zinc-700 transition-colors"
                        >
                            <Plus size={12} />
                        </button>

                        {/* ADD EFFECT MENU */}
                        {showAddMenu && (
                            <div className="absolute right-0 top-full mt-1 z-50 w-24 bg-zinc-800 border border-white/10 rounded shadow-2xl flex flex-col p-1 gap-1">
                                <button
                                    onClick={() => handleAddEffect(1)}
                                    className="text-left px-2 py-1 text-[9px] text-zinc-300 hover:bg-violet-600 hover:text-white rounded transition-colors"
                                >
                                    + Limiter
                                </button>
                                <button
                                    onClick={() => handleAddEffect(2)}
                                    className="text-left px-2 py-1 text-[9px] text-zinc-300 hover:bg-violet-600 hover:text-white rounded transition-colors"
                                >
                                    + Echo
                                </button>
                                <button
                                    onClick={() => handleAddEffect(3)}
                                    className="text-left px-2 py-1 text-[9px] text-zinc-300 hover:bg-violet-600 hover:text-white rounded transition-colors"
                                >
                                    + Tempo
                                </button>
                            </div>
                        )}
                    </div>
                </div>
            </div>

            {/* FX CHAIN LIST */}
            <div className="p-2 flex-1 overflow-y-auto bg-black/20 space-y-1 custom-scrollbar">
            <div className="text-[7px] font-mono text-zinc-600 uppercase text-center mb-2 tracking-tighter">Effect Chain</div>

                {effectsChain.map((fx: FxEffect, idx: number) => (
                    <div
                        key={idx}
                        onClick={() => setSelectedFxIdx(idx)}
                        className={cn(
                            "group relative flex items-center justify-between p-1.5 rounded border cursor-pointer transition-all",
                            "text-[10px] font-medium",
                            selectedFxIdx === idx
                                ? "bg-zinc-700 border-violet-500 text-white"
                                : "bg-zinc-800/50 border-white/5 text-zinc-400 hover:border-white/20"
                        )}
                    >
                        <div className="flex items-center gap-2 overflow-hidden">
                            <div className={cn(
                                "w-1.5 h-1.5 rounded-full",
                                fx.enabled ? "bg-emerald-500 shadow-[0_0_4px_#10b981]" : "bg-zinc-600"
                            )} />
                            <span className="truncate">{getFxTypeName(fx.type)}</span>
                        </div>
                        <div className="flex items-center gap-1">
                            <button
                                onClick={(e) => { e.stopPropagation(); /* Toggle Enable placeholder */ }}
                                className="p-0.5 hover:text-white transition-colors"
                            >
                                <Power size={10} />
                            </button>
                            <button
                                onClick={(e) => {
                                    e.stopPropagation();
                                    sendMessage({msgid: 3042, idx: channelIdx, slot: idx});
                                    setSelectedFxIdx(null);
                                }}
                                className="p-0.5 text-zinc-600 hover:text-red-400 transition-colors"
                            >
                                <X size={10} />
                            </button>
                        </div>
                    </div>
                ))}
            </div>

            {/* SETTINGS PANEL */}
            <div className={cn(
                "p-2 bg-black/60 border-t border-white/5 transition-all duration-300",
                selectedFx ? "h-auto opacity-100 block" : "h-0 opacity-0 overflow-hidden pointer-events-none"
            )}>
                {selectedFx && (
                    <div className="flex flex-col gap-3">
                        <div className="flex items-center justify-between mb-1">
                            <span className="text-[9px] font-bold text-violet-400 uppercase">{selectedFx.type}</span>
                            <GripVertical size={10} className="text-zinc-700" />
                        </div>

                        <div className="space-y-3">
                            {PARAM_CONFIG[selectedFx?.type]?.map((config) => {
                                const value = localParams[config.key] ?? selectedFx?.[config.key] ?? 0;

                                return (
                                    <div key={config.label} className="space-y-1">
                                        <div className="flex justify-between text-[8px] font-mono text-zinc-500">
                                            <span>{config.label}</span>
                                            <span>{typeof value === 'number' ? value.toFixed(2) : value}</span>
                                        </div>
                                        <input
                                            type="range"
                                            min={config.min}
                                            max={config.max}
                                            step={config.step}
                                            value={value}
                                            onChange={(e) => {
                                                const newValue = parseFloat(e.target.value);
                                                // Aggiornamento solo visivo e locale per mantenere la fluidità
                                                setLocalParams(prev => ({ ...prev, [config.key]: newValue }));
                                            }}
                                            onMouseUp={() => {
                                                // Invia il comando solo quando l'utente rilascia il mouse
                                                const serverParams: Record<string, number> = {};
                                                PARAM_CONFIG[selectedFx!.type].forEach(p => {
                                                    const val = localParams[p.key] ?? (selectedFx![p.key] ?? 0);
                                                    serverParams[p.key] = Number(val.toFixed(2));
                                                });

                                                sendMessage({
                                                    msgid: 3044,
                                                    idx: channelIdx,
                                                    slot: selectedFxIdx,
                                                    type: selectedFx?.type,
                                                    params: serverParams
                                                });
                                            }}
                                            className="w-full h-1 bg-zinc-700 rounded-lg appearance-none cursor-pointer accent-violet-500"
                                        />
                                    </div>
                                );
                            })}
                        </div>
                    </div>
                )}
            </div>
        </div>
    );
});