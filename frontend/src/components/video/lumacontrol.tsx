import { Sun, Contrast } from 'lucide-react';
import { cn } from "@/lib/utils";
import ControlSlider from "@/components/video/ControlSlider.tsx";

// Definiamo l'interfaccia per i dati del lumakey
interface LumaKeyData {
    enabled: number;
    lower: number;    // Soglia minima di luminosità
    upper: number;    // Soglia massima di luminosità
    softness: number; // Morbidezza dei bordi
    invert: number;   // 0.0 o 1.0 per invertire la maschera
}

interface LumaKeyControlProps {
    data: LumaKeyData;
    onUpdate: (update: Partial<LumaKeyData>) => void;
}

export const LumaKeyControl = ({
                                   data,
                                   onUpdate
                               }: LumaKeyControlProps) => {

    const isLumaEnabled = data.enabled === 1.0;
    const isInverted = data.invert === 1.0;

    return (
        <div className="flex flex-col gap-4 animate-in fade-in duration-200">
            {/* Header con Switch Abilitazione */}
            <div className="flex justify-between items-center bg-black/20 p-3 rounded border border-white/5">
                <div className="flex items-center gap-3">
                    <span className="text-[10px] font-bold text-slate-400 uppercase tracking-wider">
                        {isLumaEnabled ? 'Luma Active' : 'Luma Off'}
                    </span>

                    <label className="relative inline-flex items-center cursor-pointer">
                        <input
                            type="checkbox"
                            className="sr-only peer"
                            checked={isLumaEnabled}
                            onChange={(e) => onUpdate({ enabled: e.target.checked ? 1.0 : 0.0 })}
                        />
                        <div className="w-11 h-6 bg-slate-700 peer-focus:outline-none rounded-full peer
                            peer-checked:after:translate-x-full peer-checked:after:border-white
                            after:content-[''] after:absolute after:top-[2px] after:left-[2px]
                            after:bg-white after:border-gray-300 after:border after:rounded-full
                            after:h-5 after:w-5 after:transition-all peer-checked:bg-amber-500
                            shadow-inner transition-colors">
                        </div>
                    </label>
                </div>

                <Sun size={16} className={cn("transition-colors", isLumaEnabled ? "text-amber-400" : "text-slate-600")} />
            </div>

            {/* Range Selectors: Lower e Upper */}
            <div className="grid grid-cols-2 gap-3">
                <ControlSlider
                    label="Lower Bound"
                    value={data.lower}
                    min={0}
                    max={1}
                    step={0.001}
                    onChange={(v) => onUpdate({ lower: v })}
                />
                <ControlSlider
                    label="Upper Bound"
                    value={data.upper}
                    min={0}
                    max={1}
                    step={0.001}
                    onChange={(v) => onUpdate({ upper: v })}
                />
            </div>

            {/* Softness e Invert Toggle */}
            <div className="grid grid-cols-2 gap-4 bg-black/10 p-3 rounded border border-white/5 items-end">
                <ControlSlider
                    label="Softness"
                    value={data.softness}
                    min={0}
                    max={1}
                    step={0.001}
                    onChange={(v) => onUpdate({ softness: v })}
                />

                {/* Toggle per Invert */}
                <div className="flex flex-col gap-2">
                    <span className="text-[10px] font-bold text-slate-500 uppercase tracking-tight px-0.5">Mask Invert</span>
                    <button
                        onClick={() => onUpdate({ invert: isInverted ? 0.0 : 1.0 })}
                        className={cn(
                            "flex items-center justify-center gap-2 h-8 rounded-lg border transition-all text-[10px] font-bold uppercase",
                            isInverted
                                ? "bg-white/10 border-white/20 text-white"
                                : "bg-black/20 border-white/5 text-slate-500 hover:border-white/10"
                        )}
                    >
                        <Contrast size={14} />
                        {isInverted ? "Inverted" : "Normal"}
                    </button>
                </div>
            </div>

            {/* Helper text per l'utente */}
            <div className="px-1 text-[8px] text-zinc-600 font-mono leading-tight">
                LUMAKEY FILTERS PIXELS BASED ON LUMINANCE (Y) VALUES BETWEEN LOWER AND UPPER BOUNDS.
            </div>
        </div>
    );
};