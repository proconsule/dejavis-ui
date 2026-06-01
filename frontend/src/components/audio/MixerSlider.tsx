import * as React from "react"
import * as SliderPrimitive from "@radix-ui/react-slider"
import { cn } from "@/lib/utils"


interface MixerSliderProps extends React.ComponentPropsWithoutRef<typeof SliderPrimitive.Root> {
    mixerout_idx?: number;
    value?: number[];
}

const MixerSlider = React.forwardRef<
    React.ElementRef<typeof SliderPrimitive.Root>,
    MixerSliderProps
>(({ className, orientation = "vertical", mixerout_idx = -1, value, onValueChange, ...props }, ref) => {

    const getDbLabel = (linearGain: number) => {
        const audioGain = Math.pow(linearGain, 3);

        if (audioGain <= 0.0001) return "-∞ dB";
        const db = 20 * Math.log10(audioGain);

        if (db > -0.05) return "0.0 dB";
        return `${db.toFixed(1)} dB`;
    };

    const getKnotColor = () => {
        switch (mixerout_idx) {
            case 0:  return "border-green-500 shadow-[0_0_10px_rgba(34,197,94,0.4)] bg-green-950";
            case 1:  return "border-red-500 shadow-[0_0_10px_rgba(239,68,68,0.4)] bg-red-950";
            default: return "border-zinc-600 shadow-none bg-zinc-900";
        }
    };

    const getLineColor = () => {
        switch (mixerout_idx) {
            case 0:  return "bg-green-400 shadow-[0_0_5px_#22c55e]";
            case 1:  return "bg-red-400 shadow-[0_0_5px_#ef4444]";
            default: return "bg-zinc-500";
        }
    };

    const internalValue = value ? [value[0] * 100] : [0];

    const handleValueChange = (vals: number[]) => {
        if (onValueChange) {
            const normalized = Math.round((vals[0] / 100) * 10000) / 10000;
            onValueChange([normalized]);
        }
    };

    return (
        <div className="flex flex-col items-center gap-3 h-full">
            <SliderPrimitive.Root
                ref={ref}
                orientation={orientation}
                value={internalValue}
                onValueChange={handleValueChange}
                max={100}
                step={0.1}
                className={cn(
                    "relative flex touch-none select-none items-center justify-center",
                    orientation === "vertical" ? "h-full w-6 flex-col" : "w-full h-6",
                    className
                )}
                {...props}
            >
                <SliderPrimitive.Track
                    className={cn(
                        "relative grow overflow-hidden bg-zinc-800/50 border border-white/5",
                        orientation === "vertical" ? "w-[2px] h-full" : "h-[2px] w-full"
                    )}
                >
                    <SliderPrimitive.Range className="absolute bg-zinc-700/50" />
                </SliderPrimitive.Track>

                <SliderPrimitive.Thumb
                    className={cn(
                        "block transition-none focus-visible:outline-none disabled:pointer-events-none disabled:opacity-50",
                        "border-2 active:scale-95 cursor-ns-resize relative h-4 w-8 rounded-sm",
                        getKnotColor()
                    )}
                >
                    <div className={cn(
                        "absolute top-1/2 left-0 w-full h-[1.5px] -translate-y-1/2 transition-colors",
                        getLineColor()
                    )} />
                </SliderPrimitive.Thumb>
            </SliderPrimitive.Root>

            {/* Label dei Decibel basata sulla posizione */}
            <div className="min-w-[45px] text-center">
                <span className="text-[10px] font-mono font-bold text-zinc-400 bg-black/60 px-1.5 py-0.5 rounded border border-white/10 shadow-sm">
                    {getDbLabel(value ? value[0] : 0)}
                </span>
            </div>
        </div>
    );
})

MixerSlider.displayName = "MixerSlider"

export { MixerSlider }


interface PanSliderProps extends React.ComponentPropsWithoutRef<typeof SliderPrimitive.Root> {
    onReset?: () => void;
}

const PanSlider = React.forwardRef<
    React.ElementRef<typeof SliderPrimitive.Root>,
    PanSliderProps
>(({ className, value, defaultValue, onReset, ...props }, ref) => {
    const currentValue = value ? value[0] : (defaultValue ? defaultValue[0] : 0);

    const isRight = currentValue > 0;
    const width = Math.abs(currentValue) / 2; // Poiché il centro è al 50%
    const left = isRight ? "50%" : `${50 - (Math.abs(currentValue) / 2)}%`;

    return (
        <div className="relative w-full group" onDoubleClick={onReset} title="Double click to center">
            <SliderPrimitive.Root
                ref={ref}
                value={value}
                defaultValue={defaultValue}
                className={cn(
                    "relative flex w-full touch-none select-none items-center h-4",
                    className
                )}
                {...props}
            >
                {/* Binario di fondo */}
                <SliderPrimitive.Track className="relative h-[2px] w-full grow bg-zinc-800 border-x border-white/10">
                    {/* Tacca centrale di riferimento */}
                    <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 w-[1px] h-3 bg-zinc-600 z-10" />

                    {/* Barra dinamica che parte dal centro */}
                    <div
                        className="absolute h-full bg-zinc-400 transition-none"
                        style={{
                            left: left,
                            width: `${width}%`,
                            backgroundColor: 'rgba(255,255,255,0.4)'
                        }}
                    />
                </SliderPrimitive.Track>

                {/* Knot del Pan (più piccolo e tecnico) */}
                <SliderPrimitive.Thumb className="block h-3 w-3 rounded-full border-2 border-zinc-400 bg-zinc-950 ring-offset-background transition-colors focus-visible:outline-none disabled:pointer-events-none disabled:opacity-50 cursor-ew-resize hover:border-white" />
            </SliderPrimitive.Root>
        </div>
    );
})

PanSlider.displayName = "PanSlider"

export { PanSlider }