export const formatBitrate = (bps: number | undefined | null) => {
    // Guard clause per valori mancanti o zero
    if (bps === undefined || bps === null || bps === 0) {
        return { value: "---", unit: "kbps" };
    }

    const kbps = bps / 1000;

    // Se supera i 10.000 kbps, passiamo a Mbps per risparmiare spazio
    if (kbps >= 10000) {
        return {
            // Usiamo toFixed(1) per mantenere la larghezza del testo costante
            value: (kbps / 1000).toFixed(1),
            unit: "Mbps"
        };
    }

    // Altrimenti mostriamo kbps arrotondati senza decimali
    return {
        value: Math.floor(kbps).toString(),
        unit: "kbps"
    };
};

export function BitrateStatCard({
                                    icon: Icon,
                                    label,
                                    videoRaw,
                                    audioRaw
                                }: { icon: any, label: string, videoRaw: number, audioRaw: number }) {

    // Formattiamo i dati qui dentro o passali già pronti
    const v = formatBitrate(videoRaw);
    const a = formatBitrate(audioRaw);
    return (
        <div className="bg-black border border-white/20 rounded-lg px-3 py-2 flex flex-col justify-center min-w-[150px]">
            <div className="flex items-center gap-1.5 mb-2 opacity-60">
                <Icon className="h-2.5 w-2.5 text-white" />
                <span className="text-[8px] font-black uppercase tracking-[0.2em] text-white">{label}</span>
            </div>

            <div className="grid grid-cols-2 gap-4">
                {/* VIDEO */}
                <div className="flex flex-col">
                    <span className="text-[7px] font-bold text-white/40 uppercase">Video</span>
                    <div className="flex items-baseline gap-0.5">
            <span className="text-xl font-black font-mono text-white leading-none">
              {v.value}
            </span>
                        <span className="text-[7px] font-bold text-white/30 uppercase">{v.unit}</span>
                    </div>
                </div>

                {/* AUDIO */}
                <div className="flex flex-col border-l border-white/10 pl-3">
                    <span className="text-[7px] font-bold text-white/40 uppercase">Audio</span>
                    <div className="flex items-baseline gap-0.5">
            <span className="text-xl font-black font-mono text-white leading-none">
              {a.value}
            </span>
                        <span className="text-[7px] font-bold text-white/30 uppercase">{a.unit}</span>
                    </div>
                </div>
            </div>
        </div>
    );
}