interface AudioBufferVerticalProps {
    fill: number;
    max: number;
    sampleRate?: number;
    className?: string;
}

export function AudioBufferVertical({
                                        fill,
                                        max,
                                        sampleRate = 44100,
                                        className
                                    }: AudioBufferVerticalProps) {
    const channels = 1;
    const percentage = Math.min((fill / max) * 100, 100);

    const ms = ((fill / channels) / sampleRate) * 1000;

    return (
        <div className={`flex h-full flex-col items-center py-1.5 font-sans ${className}`}>

            <div className="flex flex-col items-center mb-1.5 leading-none px-1">
                <span className="text-[10px] font-white text-white">
                    {ms.toFixed(1)}
                </span>
                <span className="text-[6px] text-white/60 uppercase font-bold tracking-tighter">ms</span>
            </div>

            <div className="relative w-2.5 flex-1 bg-zinc-200 border border-black/10 overflow-hidden">
                <div
                    className="absolute bottom-0 w-full bg-black transition-[height] duration-75 ease-linear"
                    style={{ height: `${percentage}%` }}
                />

                <div className="absolute inset-0 flex flex-col justify-between pointer-events-none">
                    <div className="border-t border-red-600 w-full h-[1px]" /> {/* 100% */}
                    <div className="border-t border-black/20 w-full h-[50%] border-b border-black/20" /> {/* 50% */}
                    <div className="w-full" /> {/* 0% */}
                </div>
            </div>

            <div className="mt-1.5 flex flex-col items-center leading-none">
                <span className="text-[9px] font-bold text-white">
                    {Math.round(percentage)}%
                </span>
            </div>
        </div>
    );
}