"use client";

import { useEffect, useRef } from 'react';
import { useWS } from '../../WebSocketContext';

interface VUMeterProps {
    inputIdx: number;
    active: boolean;
}

export function VUMeter_Horizontal({ inputIdx, active }: VUMeterProps) {
    const { audioRef } = useWS();
    const barLeftRef = useRef<HTMLDivElement>(null);
    const barRightRef = useRef<HTMLDivElement>(null);
    const rafRef = useRef<number>(0);

    const prevLeftRef = useRef(0);
    const prevRightRef = useRef(0);

    useEffect(() => {
        if (!active) {
            if (barLeftRef.current) barLeftRef.current.style.width = '0%';
            if (barRightRef.current) barRightRef.current.style.width = '0%';
            return;
        }

        const calculateDbLevel = (maxLinear: number) => {
            if (maxLinear <= 0.0001) return 0;
            const db = 20 * Math.log10(maxLinear);
            const minDb = -60;
            const level = ((db - minDb) / (0 - minDb)) * 100;
            return Math.max(0, Math.min(100, level));
        };

        const updateStyles = (ref: React.RefObject<HTMLDivElement | null>, level: number) => {
            if (!ref.current) return;

            // Cambiamo da height a width per l'orientamento orizzontale
            ref.current.style.width = `${level}%`;

            if (level > 90) ref.current.style.backgroundColor = '#ef4444';
            else if (level > 70) ref.current.style.backgroundColor = '#eab308';
            else ref.current.style.backgroundColor = '#22c55e';
        };

        const updateTick = () => {
            const data = inputIdx == 0 ? audioRef.current.master:audioRef.current.aux;
            if (data && data.length > 0) {
                let maxL = 0;
                let maxR = 0;

                for (let i = 0; i < data.length; i += 2) {
                    const sL = Math.abs(data[i]);
                    const sR = Math.abs(data[i + 1] || 0);
                    if (sL > maxL) maxL = sL;
                    if (sR > maxR) maxR = sR;
                }

                const targetL = calculateDbLevel(maxL);
                const targetR = calculateDbLevel(maxR);

                // Ballistica (Inerzia del VU Meter)
                const attack = 0.9;
                const release = 0.12;

                const currentL = targetL > prevLeftRef.current
                    ? prevLeftRef.current + (targetL - prevLeftRef.current) * attack
                    : prevLeftRef.current - (prevLeftRef.current - targetL) * release;

                const currentR = targetR > prevRightRef.current
                    ? prevRightRef.current + (targetR - prevRightRef.current) * attack
                    : prevRightRef.current - (prevRightRef.current - targetR) * release;

                prevLeftRef.current = currentL;
                prevRightRef.current = currentR;

                updateStyles(barLeftRef, currentL);
                updateStyles(barRightRef, currentR);
            }

            rafRef.current = requestAnimationFrame(updateTick);
        };

        rafRef.current = requestAnimationFrame(updateTick);
        return () => { if (rafRef.current) cancelAnimationFrame(rafRef.current); };
    }, [active, inputIdx, audioRef]);

    return (
        <div className="flex flex-col gap-[2px] w-full bg-black/80 p-[2px] rounded border border-white/10 h-6 relative shadow-inner overflow-hidden">

            {/* Canale L */}
            <div className="relative flex-1 w-full bg-white/5 overflow-hidden rounded-sm">
                <div ref={barLeftRef} className="absolute left-0 h-full transition-colors duration-200" style={{ width: '0%' }} />
            </div>

            {/* Canale R */}
            <div className="relative flex-1 w-full bg-white/5 overflow-hidden rounded-sm">
                <div ref={barRightRef} className="absolute left-0 h-full transition-colors duration-200" style={{ width: '0%' }} />
            </div>

            {/* Marcatori Decibel (Overlay Orizzontale) */}
            <div className="absolute inset-0 flex justify-between px-1 py-0.5 opacity-30 pointer-events-none text-[6px] font-mono text-white">
                <div className="h-full border-l border-white flex flex-col justify-end">inf</div>
                <div className="h-full border-l border-white/40 flex flex-col justify-end">-40</div>
                <div className="h-full border-l border-white/40 flex flex-col justify-end">-20</div>
                <div className="h-full border-l border-yellow-500/60 flex flex-col justify-end">-10</div>
                <div className="h-full border-l border-red-500 flex flex-col justify-end">0</div>
            </div>
        </div>
    );
}

interface VUMeterProps {
    inputIdx: number;
    active: boolean;
}

export function HorizontalVUMeter({ inputIdx, active }: VUMeterProps) {
    const { audioRef } = useWS();
    const barLeftRef = useRef<HTMLDivElement>(null);
    const barRightRef = useRef<HTMLDivElement>(null);
    const rafRef = useRef<number>(0);

    const prevLeftRef = useRef(0);
    const prevRightRef = useRef(0);

    useEffect(() => {
        if (!active) {
            if (barLeftRef.current) barLeftRef.current.style.width = '0%';
            if (barRightRef.current) barRightRef.current.style.width = '0%';
            return;
        }

        const calculateDbLevel = (maxLinear: number) => {
            if (maxLinear <= 0.0001) return 0;
            const db = 20 * Math.log10(maxLinear);
            const minDb = -60;
            const level = ((db - minDb) / (0 - minDb)) * 100;
            return Math.max(0, Math.min(100, level));
        };

        const updateStyles = (ref: React.RefObject<HTMLDivElement | null>, level: number) => {
            if (!ref.current) return;

            // Ora aggiorniamo la WIDTH invece della height
            ref.current.style.width = `${level}%`;

            if (level > 90) ref.current.style.backgroundColor = '#ef4444';
            else if (level > 70) ref.current.style.backgroundColor = '#eab308';
            else ref.current.style.backgroundColor = '#22c55e';
        };

        const updateTick = () => {
            const data = inputIdx == 0 ? audioRef.current.master:audioRef.current.aux;
            if (data && data.length > 0) {
                let maxL = 0;
                let maxR = 0;

                for (let i = 0; i < data.length; i += 2) {
                    const sL = Math.abs(data[i]);
                    const sR = Math.abs(data[i + 1] || 0);
                    if (sL > maxL) maxL = sL;
                    if (sR > maxR) maxR = sR;
                }

                const targetL = calculateDbLevel(maxL);
                const targetR = calculateDbLevel(maxR);

                const attack = 0.9;
                const release = 0.12;

                const currentL = targetL > prevLeftRef.current
                    ? prevLeftRef.current + (targetL - prevLeftRef.current) * attack
                    : prevLeftRef.current - (prevLeftRef.current - targetL) * release;

                const currentR = targetR > prevRightRef.current
                    ? prevRightRef.current + (targetR - prevRightRef.current) * attack
                    : prevRightRef.current - (prevRightRef.current - targetR) * release;

                prevLeftRef.current = currentL;
                prevRightRef.current = currentR;

                updateStyles(barLeftRef, currentL);
                updateStyles(barRightRef, currentR);
            }
            rafRef.current = requestAnimationFrame(updateTick);
        };

        rafRef.current = requestAnimationFrame(updateTick);
        return () => { if (rafRef.current) cancelAnimationFrame(rafRef.current); };
    }, [active, inputIdx, audioRef]);

    return (
        <div className="flex flex-col gap-[1px] w-full bg-black/80 p-[2px] rounded border border-white/10 h-6 relative shadow-inner overflow-hidden">

            {/* Canale L (Sopra) */}
            <div className="relative flex-1 w-full bg-white/5 overflow-hidden rounded-[1px]">
                <div ref={barLeftRef} className="absolute left-0 h-full transition-colors duration-200" style={{ width: '0%' }} />
            </div>

            {/* Canale R (Sotto) */}
            <div className="relative flex-1 w-full bg-white/5 overflow-hidden rounded-[1px]">
                <div ref={barRightRef} className="absolute left-0 h-full transition-colors duration-200" style={{ width: '0%' }} />
            </div>

            {/* Marcatori Decibel (Overlay Orizzontale) */}
            <div className="absolute inset-0 flex justify-between px-1 opacity-30 pointer-events-none text-[6px] font-mono text-white">
                <div className="h-full border-l border-white/20 flex flex-col justify-end pb-0.5">inf</div>
                <div className="h-full border-l border-white/20 flex flex-col justify-end pb-0.5">-40</div>
                <div className="h-full border-l border-white/20 flex flex-col justify-end pb-0.5">-20</div>
                <div className="h-full border-l border-yellow-500/50 flex flex-col justify-end pb-0.5">-10</div>
                <div className="h-full border-l border-red-500 flex flex-col justify-end pb-0.5">0</div>
            </div>
        </div>
    );
}