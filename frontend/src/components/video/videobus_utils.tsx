export const BusSelector_Layer = ({ video_input_idx, value, onChange }: { video_input_idx:number, value: number, onChange: (idx: number,val: number) => void }) => {
    return (
        <div className="flex flex-col gap-2 mb-4 p-2 bg-slate-800/50 rounded-lg border border-slate-700">
            <span className="text-[10px] font-bold text-slate-500 uppercase tracking-tight">Video Bus</span>
            <div className="flex gap-1 p-1 bg-black/40 rounded-md">
                {[
                    { label: 'Bus A', value: 0 },
                    { label: 'Bus B', value: 1 },
                ].map((bus) => (
                    <button
                        key={bus.value}
                        onClick={() => onChange(video_input_idx,bus.value)}
                        className={`flex-1 py-1 text-[10px] font-bold rounded transition-all ${
                            value === bus.value
                                ? 'bg-emerald-600 text-white shadow-sm'
                                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-700'
                        }`}
                    >
                        {bus.label}
                    </button>
                ))}
            </div>
        </div>
    );
};

export const BusSelector_External = ({ label,value, onChange }: {label: string, value: number, onChange: (val: number) => void }) => {
    return (
        <div className="flex flex-col gap-2 mb-4 p-2 bg-slate-800/50 rounded-lg border border-slate-700">
        <span className="text-[10px] font-bold text-slate-500 uppercase tracking-tight">{label}</span>
    <div className="flex gap-1 p-1 bg-black/40 rounded-md">
        {[
                { label: 'Bus A', value: 0 },
    { label: 'Bus B', value: 1 },
].map((bus) => (
        <button
            key={bus.value}
    onClick={() => onChange(bus.value)}
    className={`flex-1 py-1 text-[10px] font-bold rounded transition-all ${
        value === bus.value
            ? 'bg-emerald-600 text-white shadow-sm'
            : 'text-slate-400 hover:text-slate-200 hover:bg-slate-700'
    }`}
>
    {bus.label}
    </button>
))}
    </div>
    </div>
);
};