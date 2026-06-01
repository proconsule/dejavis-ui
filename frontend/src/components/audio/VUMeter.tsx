"use client";

import { useEffect, useRef } from 'react';
import { useWS } from '../../WebSocketContext';

interface VUMeterProps {
    inputIdx: number;
    active: boolean;
}

export function VUMeter({ inputIdx, active }: VUMeterProps) {
    const { audioRef } = useWS();
    const barLeftRef = useRef<HTMLDivElement>(null);
    const barRightRef = useRef<HTMLDivElement>(null);
    const rafRef = useRef<number>(0);

    const prevLeftRef = useRef(0);
    const prevRightRef = useRef(0);

    useEffect(() => {
        if (!active) {
            if (barLeftRef.current) barLeftRef.current.style.height = '0%';
            if (barRightRef.current) barRightRef.current.style.height = '0%';
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

            ref.current.style.height = `${level}%`;

            if (level > 90) ref.current.style.backgroundColor = '#ef4444';
            else if (level > 70) ref.current.style.backgroundColor = '#eab308';
            else ref.current.style.backgroundColor = '#22c55e';
        };

        const updateTick = () => {
            const data = audioRef.current.inputs[inputIdx];

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

        return () => {
            if (rafRef.current) cancelAnimationFrame(rafRef.current);
        };
    }, [active, inputIdx, audioRef]);

    return (
        <div className="flex gap-[1px] h-full bg-black/80 p-[2px] rounded border border-white/10 w-6 relative shadow-2xl">
            <div className="relative flex-1 h-full bg-white/5 overflow-hidden">
                <div ref={barLeftRef} className="absolute bottom-0 w-full transition-colors duration-200" style={{ height: '0%' }} />
            </div>
           <div className="relative flex-1 h-full bg-white/5 overflow-hidden">
                <div ref={barRightRef} className="absolute bottom-0 w-full transition-colors duration-200" style={{ height: '0%' }} />
            </div>

            <div className="absolute inset-0 flex flex-col justify-between py-1 opacity-40 pointer-events-none text-[6px] font-mono text-white px-0.5">
                <div className="border-t border-red-500 w-full">0</div>
                <div className="border-t border-yellow-500 w-full opacity-50">-10</div>
                <div className="border-t border-white w-full opacity-30">-20</div>
                <div className="border-t border-white w-full opacity-20">-40</div>
                <div className="w-full text-center">inf</div>
            </div>
        </div>
    );
}

export function VUMeterOutput({ inputIdx, active }: VUMeterProps) {
    const { audioRef } = useWS();
    const barLeftRef = useRef<HTMLDivElement>(null);
    const barRightRef = useRef<HTMLDivElement>(null);
    const rafRef = useRef<number>(0);

    // Memoria per il filtro di smoothing
    const prevLeftRef = useRef(0);
    const prevRightRef = useRef(0);

    useEffect(() => {
        if (!active) {
            if (barLeftRef.current) barLeftRef.current.style.height = '0%';
            if (barRightRef.current) barRightRef.current.style.height = '0%';
            return;
        }

        // Funzione per convertire il valore lineare in percentuale basata su dB
        const calculateDbLevel = (maxLinear: number) => {
            if (maxLinear <= 0.0001) return 0; // Silenzio o rumore di fondo trascurabile

            // Calcolo dei Decibel: 20 * log10(Ampiezza)
            const db = 20 * Math.log10(maxLinear);

            // Mapping: Assumiamo un range da -60dB (0%) a 0dB (100%)
            // La formula: (db - min) / (max - min) * 100
            const minDb = -60;
            const level = ((db - minDb) / (0 - minDb)) * 100;

            return Math.max(0, Math.min(100, level));
        };

        const updateStyles = (ref: React.RefObject<HTMLDivElement | null>, level: number) => {
            // Il controllo if (!ref.current) ora soddisfa TypeScript
            if (!ref.current) return;

            ref.current.style.height = `${level}%`;

            // Colori standard da mixer
            if (level > 90) ref.current.style.backgroundColor = '#ef4444';
            else if (level > 70) ref.current.style.backgroundColor = '#eab308';
            else ref.current.style.backgroundColor = '#22c55e';
        };

        const updateTick = () => {
            const data = inputIdx == 0 ? audioRef.current.master:audioRef.current.aux;

            if (data && data.length > 0) {
                let maxL = 0;
                let maxR = 0;

                // Estrazione picchi (Interleaved)
                for (let i = 0; i < data.length; i += 2) {
                    const sL = Math.abs(data[i]);
                    const sR = Math.abs(data[i + 1] || 0);
                    if (sL > maxL) maxL = sL;
                    if (sR > maxR) maxR = sR;
                }

                // Conversione in scala dB (%)
                const targetL = calculateDbLevel(maxL);
                const targetR = calculateDbLevel(maxR);

                // FILTRO MORBIDO (Ballistics)
                // Salita istantanea (0.9), Discesa lenta (0.12) per simulare l'inerzia
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

        return () => {
            if (rafRef.current) cancelAnimationFrame(rafRef.current);
        };
    }, [active, inputIdx, audioRef]);

    return (
        <div className="flex gap-[1px] h-full bg-black/80 p-[2px] rounded border border-white/10 w-6 relative shadow-2xl">
            {/* Canale L */}
            <div className="relative flex-1 h-full bg-white/5 overflow-hidden">
                <div ref={barLeftRef} className="absolute bottom-0 w-full transition-colors duration-200" style={{ height: '0%' }} />
            </div>
            {/* Canale R */}
            <div className="relative flex-1 h-full bg-white/5 overflow-hidden">
                <div ref={barRightRef} className="absolute bottom-0 w-full transition-colors duration-200" style={{ height: '0%' }} />
            </div>

            {/* Marcatori Decibel (Overlay) */}
            <div className="absolute inset-0 flex flex-col justify-between py-1 opacity-40 pointer-events-none text-[6px] font-mono text-white px-0.5">
                <div className="border-t border-red-500 w-full">0</div>
                <div className="border-t border-yellow-500 w-full opacity-50">-10</div>
                <div className="border-t border-white w-full opacity-30">-20</div>
                <div className="border-t border-white w-full opacity-20">-40</div>
                <div className="w-full text-center">inf</div>
            </div>
        </div>
    );
}