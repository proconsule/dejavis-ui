import { Pipette } from 'lucide-react';
import { cn } from "@/lib/utils";
import ControlSlider from "@/components/video/ControlSlider.tsx"; // Assumendo tu abbia una utility per le classi

// Definiamo l'interfaccia per i dati del chromakey
interface ChromaKeyData {
    enabled: number;
    v0: number; // R
    v1: number; // G
    v2: number; // B
    threshold: number;
    softness: number;
    spill: number;
}

interface ChromaKeyControlProps {
    data: ChromaKeyData;
    isPicking: boolean;
    setIsPicking: (picking: boolean) => void;
    onUpdate: (update: Partial<ChromaKeyData>) => void;
}

export const ChromaKeyControl = ({
                                     data,
                                     isPicking,
                                     setIsPicking,
                                     onUpdate
                                 }: ChromaKeyControlProps) => {

    const isChromaEnabled = data.enabled === 1.0;

    return (
        <div className="flex flex-col gap-4 animate-in fade-in duration-200">
            {/* Header con Switch e Pipetta */}
            <div className="flex justify-between items-center bg-black/20 p-3 rounded border border-slate-700/50">
                <div className="flex items-center gap-3">
                    <span className="text-[10px] font-bold text-slate-400 uppercase tracking-wider">
                        {isChromaEnabled ? 'Chroma Active' : 'Chroma Off'}
                    </span>

                    <label className="relative inline-flex items-center cursor-pointer">
                        <input
                            type="checkbox"
                            className="sr-only peer"
                            checked={isChromaEnabled}
                            onChange={(e) => onUpdate({ enabled: e.target.checked ? 1.0 : 0.0 })}
                        />
                        <div className="w-11 h-6 bg-slate-700 peer-focus:outline-none rounded-full peer
                            peer-checked:after:translate-x-full peer-checked:after:border-white
                            after:content-[''] after:absolute after:top-[2px] after:left-[2px]
                            after:bg-white after:border-gray-300 after:border after:rounded-full
                            after:h-5 after:w-5 after:transition-all peer-checked:bg-emerald-500
                            shadow-inner transition-colors">
                        </div>
                    </label>
                </div>

                <button
                    onClick={() => setIsPicking(!isPicking)}
                    className={cn(
                        "p-2 rounded-lg transition-all",
                        isPicking ? "bg-cyan-500 text-white animate-pulse" : "bg-slate-700 text-cyan-400 hover:bg-slate-600"
                    )}
                >
                    <Pipette size={18} />
                </button>
            </div>

            {/* RGB Selectors */}
            <div className="grid grid-cols-3 gap-3">
                <ControlSlider label="Key R" value={data.v0} min={0} max={1} step={0.01} onChange={(v) => onUpdate({ v0: v })} />
                <ControlSlider label="Key G" value={data.v1} min={0} max={1} step={0.01} onChange={(v) => onUpdate({ v1: v })} />
                <ControlSlider label="Key B" value={data.v2} min={0} max={1} step={0.01} onChange={(v) => onUpdate({ v2: v })} />
            </div>

            {/* Threshold, Softness, Spill */}
            <div className="grid grid-cols-3 gap-4 bg-black/10 p-2 rounded border border-white/5">
                <ControlSlider label="Threshold" value={data.threshold} min={0} max={1} step={0.01} onChange={(v) => onUpdate({ threshold: v })} />
                <ControlSlider label="Softness" value={data.softness} min={0} max={1} step={0.01} onChange={(v) => onUpdate({ softness: v })} />
                <ControlSlider label="Spill" value={data.spill} min={0} max={1} step={0.01} onChange={(v) => onUpdate({ spill: v })} />
            </div>
        </div>
    );
};