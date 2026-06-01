// Definiamo un'interfaccia per le Props per sfruttare appieno TypeScript
interface ControlSliderProps {
    label: string;
    value: number | undefined;
    min: number;
    max: number;
    step: number;
    onChange: (value: number) => void;
}

export const ControlSlider = ({
                                  label,
                                  value,
                                  min,
                                  max,
                                  step,
                                  onChange
                              }: ControlSliderProps) => {

    // Gestione del fallback per evitare errori con .toFixed()
    const safeValue = value ?? 0;

    return (
        <div className="flex flex-col gap-1.5">
            <div className="flex justify-between items-center px-0.5">
                <span className="text-[10px] font-bold text-slate-400 uppercase tracking-tight">
                    {label}
                </span>
                <span className="text-[10px] font-mono text-emerald-400 bg-black/30 px-1.5 rounded border border-white/5">
                    {safeValue.toFixed(step < 0.1 ? 3 : 2)}
                </span>
            </div>
            <input
                type="range"
                min={min}
                max={max}
                step={step}
                value={safeValue}
                onChange={(e) => onChange(parseFloat(e.target.value))}
                className="w-full h-1.5 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-emerald-500 hover:accent-emerald-400 transition-all"
            />
        </div>
    );
};

// Facoltativo: esportazione predefinita
export default ControlSlider;