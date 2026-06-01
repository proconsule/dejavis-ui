import { useEffect, useState } from 'react';

export const VUMeter = () => {
    const [levels, setLevels] = useState({ l: 0, r: 0 });

    useEffect(() => {
        const handleLevels = (e: any) => setLevels(e.detail);
        window.addEventListener('audio-monitor-levels', handleLevels);
        return () => window.removeEventListener('audio-monitor-levels', handleLevels);
    }, []);

    const renderBar = (value: number) => {
        const percentage = Math.min(value * 250, 100);
        const color = value > 0.9 ? '#ff4d4d' : value > 0.7 ? '#ffd633' : '#00ff88';

        return (
            <div className="flex-1 h-1.5 bg-gray-800 rounded-full overflow-hidden">
                <div
                    className="h-full transition-all duration-75 ease-out"
                    style={{
                        width: `${percentage}%`,
                        backgroundColor: color,
                        boxShadow: value > 0.8 ? `0 0 10px ${color}` : 'none'
                    }}
                />
            </div>
        );
    };

    return (
        <div className="w-full bg-black/40 p-3 rounded-lg border border-white/5 space-y-2">
            <div className="flex items-center gap-2">
                <span className="text-[9px] font-mono text-gray-500 w-3">L</span>
                {renderBar(levels.l)}
            </div>
            <div className="flex items-center gap-2">
                <span className="text-[9px] font-mono text-gray-500 w-3">R</span>
                {renderBar(levels.r)}
            </div>
        </div>
    );
};

export const FFTMeter = () => {
    const [data, setData] = useState<{value: number, label: string}[]>(
        new Array(16).fill({ value: 0, label: '-' })
    );

    useEffect(() => {
        const handleFFT = (e: any) => setData([...e.detail]);
        window.addEventListener('audio-fft-data', handleFFT);
        return () => window.removeEventListener('audio-fft-data', handleFFT);
    }, []);

    return (
        <div className="w-full bg-black/80 p-4 rounded-lg border border-white/10 shadow-2xl">
            <div className="flex items-end gap-[2px] h-32 w-full">
                {data.map((item, i) => (
                    <div key={i} className="flex-1 h-full bg-green-900/20 relative">
                        {/* Barra Verde Rigida */}
                        <div
                            className="w-full absolute bottom-0 transition-all duration-75 ease-out"
                            style={{
                                height: `${Math.max(item.value * 100, 2)}%`,
                                backgroundColor: '#00ff88',
                                boxShadow: item.value > 0.5 ? '0 0 10px rgba(0, 255, 136, 0.4)' : 'none'
                            }}
                        />
                    </div>
                ))}
            </div>


        </div>
    );
};